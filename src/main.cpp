#include "poppler.hpp"
#include "opencv.hpp"
#include "ptv.hpp"
#include <cctype>
#include <filesystem>
#include <libavformat/avformat.h>
#include <opencv2/imgcodecs.hpp>
#include "cpp/poppler-rectangle.h"
#include "poppler.hpp"
#include "opencv.hpp"
#include "ptv.hpp"
#include <algorithm>
#include <cstddef>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <iostream>
#include <opencv2/videoio.hpp>
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

// ===================================================
//
// =====================================================
void scale_image_to_width(cv::Mat &img, int dst_width);
void scale_image_to_fit(cv::Mat& img, struct VP &vp);

double get_scaled_dpi_from_width(poppler::page *page, int width); // dpi fits page to vp width
double get_scaled_dpi(poppler::page *page, struct VP &vp); // dpi fits page in viewport

cv::Mat get_long_image(vector<cv::Mat> &imgs, struct VP &vp);
std::map<int, string> get_image_sequence_map(vector<string> dirs);
vector<cv::Mat> get_images(std::map<int, string> img_map, Style style, struct VP &vp);
vector<cv::Mat> get_images(vector<string> pdf_paths, Style style, struct VP &vp);

string format_path(string str);

void generate_video(string frames_dir, string output, struct VP &vp);
void generate_scroll_frames(cv::VideoWriter &vid, int pages, cv::Mat &long_image, struct VP &vp);
void generate_sequence_frames(cv::VideoWriter &vid, vector<cv::Mat> &imgs, struct VP &vp);


void test();
// ===================================================
//
// =====================================================


