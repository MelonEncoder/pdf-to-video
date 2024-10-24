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
#include <string>
#include <filesystem>
#include <utility>
#include <vector>
#include <cstdlib>
#include <cstdio>

using std::string;
using std::vector;
namespace fs = std::filesystem;

void scale_img_to_width(cv::Mat &image, int new_width) {
    cv::Mat new_img;
    double scale = (double)new_width / (double)image.cols;
    cv::resize(image, new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
    image = new_img;
}
void scale_img_to_height(cv::Mat &image, int new_height) {
    cv::Mat new_img;
    double scale = (double)new_height / (double)image.rows;
    cv::resize(image, new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
    image = new_img;
}
void scale_img_to_width(vector<cv::Mat> &images, int new_width) {
    for (int i = 0; i < (int)images.size(); i++) {
        cv::Mat new_img;
        double scale = (double)new_width / (double)images[i].cols;
        cv::resize(images[i], new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
        images[i] = new_img;
    }
}
void scale_img_to_height(vector<cv::Mat> &images, int new_height) {
    for (int i = 0; i < (int)images.size(); i++) {
        cv::Mat new_img;
        double scale = (double)new_height / (double)images[i].rows;
        cv::resize(images[i], new_img, cv::Size(), scale, scale, cv::INTER_LINEAR);
        images[i] = new_img;
    }
}

// dir contains numbered images
void pad_image_names(string dir) {
    vector<string> old_paths;
    vector<string> img_names;
    vector<string> file_extensions;

    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!fs::is_directory(entry)) {
            string file_name = entry.path().filename().string();

            img_names.push_back(file_name.substr(0, file_name.find('.')));
            file_extensions.push_back(file_name.substr(file_name.find('.')));
            old_paths.push_back(entry.path().string());
        }
    }

    for (int i = 0; i < (int)old_paths.size(); i++) {
        string new_path = dir + get_page_name(std::stoi(img_names[i]), old_paths.size()) + file_extensions[i];
        if (std::rename(old_paths[i].c_str(), new_path.c_str())) {
            std::cerr << "Error renaming " + old_paths[i] << std::endl;
        }
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

void generate_video(string pdf_dir, string frames_dir, string output, struct VP &vp) {
    // ffmpeg -f image2 -framerate 12 -i ./%04d.jpg -s 1920x1080 e.mp4
    // ffmpeg -framerate 1/2 -i "%04d.jpg" -c:v libx264 -crf 23 -preset medium -vf scale=1280:-1 output.mp4
    while (fs::exists(output)) {
        output.insert(output.length() - 4, "+");
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

    std::cout << "CMD: " << cmd << std::endl;

    std::cout << "Generating video..." << std::endl;
    std::system(cmd.c_str());

    std::cout << "Output path: " << output << std::endl;
}

void generate_scroll_frames(string frames_dir, int pages, cv::Mat long_image, struct VP &vp) {
    int pixels_translated = (double)long_image.rows / (vp.fps * vp.spp * pages);
    int height = long_image.rows - vp.height; // - vp.rows prevents out of bounds error with cv::Rect roi
    cv::Mat tmp_img(vp.height, vp.width, CV_8UC3);

    // std::cout << "height: " << long_image.rows << " width: " << long_image.cols << std::endl;
    std::cout << "pixels per frame: " << pixels_translated << std::endl;
    // std::cout << "total_height: " << height << std::endl;

    std::cout << "Generating video frames..." << std::endl;
    for (int h = 0, i = 0; h < height; h += pixels_translated, i++) {
        cv::Rect roi = cv::Rect(0, h, vp.width, vp.height);
        long_image(roi).copyTo(tmp_img);
        string file = frames_dir + get_frame_name(i) + ".jpg";
        cv::imwrite(file, tmp_img, {cv::IMWRITE_JPEG_QUALITY, 90});
    }
}

void generate_sequence_frames(string frames_dir, int pages, vector<cv::Mat> images, struct VP &vp) {
    int count = 0;
    std::cout << "Generating video frames..." << std::endl;
    cv::Mat vp_img;

    for (int i = 0; i < (int)images.size(); i ++) {
        vp_img = cv::Mat(vp.height, vp.width, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::Mat img(images[i]);
        int x = 0;
        int y = 0;

        // std::cout << "img_cols: " << img.cols << "\timg_rows: " << img.rows << std::endl;
        // std::cout << "vp_cols: " << vp_img.cols << "\tvp_rows: " << vp_img.rows << std::endl;
        // std::cout << "X: " << x << "\tY: " << y << std::endl;

        // adds offset
        if (vp_img.cols - img.cols >= 2) {
            x += (vp_img.cols - img.cols) / 2;
        } else if (vp_img.rows - img.rows >= 2) {
            y += (vp_img.rows - img.rows) / 2;
        }

        // std::cout << "X: " << x << "\tY: " << y << std::endl;

        // prevents stretching of images when being rendered.
        // keeps them within the vp.
        cv::Rect2i roi(x, y, img.cols, img.rows);
        img.copyTo(vp_img(roi));

        string path = frames_dir + get_frame_name(count) + ".jpg";
        // std::cout << path << std::endl;
        cv::imwrite(path, vp_img, {cv::IMWRITE_JPEG_QUALITY, 90});
        count++;
    }
}

string get_frames_dir(string pdf_dir) {
    return pdf_dir + "frames/";
}

vector<cv::Mat> get_images(string dir) {
    vector<cv::Mat> images;
    std::map<int, string> image_map;
    for (const auto &entry : fs::directory_iterator(dir)) {
        string path;
        int index;
        if (!fs::is_directory(entry)) {
            path = entry.path().string();
            string name = entry.path().filename().string();
            index = std::stoi(name.substr(0, name.length() - name.find_last_of('.')));
            image_map.insert(std::make_pair(index, path));
            // std::cout << "index: " << index << "  path: " << path << std::endl;
        }
    }
    // reads images in numerical order
    for (int i = 0; i < (int)image_map.size(); i++) {
        cv::Mat tmp = cv::imread(image_map[i]);
        images.push_back(tmp);
    }
    return images;
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

cv::Mat get_long_image(int pages, string pdf_dir, struct VP &vp) {
    int height = 0;
    vector<cv::Mat> images;
    cv::Mat long_image;

    // stores exported pdf pages as images in vector.
    for (int i = 0; i < pages; i++) {
        cv::Mat tmp_img = cv::imread(pdf_dir + get_page_name(i, pages) + ".jpg", cv::IMREAD_COLOR);
        height += tmp_img.rows;
        images.push_back(tmp_img);
    }
    // adds white space before and after content.
    height += vp.height * 2;

    // images that will contain all contents of pdf
    long_image = cv::Mat(height, vp.width, CV_8UC3);

    // sequentually adds all pages of pdf in order to long_image.
    for (int i = 0, h = vp.height; i < pages; i++) {
        cv::Mat tmp_img = images[i];
        // std::cout << tmp_img.rows << std::endl;
        // ROI -> Region of Interest
        cv::Rect roi = cv::Rect(0, h, long_image.cols, tmp_img.rows);
        tmp_img.copyTo(long_image(roi));
        h += tmp_img.rows;
    }
    // cv::imwrite("../longboy.jpg", long_image);

    return long_image;
}

// returns dpi to scale page to viewport width
double get_scaled_dpi_from_width(poppler::page *page, int width) {
    auto rect = page->page_rect(poppler::media_box);
    // std::cout << "Rect_w: " << rect.width() << "  width: " << width << std::endl;

    if (rect.width() == width) {
        return DEFAULT_DPI;
    }

    return ((double)width * DEFAULT_DPI) / (double)rect.width();
}

// returns dpi to scale page to viewport height
double get_scaled_dpi_from_height(poppler::page *page, int height) {
    auto rect = page->page_rect(poppler::media_box);

    if (rect.height() == height) {
        return DEFAULT_DPI;
    }

    return ((double)height * DEFAULT_DPI) / (double)rect.height();
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

void make_pdf_dir(string pdf_dir) {
    if (!fs::exists(pdf_dir)) {
        fs::create_directory(pdf_dir);
        std::cout << "Created PDF directory." << std::endl;
    } else {
        std::cout << "PDF directory already exists." << std::endl;
    }
}

bool delete_dir(string dir) {
    if (fs::is_directory(dir)) {
        return fs::remove_all(fs::path(dir));
    }
    std::cout << "Error: cannot delete " << dir << "" << std::endl;
    return false;
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
