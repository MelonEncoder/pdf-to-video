#include "opencv.hpp"
#include "poppler.hpp"
#include "ptv.hpp"
#include <cmath>
#include <filesystem>
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
void scale_image_to_fit(cv::Mat& img, struct VP &vp);

float get_scaled_dpi_from_width(poppler::page *page, int width); // dpi fits page to vp width
float get_scaled_dpi_to_fit(poppler::page *page, struct VP &vp); // dpi fits page in viewport

std::map<int, string> get_image_sequence_map(vector<string> seq_dirs);
vector<cv::Mat> get_images(std::map<int, string> img_map, Style style, struct VP &vp);
vector<cv::Mat> get_images(vector<string> pdf_paths, Style style, struct VP &vp);

void generate_scroll_video(cv::VideoWriter &vid, vector<cv::Mat> &imgs, struct VP &vp);
void generate_sequence_video(cv::VideoWriter &vid, vector<cv::Mat> &imgs, struct VP &vp);

// ============= //
// Main Function //
// ============= //

int main(int argc, char **argv) {
    Style style = Style::SEQUENCE;
    bool is_pdf = false;
    bool is_seq = false;
    string output = "";
    string format = ".mp4";
    vector<string> pdf_paths = {};
    vector<string> seq_dirs = {};
    struct VP vp = {
        1280.0f, // width
        720.0f, // height
        1.0f, // fps
        1.0f, // spp
        -1.0f, // duration
    };

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
        }
        else if ((int)arg.find(".pdf") > -1) {
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
        }
        else if ((int)arg.find('/') > -1) {
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
        }
        else if (arg == "-r") {
            i++;
            vp.width = std::stoi(argv[i]);
            i++;
            vp.height = std::stoi(argv[i]);
        }
        else if (arg == "-f") {
            i++;
            vp.fps = std::stof(argv[i]);
        }
        else if (arg == "-s") {
            i++;
            vp.spp = std::stof(argv[i]);
        }
        else if (arg == "-d") {
            i++;
            vp.duration = std::stof(argv[i]);
        }
        else if (arg == "-o") {
            i++;
            arg = argv[i];
            if ((int)arg.find(format) == -1) {
                std::cerr << "<!> Error: output file extension must be " + format << std::endl;
                return 1;
            }
            if ((int)arg.find('/') == -1) {
                output = arg;
            }
            else {
                string dir = arg.substr(0, arg.find_last_of('/') + 1);
                if (!fs::exists(dir)) {
                    std::cout << "<!> Error: Output directory does not exists." << std::endl;
                    return 1;
                }
            }
            output = arg;
        }
        else if (arg == "--scroll") {
            style = Style::SCROLL;
        }
        else {
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
    }
    else if (is_seq) {
        std::cout << "Sequence Directories (" << seq_dirs.size() << "): ";
        for (size_t i = 0; i < seq_dirs.size(); i++) {
            if (i > 0) {
                std::cout << " + ";
            }
            std::cout << "" + seq_dirs[i];
        }
    }
    std::cout << std::endl;
    if (output == "" && is_seq) {
        string path = seq_dirs[0];
        output = path.substr(0, path.find_last_of('/')) + format;
    }
    else if (output == "" && is_pdf) {
        string path = pdf_paths[0];
        output = path.substr(0, path.find_last_of('.')) + format;
    }
    std::cout << "Output: " << output << std::endl;
    std::cout << "Resolution: " << vp.width << "x" << vp.height << std::endl;
    std::cout << "FPS: " << vp.fps << std::endl;
    if (vp.duration != -1.0 && style == Style::SCROLL) {
        std::cout << "Duration: " << vp.duration << "s" << std::endl;
    }
    else if (style == Style::SCROLL) {
        std::cout << "SPP: " << vp.spp << "s" << std::endl;
    }
    if (style == Style::SCROLL) {
        std::cout << "Style: SCROLL" << std::endl;
    }
    else if (style == Style::SEQUENCE) {
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

    // loads images into vector
    vector<cv::Mat> images = {};
    if (is_pdf) {
        poppler::document *pdf = poppler::document::load_from_file(pdf_paths[0]);
        poppler::rectf rect = pdf->create_page(0)->page_rect(poppler::media_box);
        // sets viewport's resolution to the full resolution of the first pdf page
        if (vp.width == -1)
            vp.width = rect.width();
        if (vp.height == -1)
            vp.height = rect.height();
        images = get_images(pdf_paths, style, vp);
    }
    else if (is_seq) {
        std::map<int, string> img_map = get_image_sequence_map(seq_dirs);
        cv::Mat img = cv::imread(img_map[-1]);
        // Sets viewport's resolution to the full resolution of the first image in the sequence.
        if (vp.width == -1)
            vp.width = img.cols % 2 == 0 ? img.cols : img.cols + 1;
        if (vp.height == -1)
            vp.height = img.rows % 2 == 0 ? img.rows : img.rows + 1;
        img_map.erase(-1);
        images = get_images(img_map, style, vp);
    }

    // inits video renderer
    cv::Size frame_size(vp.width, vp.height);
    cv::VideoWriter video = cv::VideoWriter(output, cv::CAP_FFMPEG, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), vp.fps, frame_size, true);

    // generates frames
    if (style == Style::SCROLL) {
        generate_scroll_video(video, images, vp);
    } else if (style == Style::SEQUENCE) {
        generate_sequence_video(video, images, vp);
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

void scale_image_to_fit(cv::Mat &img, struct VP &vp) {
    float scale_w;
    float scale_h;
    float scale = 1.0;
    if (img.cols > vp.width && img.cols > vp.height) {
        scale_w = (float)vp.width / (float)img.cols;
        scale_h = (float)vp.height / (float)img.rows;
        scale = std::min(scale_w, scale_h);
    }
    if (img.cols < vp.width && img.rows < vp.height) {
        scale_w = (float)vp.width / (float)img.cols;
        scale_h = (float)vp.height / (float)img.rows;
        scale = std::min(scale_w, scale_h);
    }
    if (img.cols > vp.width && img.rows <= vp.height) {
        scale_w = (float)vp.width / (float)img.cols;
        scale = scale_w;
    }
    if (img.cols <= vp.width && img.rows > vp.height) {
        scale_h = (float)vp.height / (float)img.rows;
        scale = scale_h;
    }
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

// returns dpi to scale page to viewport width
float get_scaled_dpi_from_width(poppler::page *page, int width) {
    auto rect = page->page_rect(poppler::media_box);
    if (rect.width() == width)
        return DEFAULT_DPI;
    return ((float)width * DEFAULT_DPI) / (float)rect.width();
}

// returns dpi that will scale the pdf page to fit the viewport dimentions
float get_scaled_dpi_to_fit(poppler::page *page, struct VP &vp) {
    float dpi_w;
    float dpi_h;
    poppler::rectf rect = page->page_rect(poppler::media_box);
    if (rect.width() > vp.width && rect.height() > vp.height) {
        dpi_w = ((float)vp.width * DEFAULT_DPI) / rect.width();
        dpi_h = ((float)vp.height * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    }
    if (rect.width() < vp.width && rect.height() < vp.height) {
        dpi_w = ((float)vp.width * DEFAULT_DPI) / rect.width();
        dpi_h = ((float)vp.height * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    }
    if (rect.width() > vp.width && rect.height() <= vp.height) {
        dpi_w = ((float)vp.width * DEFAULT_DPI) / rect.width();
        return dpi_w;
    }
    if (rect.width() <= vp.width && rect.height() > vp.height) {
        dpi_h = ((float)vp.height * DEFAULT_DPI) / rect.height();
        return dpi_h;
    }
    return DEFAULT_DPI;
}

// returns ordered image paths
std::map<int, string> get_image_sequence_map(vector<string> seq_dirs) {
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

// returns images read from image sequence directory
vector<cv::Mat> get_images(std::map<int, string> img_map, Style style, struct VP &vp) {
    std::cout << "Loading images..." << std::endl;
    vector<cv::Mat> images;

    // reads images in numerical order
    for (size_t i = 0; i < img_map.size(); i++) {
        if (img_map[i] == "")
            continue;
        cv::Mat mat = cv::imread(img_map[i]);
        if (style == Style::SCROLL)
            scale_image_to_width(mat, vp.width);
        else if (style == Style::SEQUENCE)
            scale_image_to_fit(mat, vp);

        // makes dimentions of the image divisible by 2.
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
vector<cv::Mat> get_images(vector<string> pdf_paths, Style style, VP &vp) {
    int total_pages = 0;
    auto renderer = poppler::page_renderer();
    vector<cv::Mat> images = {};

    std::cout << "Loading Images..." << std::endl;
    for (size_t i = 0; i < pdf_paths.size(); i++) {
        int curr_index = total_pages;
        poppler::document *pdf = poppler::document::load_from_file(pdf_paths[i]);
        total_pages += pdf->pages();

        // required to copy pdf data into vector
        for (int j = 0; j < pdf->pages(); j++) {
            images.emplace_back(cv::Mat(100, 100, CV_8UC3, cv::Scalar(0, 0, 0)));
        }

        // gets pages of individual pdf files
        for (int pg = 0; curr_index < total_pages; curr_index++, pg++) {
            float dpi = DEFAULT_DPI;
            poppler::page *page = pdf->create_page(pg);

            // scales pages to correctly fit inside displayport.
            if (style == Style::SCROLL)
                dpi = get_scaled_dpi_from_width(page, vp.width);
            else if (style == Style::SEQUENCE)
                dpi = get_scaled_dpi_to_fit(page, vp);

            poppler::image img = renderer.render_page(page, dpi, dpi);
            cv::Mat mat;
            // Determine the format
            if (img.data() == nullptr) {
                std::cerr << "<!> Error: Page " << pg << " has no data to load. Skipped." << std::endl;
                images.erase(images.begin() + curr_index);
                continue;
            }
            else if (img.format() == poppler::image::format_invalid) {
                std::cerr << "<!> Error: Page " << pg << " has invalid image format. Skipped." << std::endl;
                images.erase(images.begin() + curr_index);
                continue;
            }
            else if (img.format() == poppler::image::format_gray8) {
                cv::Mat tmp = cv::Mat(img.height(), img.width(), CV_8UC1, img.data(), img.bytes_per_row());
                cv::cvtColor(tmp, mat, cv::COLOR_GRAY2RGB);
            }
            else if (img.format() == poppler::image::format_rgb24) {
                mat = cv::Mat(img.height(), img.width(), CV_8UC3, img.data(), img.bytes_per_row());
            }
            else if (img.format() == poppler::image::format_bgr24) {
                cv::Mat tmp = cv::Mat(img.height(), img.width(), CV_8UC3, img.data(), img.bytes_per_row());
                cv::cvtColor(tmp, mat, cv::COLOR_BGR2RGB);
            }
            else if (img.format() == poppler::image::format_argb32) {
                cv::Mat tmp = cv::Mat(img.height(), img.width(), CV_8UC4, img.data(), img.bytes_per_row());
                cv::cvtColor(tmp, mat, cv::COLOR_RGBA2RGB);
            }

            // makes sure dimentions are even
            // ffmpeg will get upset if odd
            if (mat.rows % 2 != 0 || mat.cols % 2 != 0) {
                int rows = mat.rows % 2 != 0 ? mat.rows + 1 : mat.rows;
                int cols = mat.cols % 2 != 0 ? mat.cols + 1 : mat.cols;
                cv::Rect2i roi(0, 0, mat.cols, mat.rows);
                cv::Mat tmp_mat(rows, cols, CV_8UC3, cv::Scalar(0, 0, 0));
                mat.copyTo(tmp_mat(roi));
                tmp_mat.copyTo(images[curr_index]);
            }
            else {
                mat.copyTo(images[curr_index]);
            }
        }
    }
    return images;
}

// scroll effect
void generate_scroll_video(cv::VideoWriter &vid, std::vector<cv::Mat> &imgs, struct VP &vp) {
    float px_per_frame = 0;
    float h = 0;
    cv::Mat long_img(vp.height + imgs[0].rows, vp.width, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat vp_img(vp.height, vp.width, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Rect2d roi(0, vp.height, imgs[0].cols, imgs[0].rows);
    imgs[0].copyTo(long_img(roi));

    int height = 0;
    for (size_t i = 0; i < imgs.size(); i++) {
        height += imgs[i].rows;
    }
    if (vp.duration != -1.0)
        px_per_frame = (float)height / (vp.fps * vp.duration);
    else
        px_per_frame = (float)height / (vp.fps * vp.spp * imgs.size());
    if (px_per_frame <= 0.0) {
        px_per_frame = 1.0;
        std::cout << "<!> Warning: pixels per frame <= 0.0, set value to 1.0" << std::endl;
    }
    std::cout << "Pixels per frame: " << px_per_frame << std::endl;
    std::cout << "Generating video..." << std::endl;
    for (size_t i = 1; i < imgs.size(); i++) {
        std::cout << i << "/" << imgs.size() << std::endl;
        // generates and writes frames to video file
            while (((float)long_img.rows - (h + vp.height)) > px_per_frame) {
                cv::Rect2d roi(0.0, h, vp.width, vp.height);
                long_img(roi).copyTo(vp_img);
                vid.write(vp_img);
                h += px_per_frame;
            }
        // logic to readjust the translation of video frames
        float unused_height = long_img.rows - (h + vp.height); // height not yet rendered in vp.
        float new_long_h = vp.height + unused_height + imgs[i].rows;
        cv::Mat new_long;
        if (imgs.size() - 2 == i) // allows video to scroll to black at end
            new_long = cv::Mat(std::ceil(new_long_h) + vp.height + px_per_frame, vp.width, CV_8UC3, cv::Scalar(0, 0, 0));
        else
            new_long = cv::Mat(std::ceil(new_long_h), vp.width, CV_8UC3, cv::Scalar(0, 0, 0));
        cv::Rect2d tmp_ROI(0.0, h, vp.width, unused_height + vp.height);
        cv::Rect2d dst_ROI(0.0, 0.0, vp.width, unused_height + vp.height);
        long_img(tmp_ROI).copyTo(new_long(dst_ROI));
        cv::Rect2d next_ROI(0.0, unused_height + vp.height, vp.width, imgs[i].rows);
        imgs[i].copyTo(new_long(next_ROI));
        long_img = new_long;
        h = 0.0;
    }
    std::cout << imgs.size() << "/" << imgs.size() << std::endl;
}

// classic image sequence effect
void generate_sequence_video(cv::VideoWriter &vid, vector<cv::Mat> &imgs, struct VP &vp) {
    std::cout << "Generating video..." << std::endl;
    for (size_t i = 0; i < imgs.size(); i ++) {
        cv::Mat img = imgs[i];
        cv::Mat vp_img = cv::Mat(vp.height, vp.width, img.type(), cv::Scalar(0, 0, 0));
        int x = 0;
        int y = 0;

        // adds offset
        if (vp_img.cols - img.cols >= 2)
            x += (vp_img.cols - img.cols) / 2;
        else if (vp_img.rows - img.rows >= 2)
            y += (vp_img.rows - img.rows) / 2;

        // prevents stretching of images when being rendered.
        // keeps them within the vp.
        cv::Rect2i roi(x, y, img.cols, img.rows);
        img.copyTo(vp_img(roi));
        vid.write(vp_img);
    }
}
