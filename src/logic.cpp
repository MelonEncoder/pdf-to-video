#include "poppler.hpp"
#include "opencv.hpp"
#include "logic.hpp"
#include <cstdio>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdlib>

using std::string;
using std::vector;
namespace fs = std::filesystem;

////////
class Viewport {
    int width;
    int height;
    int fps;
    int spp;
    string video_fmt;

    public:
        // Vairables
        cv::Mat img;

        // Constructors
        Viewport(int width, int height, int fps, int seconds_per_page, string video_fmt) {
            this->width = width;
            this->height = height;
            this->fps = fps;
            this->spp = seconds_per_page;
            this->video_fmt = video_fmt;
            this->img = cv::Mat(height, width, CV_8UC3);
        }

        // Getters
        int get_width() {
            return this->width;
        }
        int get_height() {
            return this->height;
        }
        int get_fps() {
            return this->fps;
        }
        int get_spp() {
            return this->spp;
        }
        string get_fmt() {
            return this->video_fmt;
        }
};

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

void save_pages(string pdf_path, double scaled_dpi, bool keep_resolution, poppler::document *pdf) {
    int page_count = pdf->pages();
    auto renderer = poppler::page_renderer();
    string image_type = "jpg";
    string pdf_dir = get_pdf_dir(pdf_path);
    double dpi = 72.0;

    for (int i = 0; i < page_count; i++) {
        string pg_name = get_page_name(i, page_count);
        string path = pdf_dir + pg_name + "." + image_type;

        poppler::page *page = pdf->create_page(i);
        if (!keep_resolution) {
            dpi = scaled_dpi;
        }

        poppler::image tmp_img = renderer.render_page(page, dpi, dpi);
        tmp_img.save(path, image_type);

        std::cout << std::to_string(i + 1) + "/" + std::to_string(page_count) << std::endl;
    }
}

void generate_video(string pdf_path, string pdf_dir, Viewport &vp) {
    // ffmpeg -f image2 -framerate 12 -i ./%04d.jpg -s 1920x1080 e.mp4
    // ffmpeg -framerate 1/2 -i "%04d.jpg" -c:v libx264 -crf 23 -preset medium -vf scale=1280:-1 output.mp4
    string output = pdf_dir + get_pdf_name(pdf_path) + "." + vp.get_fmt();

    while (fs::exists(output)) {
        output.insert(output.length() - 4, "+");
    }

    vector<string> cmd_args = {
        "-framerate",
        std::to_string(vp.get_fps()),
        "-i",
        get_frames_dir(pdf_dir) + "%05d.jpg",
        "-s",
        std::to_string(vp.get_width()) + "x" + std::to_string(vp.get_height()),
        output
    };

    string cmd = "ffmpeg";
    for (string arg : cmd_args) {
        cmd += " " + arg;
    }

    std::cout << "CMD: " << cmd << std::endl;

    std::cout << "Generating video." << std::endl;
    std::system(cmd.c_str());
    std::cout << "Finished." << std::endl;
}

void generate_frames(string frames_dir, cv::Mat long_image, poppler::document *pdf, Viewport &vp) {
    std::cout << "Generating video frames." << std::endl;
    int pixels_translated = (double)long_image.rows / (vp.get_fps() * vp.get_spp() * pdf->pages());
    int height = long_image.rows - vp.img.rows; // - vp.rows prevents out of bounds error with cv::Rect roi

    // std::cout << "height: " << long_image.rows << " width: " << long_image.cols << std::endl;
    std::cout << "pixels per frame: " << pixels_translated << std::endl;
    // std::cout << "total_height: " << height << std::endl;

    for (int h = 0, i = 0; h < height; h += pixels_translated, i++) {
        cv::Rect roi = cv::Rect(0, h, vp.img.cols, vp.img.rows);
        long_image(roi).copyTo(vp.img);
        string file = frames_dir + get_frame_name(i) + ".jpg";
        cv::imwrite(file, vp.img);
    }
    std::cout << "Finished generating video frames." << std::endl;
}

string get_frames_dir(string pdf_dir) {
    return pdf_dir + "frames/";
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

cv::Mat get_long_image(int pages, string pdf_dir, Viewport &vp) {
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
    height += vp.img.rows * 2;

    // images that will contain all pages of pdf
    long_image = cv::Mat(height, vp.img.cols, CV_8UC3);

    // sequentually adds all pages of pdf in order to long_image.
    for (int i = 0, h = vp.img.rows; i < pages; i++) {
        cv::Mat tmp_img = images[i];
        std::cout << tmp_img.rows << std::endl;
        cv::Rect roi = cv::Rect(0, h, long_image.cols, tmp_img.rows);
        tmp_img.copyTo(long_image(roi));
        h += tmp_img.rows;
    }
    // cv::imwrite("../longboy.jpg", long_image);

    return long_image;
}

// returns dpi that will scale the pdf page to fit viewport width
double get_scaled_dpi(poppler::page *page, Viewport &vp) {
    double dpi = 0.0;
    auto rect = page->page_rect(poppler::media_box);

    dpi = ((double)vp.get_width() * 72.0) / rect.width();

    return dpi;
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

string get_pdf_name(string pdf_path) {
    return pdf_path.substr(pdf_path.find_last_of("/") + 1, pdf_path.length() - 7);
}

// returns number string with 6 padding.
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
