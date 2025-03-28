#include "opencv.hpp"
#include "poppler-global.h"
#include "poppler.hpp"
#include "ptv.hpp"
#include <cmath>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>

using std::string;
using std::vector;
namespace fs = std::filesystem;

// ======= //
// Headers //
// ======= //

void scale_image_to_width(cv::Mat &img, int dst_width);
void scale_image_to_fit(cv::Mat& img, ptv::Config &conf);

float get_scaled_dpi_from_width(poppler::page *page, int width); // dpi fits page to vp width
float get_scaled_dpi_to_fit(poppler::page *page, ptv::Config &conf); // dpi fits page in viewport

std::map<int, string> get_image_seq_map(vector<string> seq_dirs);
vector<cv::Mat> get_seq_images(std::map<int, string> &img_map, ptv::Config &conf);
vector<cv::Mat> get_pdf_images(ptv::Config &conf);

void generate_scroll_video(cv::VideoWriter &vid, vector<cv::Mat> &imgs, ptv::Config &conf);
void generate_sequence_video(cv::VideoWriter &vid, vector<cv::Mat> &imgs, ptv::Config &conf);

// ============= //
// Main Function //
// ============= //

int main(int argc, char **argv) {
    ptv::Config conf(argc, argv);

    // loads images into vector
    vector<cv::Mat> images = {};
    if (conf.get_is_pdf()) {
        images = get_pdf_images(conf);
    }
    else if (conf.get_is_seq()) {
        std::map<int, string> img_map = get_image_seq_map(conf.get_seq_dirs());
        cv::Mat img = cv::imread(img_map[-1]);
        // Sets viewport's resolution to the full resolution of the first image in the sequence.
        if (conf.get_width() == 0) {
            conf.set_width(img.cols % 2 == 0 ? img.cols : img.cols + 1);
        }
        if (conf.get_height() == 0) {
            conf.set_height(img.rows % 2 == 0 ? img.rows : img.rows + 1);
        }
        img_map.erase(-1);
        images = get_seq_images(img_map, conf);
    }

    // inits video renderer
    cv::Size frame_size(conf.get_width(), conf.get_height());
    cv::VideoWriter video = cv::VideoWriter(conf.get_output(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), conf.get_fps(), frame_size, true);

    // generates frames
    if (conf.get_style() == FRAMES) {
        generate_sequence_video(video, images, conf);
    } else {
        generate_scroll_video(video, images, conf);
    }

    // clean up
    video.release();
    for (auto &img : images) {
        img.release();
    }
    std::cout << "Finished generating video." << std::endl;
    return 0;
}

// ========= //
// Functions //
// ========= //