int main(int argc, char **argv) {
    Style style = Style::SEQUENCE;
    bool is_pdf = false;
    bool is_seq = false;
    int width = 1280;
    int height = 720;
    double fps = 1.0;
    double spp = 1.0;
    string output = "";
    vector<string> pdf_paths = {};
    vector<string> seq_dirs = {};

    if (argc < 2) {
        std::cerr << HELP_TXT << std::endl;
        return 1;
    }

    // CLI
    for (int i = 1; i < argc; i++) {
        string arg = string(argv[i]);
        if (arg == "-h" || arg == "--help") {
            std::cout << HELP_TXT << std::endl;
            return 0;
        } else if ((int)arg.find(".pdf") > -1) {
            if (is_seq) {
                std::cerr << "<!> Error: Cannot convert Image Sequence and PDF at the same time." << std::endl;
                return 1;
            } else {
                is_pdf = true;
            }
            if (!fs::exists(arg)) {
                std::cerr << "<!> Error: '" << arg << "' does not exist." << std::endl;
                return 1;
            }
            if (arg.substr(arg.length() - 4, 4) != ".pdf") {
                std::cout << "<!> Error: '" << arg << "' is not a PDF file." << std::endl;
                return 1;
            }
            pdf_paths.push_back(arg);
        } else if ((int)arg.find('/') > -1) {
            if (is_pdf) {
                std::cerr << "<!> Error: Cannot convert PDF and Image Sequence at the same time." << std::endl;
                return 1;
            } else {
                is_seq = true;
            }
            if (!fs::is_directory(arg)) {
                std::cerr << "<!> Error: '" << arg <<  "' is not a valid directory." << std::endl;
                return 1;
            }
            if (arg[arg.length() - 1] != '/') {
                arg.push_back('/');
            }
            seq_dirs.push_back(arg);
        } else if (arg == "-r") {
            i++;
            width = std::stoi(argv[i]);
            i++;
            height = std::stoi(argv[i]);
        } else if (arg == "-f") {
            i++;
            fps = std::stof(argv[i]);
        } else if (arg == "-s") {
            i++;
            spp = std::stof(argv[i]);
        } else if (arg == "-o") {
            i++;
            arg = argv[i];
            if ((int)arg.find(".mkv") == -1) {
                std::cerr << "<!> Error: output file extension must be .mkv" << std::endl;
                return 1;
            }
            if ((int)arg.find('/') == -1) {
                output = arg;
            } else {
                string dir = arg.substr(0, arg.find_last_of('/') + 1);
                if (!fs::exists(dir)) {
                    std::cout << "<!> Error: Output directory does not exists." << std::endl;
                    return 1;
                }
            }
            output = arg;
        } else if (arg == "--scroll") {
            style = Style::SCROLL;
        } else {
            std::cerr << "<!> Error: unknown argument detected: " << argv[i] << std::endl;
            return 1;
        }
    }

    // Info presented before continuing.
    if (is_pdf) {
        std::cout << "PDF Paths (" << pdf_paths.size() << "): ";
        for (size_t i = 0; i < pdf_paths.size(); i++) {
            if (i > 0) {
                std::cout << " + ";
            }
            std::cout << "" + pdf_paths[i];
        }
        std::cout << std::endl;

        if (output == "") {
            string path = pdf_paths[0];
            output = path.substr(0, path.find_last_of('.')) + ".mkv";
        }
    }
    if (is_seq) {
        std::cout << "Sequence Directories (" << seq_dirs.size() << "): ";
        for (size_t i = 0; i < seq_dirs.size(); i++) {
            if (i > 0) {
                std::cout << " + ";
            }
            std::cout << "" + seq_dirs[i];
        }
        std::cout << std::endl;

        if (output == "") {
            string path = seq_dirs[0];
            output = path.substr(0, path.find_last_of('/')) + ".mkv";
        }
    }
    std::cout << "Output: " << output << std::endl;
    std::cout << "Res: " << width << "x" << height << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "SPP: " << spp << std::endl;
    if (style == Style::SCROLL) {
        std::cout << "Style: SCROLL" << std::endl;
    } else if (style == Style::SEQUENCE) {
        std::cout << "Style: SEQUENCE" << std::endl;
    }

    // Continuing logic
    string check;
    std::cout << "Are these values correct: [Y/n]" << std::endl;
    std::getline(std::cin, check);
    if (!check.empty() && check != "Y" && check != "y") {
        return 1;
    }
    std::cin.clear();

    // creates the vp
    struct VP vp = {
        width,
        height,
        fps,
        spp,
    };

    // Application Logic
    vector<cv::Mat> images = {};
    if (is_pdf) {
        poppler::document *pdf = poppler::document::load_from_file(pdf_paths[0]);

        // sets viewport's resolution to full res of first pdf page
        poppler::rectf rect = pdf->create_page(0)->page_rect(poppler::media_box);
        if (width == -1) {
            vp.width = rect.width();
        }
        if (height == -1) {
            vp.height = rect.height();
        }

        images = get_images(pdf_paths, style, vp);

        for (size_t i = 0; i < images.size(); i++) {
            cv::imwrite("../test/fuk/" + std::to_string(i) + ".jpg", images[i]);
        }
    }
    else if (is_seq) {
        std::map<int, string> img_map = get_image_sequence_map(seq_dirs);

        // sets viewport res to resolution of img
        cv::Mat img = cv::imread(img_map[-1]);
        if (width == -1) {
            vp.width = img.cols % 2 == 0 ? img.cols : img.cols + 1;
        }
        if (height == -1) {
            vp.height = img.rows % 2 == 0 ? img.rows : img.rows + 1;
        }
        img_map.erase(-1);

        images = get_images(img_map, style, vp);
    }

    // inits video renderer
    cv::Size frame_size(vp.width, vp.height);
    cv::VideoWriter video(output, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('H', '2', '6', '4'), vp.fps, frame_size, true);

    // generates frames
    if (style == Style::SCROLL) {
        cv::Mat long_img = get_long_image(images, vp);
        generate_scroll_frames(video, images.size(), long_img, vp);
    } else if (style == Style::SEQUENCE) {
        generate_sequence_frames(video, images, vp);
    }

    video.release();
    for (auto &img : images) {
        img.release();
    }

    std::cout << "Finished rendering video." << std::endl;

    return 0;
}

