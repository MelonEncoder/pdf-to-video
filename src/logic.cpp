#include "poppler.hpp"
#include "opencv.hpp"
#include "ptv.hpp"
#include "logic.hpp"
#include <algorithm>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <utility>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <ctime>

using std::string;
using std::vector;
namespace fs = std::filesystem;

// void pdf_loading_test() {

// }

void scale_images_to_width(vector<cv::Mat> &images, int dst_width) {
    for (int i = 0; i < (int)images.size(); i++) {
        cv::Mat new_img;
        double scale = (double)dst_width / (double)images[i].cols;
        cv::resize(images[i], new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
        images[i] = new_img;
    }
}

void scale_images_to_height(vector<cv::Mat> &images, int dst_height) {
    for (int i = 0; i < (int)images.size(); i++) {
        cv::Mat new_img;
        double scale = (double)dst_height / (double)images[i].cols;
        cv::resize(images[i], new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
        images[i] = new_img;
    }
}

void scale_images_to_fit(vector<cv::Mat> &images, struct VP &vp) {
    for (int i = 0; i < (int)images.size(); i++) {
        cv::Mat img(images[i]);
        double scale_w;
        double scale_h;
        double scale = 1.0;

        if (img.cols > vp.width && img.cols > vp.height) {
            scale_w = (double)vp.width / (double)images[i].cols;
            scale_h = (double)vp.height / (double)images[i].rows;
            scale = std::min(scale_w, scale_h);
        }
        if (img.cols < vp.width && img.rows < vp.height) {
            scale_w = (double)vp.width / (double)images[i].cols;
            scale_h = (double)vp.height / (double)images[i].rows;
            scale = std::min(scale_w, scale_h);
        }
        if (img.cols > vp.width && img.rows <= vp.height) {
            scale_w = (double)vp.width / (double)img.cols;
            scale = scale_w;
        }
        if (img.cols <= vp.width && img.rows > vp.height) {
            scale_h = (double)vp.height / (double)img.rows;
            scale = scale_h;
        }

        cv::Mat new_img;
        cv::resize(images[i], new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
        images[i] = new_img;
    }
}

void pdf_to_images(string pdf_dir, poppler::document *pdf, struct VP &vp, Style style) {
    int page_count = pdf->pages();
    auto renderer = poppler::page_renderer();
    string image_type = "jpg";

    std::cout << "Generating PDF Pages..." << std::endl;
    for (int i = 0; i < page_count; i++) {
        double dpi = DEFAULT_DPI;
        string pg_name = get_page_name(i, page_count);
        string path = pdf_dir + pg_name + "." + image_type;

        poppler::page *page = pdf->create_page(i);
        // just in case pages are different sizes.
        if (style == Style::SCROLL) {
            dpi = get_scaled_dpi_from_width(page, vp.width);
        } else if (style == Style::SEQUENCE) {
            dpi = get_scaled_dpi(page, vp);
        }

        poppler::image tmp_img = renderer.render_page(page, dpi, dpi);
        tmp_img.save(path, image_type);

        std::cout << std::to_string(i + 1) + "/" + std::to_string(page_count) << std::endl;
    }
}

void make_frames_dir(string frames_dir) {
    if (fs::exists(frames_dir)) {
        std::cout << "Frames directory already exists." << std::endl;
    }
    else {
        fs::create_directory(frames_dir);
        std::cout << "Created frames directory." << std::endl;
    }
}

void make_pdf_dir(string pdf_dir) {
    if (!fs::exists(pdf_dir)) {
        fs::create_directory(pdf_dir);
        std::cout << "Created PDF directory." << std::endl;
    } else {
        std::cout << "PDF directory already exists." << std::endl;
    }
}

string format_path(string str) {
    string bad_chars = " ()&-<>";
    for (int i = 0; i < (int)str.length(); i++) {
        if ((int)bad_chars.find(str[i]) > -1) {
            str[i] = '_';
        }
    }
    return str;
}

void generate_video(string frames_dir, string output, struct VP &vp) {
    while (fs::exists(output)) {
        output.insert((output.length() - 4), 1, '+');
    }

    vector<string> cmd_args = {
        "-framerate",
        std::to_string(vp.fps),
        "-i",
        frames_dir + "%06d.jpg",
        "-s",
        std::to_string(vp.width) + "x" + std::to_string(vp.height),
        output
    };

    string cmd = "ffmpeg";
    for (string arg : cmd_args) {
        cmd += " " + arg;
    }

    std::cout << "FFmpeg CMD: " << cmd << std::endl;

    std::cout << "Generating video..." << std::endl;
    std::system(cmd.c_str());

    std::cout << "Output path: " << output << std::endl;
}

void generate_scroll_frames(string frames_dir, int pages, cv::Mat long_image, struct VP &vp) {
    int pixels_translated = (double)long_image.rows / (vp.fps * vp.spp * pages);
    if (pixels_translated == 0) {
        pixels_translated = 1;
    }
    int height = long_image.rows - vp.height; // - vp.rows prevents out of bounds error with cv::Rect roi
    cv::Mat tmp_img(vp.height, vp.width, CV_8UC3);

    std::cout << "Pixels per frame: " << pixels_translated << std::endl;

    for (int h = 0, i = 0; h < height; h += pixels_translated, i++) {
        cv::Rect roi = cv::Rect(0, h, vp.width, vp.height);
        long_image(roi).copyTo(tmp_img);
        string file = frames_dir + get_frame_name(i) + ".jpg";
        cv::imwrite(file, tmp_img, {cv::IMWRITE_JPEG_QUALITY, 90});
    }
}

void generate_sequence_frames(string frames_dir, int pages, vector<cv::Mat> images, struct VP &vp) {
    int count = 0;
    cv::Mat vp_img;

    for (int i = 0; i < (int)images.size(); i ++) {
        vp_img = cv::Mat(vp.height, vp.width, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::Mat img(images[i]);
        int x = 0;
        int y = 0;

        // adds offset
        if (vp_img.cols - img.cols >= 2) {
            x += (vp_img.cols - img.cols) / 2;
        } else if (vp_img.rows - img.rows >= 2) {
            y += (vp_img.rows - img.rows) / 2;
        }

        // prevents stretching of images when being rendered.
        // keeps them within the vp.
        cv::Rect2i roi(x, y, img.cols, img.rows);
        img.copyTo(vp_img(roi));

        string path = frames_dir + get_frame_name(count) + ".jpg";
        cv::imwrite(path, vp_img, {cv::IMWRITE_JPEG_QUALITY, 90});
        count++;
    }
}

string get_frames_dir(string dir) {
    std::srand(std::time(0));
    int rand_num = std::rand() % 999999 + 100000;

    int end = dir.length() - 1;
    for (int i = dir.length() - 2; i > -1; i--) {
        if (dir[i] == '/') {
            end = i + 1;
            break;
        }
    }

    string result = dir.substr(0, end) + "ptv-" + std::to_string(rand_num) + "-frames/";
    return result;
}

vector<cv::Mat> get_images(string dir) {
    vector<cv::Mat> images;
    std::map<int, string> image_map;
    // creates hash-map of valid image paths in numerical order
    for (const auto &entry : fs::directory_iterator(dir)) {
        string path;
        int index;
        if (!fs::is_directory(entry)) {
            path = entry.path().string();
            string name = entry.path().filename().string();
            try {
                index = std::stoi(name.substr(0, name.length() - name.find_last_of('.')));
            } catch (const std::invalid_argument& e) {
                std::cerr << "<!> Invalid Argument: " << name << " contains no int so is not used." << std::endl;
            } catch (const std::out_of_range& e) {
                std::cerr << "<!> Out of Range: geting int value from image name, get_images()." << std::endl;
            }
            image_map.insert(std::make_pair(index, path));
        }
    }
    // reads images in numerical order
    for (int i = 0; i < (int)image_map.size(); i++) {
        if (image_map[i] == "") {
            continue;
        }
        cv::Mat tmp = cv::imread(image_map[i]);

        // makes dimentions of the image divisible by 2.
        // ffmpeg will get upset if otherwise.
        int rows = tmp.rows % 2 != 0 ? tmp.rows + 1 : tmp.rows;
        int cols = tmp.cols % 2 != 0 ? tmp.cols + 1 : tmp.cols;

        cv::Rect2i roi(0, 0, tmp.cols, tmp.rows);
        cv::Mat tmp2(rows, cols, CV_8UC3);
        tmp.copyTo(tmp2(roi));

        images.push_back(cv::Mat(tmp2));
    }
    return images;
}


cv::Mat get_long_image(int total_images, string images_dir, struct VP &vp) {
    int height = 0;
    vector<cv::Mat> images;
    cv::Mat long_image;

    // stores exported pdf pages as images in vector.
    for (int i = 0; i < total_images; i++) {
        cv::Mat tmp_img = cv::imread(images_dir + get_page_name(i, total_images) + ".jpg", cv::IMREAD_COLOR);
        height += tmp_img.rows;
        images.push_back(tmp_img);
    }
    // adds white space before and after content.
    height += vp.height * 2;

    // images that will contain all contents of pdf
    long_image = cv::Mat(height, vp.width, CV_8UC3);

    // sequentually adds all pages of pdf in order to long_image.
    for (int i = 0, h = vp.height; i < total_images; i++) {
        cv::Mat tmp_img = images[i];
        // ROI -> Region of Interest
        cv::Rect roi = cv::Rect(0, h, long_image.cols, tmp_img.rows);
        tmp_img.copyTo(long_image(roi));
        h += tmp_img.rows;
    }

    return long_image;
}

cv::Mat get_long_image(int total_images, vector<cv::Mat> &images, struct VP &vp) {
    int height = 0;
    cv::Mat long_image;

    // stores exported pdf pages as images in vector.
    for (int i = 0; i < total_images; i++) {
        height += images[i].rows;
    }
    // adds white space before and after content.
    height += vp.height * 2;

    // images that will contain all contents of pdf
    long_image = cv::Mat(height, vp.width, CV_8UC3);

    // sequentually adds all pages of pdf in order to long_image.
    for (int i = 0, h = vp.height; i < total_images; i++) {
        cv::Mat tmp_img = images[i];
        // ROI -> Region of Interest
        cv::Rect roi = cv::Rect(0, h, long_image.cols, tmp_img.rows);
        tmp_img.copyTo(long_image(roi));
        h += tmp_img.rows;
    }

    return long_image;
}

// returns dpi to scale page to viewport width
double get_scaled_dpi_from_width(poppler::page *page, int width) {
    auto rect = page->page_rect(poppler::media_box);

    if (rect.width() == width) {
        return DEFAULT_DPI;
    }

    return ((double)width * DEFAULT_DPI) / (double)rect.width();
}

// returns dpi that will scale the pdf page to fit the viewport dimentions
double get_scaled_dpi(poppler::page *page, struct VP &vp) {
    double dpi_w;
    double dpi_h;
    poppler::rectf rect = page->page_rect(poppler::media_box);

    if (rect.width() > vp.width && rect.height() > vp.height) {
        dpi_w = ((double)vp.width * DEFAULT_DPI) / rect.width();
        dpi_h = ((double)vp.height * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    }
    if (rect.width() < vp.width && rect.height() < vp.height) {
        dpi_w = ((double)vp.width * DEFAULT_DPI) / rect.width();
        dpi_h = ((double)vp.height * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    }
    if (rect.width() > vp.width && rect.height() <= vp.height) {
        dpi_w = ((double)vp.width * DEFAULT_DPI) / rect.width();
        return dpi_w;
    }
    if (rect.width() <= vp.width && rect.height() > vp.height) {
        dpi_h = ((double)vp.height * DEFAULT_DPI) / rect.height();
        return dpi_h;
    }

    return DEFAULT_DPI;
}

string get_pdf_dir(string pdf_path) {
    string pdf_dir = pdf_path.substr(0, pdf_path.length() - 4) + "_" + pdf_path.substr(pdf_path.length() - 3) + "/";
    return pdf_dir;
}



string get_pdf_name(string pdf_path) {
    return pdf_path.substr(pdf_path.find_last_of("/") + 1, pdf_path.length() - 7);
}



// returns numerical name as string no file extension
string get_frame_name(int index) {
    if (index < 10) {
        return "00000" + std::to_string(index);
    } else if (index < 100) {
        return "0000" + std::to_string(index);
    } else if (index < 1000) {
        return "000" + std::to_string(index);
    } else if (index < 10000) {
        return "00" + std::to_string(index);
    } else if (index < 100000) {
        return "0" + std::to_string(index);
    } else if (index < 1000000) {
        return std::to_string(index);
    }
    return "z";
}

// returns numerical name as string no file extension
string get_page_name(int index, int pages) {
    if (pages < 10) {
        return std::to_string(index);
    } else if (pages < 100) {
        if (index < 10) {
            return "0" + std::to_string(index);
        } else {
            return std::to_string(index);
        }
    } else if (pages < 1000) {
        if (index < 10) {
            return "00" + std::to_string(index);
        } else if (index < 100) {
            return "0" + std::to_string(index);
        } else {
            return std::to_string(index);
        }
    } else if (pages < 10000) {
        if (index < 10) {
            return "000" + std::to_string(index);
        } else if (index < 100) {
            return "00" + std::to_string(index);
        } else if (index < 1000) {
            return "0" + std::to_string(index);
        } else {
            return std::to_string(index);
        }
    } else {
        if (index < 10) {
            return "0000" + std::to_string(index);
        } else if (index < 100) {
            return "000" + std::to_string(index);
        } else if (index < 1000) {
            return "00" + std::to_string(index);
        } else if (index < 10000) {
            return "0" + std::to_string(index);
        } else if (index < 100000) {
            return std::to_string(index);
        }
    }
    return "z";
}

bool delete_dir(string dir) {
    if (fs::is_directory(dir)) {
        return fs::remove_all(fs::path(dir));
    }
    std::cout << "<!> Error: cannot delete " << dir << "" << std::endl;
    return false;
}