void scale_image_to_width(cv::Mat &img, int dst_width) {
    float scale = (float)dst_width / (float)img.cols;
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image_to_fit(cv::Mat &img, ptv::Config &conf) {
    float scale_w;
    float scale_h;
    float scale = 1.0;
    if (img.cols > conf.get_width() && img.cols > conf.get_height()) {
        scale_w = (float)conf.get_width() / (float)img.cols;
        scale_h = (float)conf.get_height() / (float)img.rows;
        scale = std::min(scale_w, scale_h);
    }
    if (img.cols < conf.get_width() && img.rows < conf.get_height()) {
        scale_w = (float)conf.get_width() / (float)img.cols;
        scale_h = (float)conf.get_height() / (float)img.rows;
        scale = std::min(scale_w, scale_h);
    }
    if (img.cols > conf.get_width() && img.rows <= conf.get_height()) {
        scale_w = (float)conf.get_width() / (float)img.cols;
        scale = scale_w;
    }
    if (img.cols <= conf.get_width() && img.rows > conf.get_height()) {
        scale_h = (float)conf.get_height() / (float)img.rows;
        scale = scale_h;
    }
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

// returns dpi to scale page to viewport width
float get_scaled_dpi_from_width(poppler::page *page, int width) {
    auto rect = page->page_rect(poppler::media_box);
    if (rect.width() == width) {
        return DEFAULT_DPI;
    }
    return ((float)width * DEFAULT_DPI) / (float)rect.width();
}

// returns dpi that will scale the pdf page to fit the viewport dimentions
float get_scaled_dpi_to_fit(poppler::page *page, ptv::Config &conf) {
    float dpi_w;
    float dpi_h;
    poppler::rectf rect = page->page_rect(poppler::media_box);
    if (rect.width() > conf.get_width() && rect.height() > conf.get_height()) {
        dpi_w = ((float)conf.get_width() * DEFAULT_DPI) / rect.width();
        dpi_h = ((float)conf.get_height() * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    }
    if (rect.width() < conf.get_width() && rect.height() < conf.get_height()) {
        dpi_w = ((float)conf.get_width() * DEFAULT_DPI) / rect.width();
        dpi_h = ((float)conf.get_height() * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    }
    if (rect.width() > conf.get_width() && rect.height() <= conf.get_height()) {
        dpi_w = ((float)conf.get_width() * DEFAULT_DPI) / rect.width();
        return dpi_w;
    }
    if (rect.width() <= conf.get_width() && rect.height() > conf.get_height()) {
        dpi_h = ((float)conf.get_height() * DEFAULT_DPI) / rect.height();
        return dpi_h;
    }
    return DEFAULT_DPI;
}

// returns ordered image paths
std::map<int, string> get_image_seq_map(vector<string> seq_dirs) {
    int count = 0;
    int small = 99;
    std::map<int, string> image_map;

    for (size_t i = 0; i < seq_dirs.size(); i++) {
        // creates hash-map of valid image paths in numerical order
        for (const auto &entry : fs::directory_iterator(seq_dirs[i])) {
            if (!fs::is_directory(entry)) {
                int index;
                string path = entry.path().string();
                string name = entry.path().filename().string();
                try {
                    index = std::stoi(name.substr(0, name.length() - name.find_last_of('.'))) + count;
                } catch (const std::invalid_argument& e) {
                    std::cerr << "<!> Warning: " << name << " skipped. Number not found." << std::endl;
                    continue;
                } catch (const std::out_of_range& e) {
                    std::cerr << "<!> Out of Range: geting int value from image name, get_images()." << std::endl;
                    continue;
                }
                if (index < small) {
                    small = index;
                }
                image_map.insert(std::make_pair(index, path));
            }
        }
        count += image_map.size();
    }

    // used to define resolution when -r of 0 inputed as value
    image_map.insert(std::make_pair(-1, image_map[small]));
    return image_map;
}

// returns images read from image sequence directory
vector<cv::Mat> get_seq_images(std::map<int, string> &img_map, ptv::Config &conf) {
    std::cout << "Loading images..." << std::endl;
    vector<cv::Mat> images;

    // reads images in numerical order
    for (size_t i = 0; i < img_map.size(); i++) {
        if (img_map[i] == "") {
            continue;
        }
        cv::Mat mat = cv::imread(img_map[i]);
        if (conf.get_style() == FRAMES) {
            scale_image_to_fit(mat, conf);
        } else {
            scale_image_to_width(mat, conf.get_width());
        }

        // Makes dimentions of the image divisible by 2.
        // ffmpeg will get upset if otherwise.
        int rows = mat.rows % 2 != 0 ? mat.rows + 1 : mat.rows;
        int cols = mat.cols % 2 != 0 ? mat.cols + 1 : mat.cols;
        cv::Rect2i roi(0, 0, mat.cols, mat.rows);
        cv::Mat tmp_mat(rows, cols, CV_8UC3, cv::Scalar(0, 0, 0));
        mat.copyTo(tmp_mat(roi));
        images.push_back(cv::Mat(tmp_mat));
    }
    return images;
}

// returns images read from pdf files
vector<cv::Mat> get_pdf_images(ptv::Config &conf) {
    int total_pages = 0;
    auto renderer = poppler::page_renderer();
    vector<cv::Mat> images = {};

    std::cout << "Loading Images..." << std::endl;
    for (size_t i = 0; i < conf.get_pdf_paths().size(); i++) {
        poppler::document *pdf = poppler::document::load_from_file(conf.get_pdf_paths()[i]);
        int index = total_pages;
        total_pages += pdf->pages();

        // required to copy pdf data into vector
        for (int j = 0; j < pdf->pages(); j++) {
            images.emplace_back(cv::Mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0)));
        }

        // Gets pages of individual pdf files
        for (int pg = 0; index < total_pages; index++, pg++) {
            float dpi = DEFAULT_DPI;
            poppler::page *page = pdf->create_page(pg);

            // If -r 0x0, adjusts resolution to fit first page
            if (index == 0) {
                poppler::rectf rect = page->page_rect(poppler::media_box);
                if (conf.get_width() == 0) {
                    conf.set_width(((int)rect.width() % 2 == 0) ? rect.width() : rect.width() + 1);
                }
                if (conf.get_height() == 0) {
                    conf.set_height(((int)rect.height() % 2 == 0) ? rect.height() : rect.height() + 1);
                }
                std::cout << "Adjusted Resolution: Location get pdf imgs\n";
            }

            // Scales pages to correctly fit inside video resolution.
            if (conf.get_style()== FRAMES) {
                dpi = get_scaled_dpi_to_fit(page, conf);
            } else {
                dpi = get_scaled_dpi_from_width(page, conf.get_width());
            }

            poppler::image img = renderer.render_page(page, dpi, dpi);
            cv::Mat mat;
            // Determine the color space
            if (img.data() == nullptr) {
                std::cerr << "<!> Error: Page " << pg << " has no data to load. Skipped." << std::endl;
                images.erase(images.begin() + index);
                continue;
            } else if (img.format() == poppler::image::format_invalid) {
                std::cerr << "<!> Error: Page " << pg << " has invalid image format. Skipped." << std::endl;
                images.erase(images.begin() + index);
                continue;
            } else if (img.format() == poppler::image::format_gray8) {
                cv::Mat tmp = cv::Mat(img.height(), img.width(), CV_8UC1, img.data(), img.bytes_per_row());
                cv::cvtColor(tmp, mat, cv::COLOR_GRAY2RGB);
            } else if (img.format() == poppler::image::format_rgb24) {
                mat = cv::Mat(img.height(), img.width(), CV_8UC3, img.data(), img.bytes_per_row());
            } else if (img.format() == poppler::image::format_bgr24) {
                cv::Mat tmp = cv::Mat(img.height(), img.width(), CV_8UC3, img.data(), img.bytes_per_row());
                cv::cvtColor(tmp, mat, cv::COLOR_BGR2RGB);
            } else if (img.format() == poppler::image::format_argb32) {
                cv::Mat tmp = cv::Mat(img.height(), img.width(), CV_8UC4, img.data(), img.bytes_per_row());
                cv::cvtColor(tmp, mat, cv::COLOR_RGBA2RGB);
            }
            mat.copyTo(images[index]);
        }
    }
    return images;
}

// scroll effect
void generate_scroll_video(cv::VideoWriter &vid, std::vector<cv::Mat> &imgs, ptv::Config &conf) {
    float px_per_frame = 0.0f;
    float h = 0.0f;
    cv::Mat dst_img(conf.get_height() + imgs[0].rows, conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0)); // Black Box (Video dimentions) + First image
    cv::Mat vp_img(conf.get_height(), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Rect2d roi(0, conf.get_height(), imgs[0].cols, imgs[0].rows);
    imgs[0].copyTo(dst_img(roi));
    imgs[0].release();

    int height_of_imgs = 0;
    for (size_t i = 0; i < imgs.size(); i++) {
        height_of_imgs += imgs[i].size().height;
    }

    if (conf.get_duration() == 0) {
        px_per_frame += height_of_imgs / (conf.get_fps() * conf.get_spp() * imgs.size());
    } else {
        px_per_frame = height_of_imgs / (conf.get_fps() * conf.get_duration());
    }

    if (px_per_frame <= 0.0f) {
        px_per_frame = 1.0f;
        std::cout << "<!> Warning: pixels per frame value was <= 0.0, set value to 1.0" << std::endl;
    }
    std::cout << "Pixels per frame: " << px_per_frame << std::endl;
    std::cout << "Generating video..." << std::endl;

    for (size_t i = 1; i < imgs.size(); i++) {
        // Generates and writes frames to video file
        while (((float)dst_img.rows - (h + conf.get_height())) > px_per_frame) {
            cv::Rect2d roi(0.0f, h, conf.get_width(), conf.get_height());
            dst_img(roi).copyTo(vp_img);
            vid.write(vp_img);
            h += px_per_frame;
        }

        // Logic to readjust the translation of video frames
        float unused_height = dst_img.rows - (h + conf.get_height()); // height not yet rendered in vp.
        float new_dst_h = conf.get_height() + unused_height + imgs[i].rows;
        cv::Mat new_dst_img;
        if ((imgs.size() - 2) == i) { // allows video to scroll to black at end
            new_dst_img = cv::Mat(std::ceil(new_dst_h) + conf.get_height() + px_per_frame, conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0));
        } else {
            new_dst_img = cv::Mat(std::ceil(new_dst_h), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0));
        }
        cv::Rect2d tmp_ROI(0.0, h, conf.get_width(), unused_height + conf.get_height());
        cv::Rect2d dst_ROI(0.0, 0.0, conf.get_width(), unused_height + conf.get_height());
        dst_img(tmp_ROI).copyTo(new_dst_img(dst_ROI));
        cv::Rect2d next_ROI(0.0, unused_height + conf.get_height(), conf.get_width(), imgs[i].rows);
        imgs[i].copyTo(new_dst_img(next_ROI));
        dst_img = new_dst_img;
        h = 0.0;

        // Finished Rendering Current Image
        std::cout << i << "/" << imgs.size() << std::endl;
    }
    std::cout << imgs.size() << "/" << imgs.size() << std::endl;
}

// classic image sequence effect
void generate_sequence_video(cv::VideoWriter &vid, vector<cv::Mat> &imgs, ptv::Config &conf) {
    std::cout << "Generating video..." << std::endl;
    for (size_t i = 0; i < imgs.size(); i ++) {
        cv::Mat img = imgs[i];
        cv::Mat vp_img = cv::Mat(conf.get_height(), conf.get_width(), img.type(), cv::Scalar(0, 0, 0));
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
        vid.write(vp_img);
    }
}