void scale_image_to_width(cv::Mat &img, int dst_width) {
    double scale = (double)dst_width / (double)img.cols;
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image_to_fit(cv::Mat &img, struct VP &vp) {
    double scale_w;
    double scale_h;
    double scale = 1.0;
    if (img.cols > vp.width && img.cols > vp.height) {
        scale_w = (double)vp.width / (double)img.cols;
        scale_h = (double)vp.height / (double)img.rows;
        scale = std::min(scale_w, scale_h);
    }
    if (img.cols < vp.width && img.rows < vp.height) {
        scale_w = (double)vp.width / (double)img.cols;
        scale_h = (double)vp.height / (double)img.rows;
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
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
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

void generate_scroll_frames(cv::VideoWriter &vid, int pages, cv::Mat &long_image, struct VP &vp) {
    std::cout << "Generating video frames..." << std::endl;

    int pixels_translated = (double)long_image.rows / (vp.fps * vp.spp * pages);
    if (pixels_translated == 0) {
        pixels_translated = 1;
    }
    int height = long_image.rows - vp.height; // - vp.rows prevents out of bounds error with cv::Rect roi
    cv::Mat vp_img(vp.height, vp.width, long_image.type());

    std::cout << "Pixels per frame: " << pixels_translated << std::endl;

    for (int h = 0, i = 0; h < height; h += pixels_translated, i++) {
        cv::Rect roi = cv::Rect(0, h, vp.width, vp.height);
        long_image(roi).copyTo(vp_img);
        vid.write(vp_img);
    }
}

void generate_sequence_frames(cv::VideoWriter &vid, vector<cv::Mat> &imgs, struct VP &vp) {
    std::cout << "Generating video frames..." << std::endl;

    for (size_t i = 0; i < imgs.size(); i ++) {
        cv::Mat img = imgs[i];
        cv::Mat vp_img = cv::Mat(vp.height, vp.width, img.type(), cv::Scalar(0, 0, 0));
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

std::map<int, string> get_image_sequence_map(vector<string> dirs) {
    int count = 0;
    int small = 99;
    std::map<int, string> image_map;

    for (size_t i = 0; i < dirs.size(); i++) {
        // creates hash-map of valid image paths in numerical order
        for (const auto &entry : fs::directory_iterator(dirs[i])) {
            if (!fs::is_directory(entry)) {
                int index;
                string path = entry.path().string();
                string name = entry.path().filename().string();
                try {
                    index = std::stoi(name.substr(0, name.length() - name.find_last_of('.'))) + count;
                } catch (const std::invalid_argument& e) {
                    std::cerr << "<!> Warning: " << name << " contains no int. Skipped." << std::endl;
                    continue;
                } catch (const std::out_of_range& e) {
                    std::cerr << "<!> Out of Range: geting int value from image name, get_images()." << std::endl;
                    continue;
                }
                if (index < small && count == 0) {
                    small = index;
                }
                image_map.insert(std::make_pair(index, path));
            }
        }
        count += image_map.size();
    }

    // used to define vp res when -r of -1 inputed as value
    image_map.insert(std::make_pair(-1, image_map[small]));
    return image_map;
}

vector<cv::Mat> get_images(std::map<int, string> img_map, Style style, struct VP &vp) {
    std::cout << "Loading images..." << std::endl;
    vector<cv::Mat> images;

    // reads images in numerical order
    for (size_t i = 0; i < img_map.size(); i++) {
        if (img_map[i] == "") continue;
        cv::Mat mat = cv::imread(img_map[i]);

        if (style == Style::SCROLL) {
            scale_image_to_width(mat, vp.width);
        } else if (style == Style::SEQUENCE) {
            scale_image_to_fit(mat, vp);
        }

        // makes dimentions of the image divisible by 2.
        // ffmpeg will get upset if otherwise.
        int rows = mat.rows % 2 != 0 ? mat.rows + 1 : mat.rows;
        int cols = mat.cols % 2 != 0 ? mat.cols + 1 : mat.cols;
        cv::Rect2i roi(0, 0, mat.cols, mat.rows);
        cv::Mat tmp_mat(rows, cols, CV_8UC3);
        mat.copyTo(tmp_mat(roi));
        images.push_back(cv::Mat(tmp_mat));
    }
    return images;
}

vector<cv::Mat> get_images(vector<string> pdf_paths, Style style, VP &vp) {
    std::cout << "Loading Images..." << std::endl;

    int total_pages = 0;
    auto renderer = poppler::page_renderer();
    vector<cv::Mat> images = {};

    for (size_t i = 0; i < pdf_paths.size(); i++) {
        int curr_index = total_pages;
        poppler::document *pdf = poppler::document::load_from_file(pdf_paths[i]);
        total_pages += pdf->pages();

        // required to copy pdf data into vector
        for (int j = 0; j < pdf->pages(); j++) {
            images.emplace_back(cv::Mat(100, 100, CV_8UC3, cv::Scalar(100, 210, 70)));
        }

        for (int pg = 0; curr_index < total_pages; curr_index++, pg++) {
            double dpi = DEFAULT_DPI;
            poppler::page *page = pdf->create_page(pg);

            // scales pages to correctly fit inside displayport.
            if (style == Style::SCROLL) {
                dpi = get_scaled_dpi_from_width(page, vp.width);
            } else if (style == Style::SEQUENCE) {
                dpi = get_scaled_dpi(page, vp);
            }

            poppler::image img = renderer.render_page(page, dpi, dpi);
            cv::Mat mat;

            // Determine the format
            if (img.data() == nullptr) {
                std::cerr << "<!> Error: Page " << pg << " has no data to load. Skipped." << std::endl;
                images.erase(images.begin() + curr_index);
                continue;
            } else if (img.format() == poppler::image::format_invalid) {
                std::cerr << "<!> Error: Page " << pg << " has invalid image format. Skipped." << std::endl;
                images.erase(images.begin() + curr_index);
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

            // makes sure dimentions are even
            // ffmpeg will get upset if odd
            if (mat.rows % 2 != 0 || mat.cols % 2 != 0) {
                int rows = mat.rows % 2 != 0 ? mat.rows + 1 : mat.rows;
                int cols = mat.cols % 2 != 0 ? mat.cols + 1 : mat.cols;
                cv::Rect2i roi(0, 0, mat.cols, mat.rows);
                cv::Mat tmp_mat(rows, cols, CV_8UC3);

                mat.copyTo(tmp_mat(roi));
                tmp_mat.copyTo(images[curr_index]);
            } else {
                mat.copyTo(images[curr_index]);
            }
        }
    }
    return images;
}

cv::Mat get_long_image(vector<cv::Mat> &imgs, struct VP &vp) {
    int height = 0;
    int type = imgs[0].type();
    cv::Mat long_image;

    // stores exported pdf pages as images in vector.
    for (size_t i = 0; i < imgs.size(); i++) {
        height += imgs[i].rows;
    }
    // adds white space before and after content.
    height += vp.height * 2;

    // images that will contain all contents of pdf (like a long comic strip)
    long_image = cv::Mat(height, vp.width, type);

    // sequentually adds all pages of pdf in order to long_image.
    // ROI -> Region of Interest
    for (size_t i = 0, h = vp.height; i < imgs.size(); i++) {
        int r = imgs[i].rows;
        cv::Rect roi = cv::Rect(0, h, long_image.cols, r);
        imgs[i].copyTo(long_image(roi));
        h += r;
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

void test() {
    vector<cv::Mat> mats = {};

    string dir = "../test/test_seq/";
    if (fs::is_directory(dir)) {
        for (const auto &entry : fs::directory_iterator(dir)) {
            if (!fs::is_directory(entry)) {
                cv::Mat mat = cv::imread(entry.path().string());
                mats.push_back(mat);
            }
        }
    }

    cv::Size frame_size = mats[0].size();

    for (auto &m: mats) {
        if (m.size() != frame_size) {
            std::cerr << "bad frame size" << std::endl;
        }
    }

    cv::VideoWriter video("../test/out1.mkv", cv::VideoWriter::fourcc('H', '2', '6', '4'), 1, frame_size, true);

    for (const cv::Mat &m : mats) {
        video.write(m);
    }

    video.release();
    for (auto &m: mats) {
        m.release();
    }
}
