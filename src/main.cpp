#include "opencv.hpp"
#include "opencv2/imgcodecs.hpp"
#include "poppler.hpp"
#include "ptv.hpp"
#include <algorithm>

namespace fs = std::filesystem;

// ======= //
// Headers //
// ======= //

void scale_image_to_width(cv::Mat &img, const int dst_width);
void scale_image_to_height(cv::Mat &img, const int dst_height);
void scale_image_to_fit(cv::Mat& img, ptv::Config &conf);
void scale_image(cv::Mat &img, ptv::Config &conf);

double get_scaled_dpi_from_width(const poppler::page *page, const int width); // dpi fits page width to vp width
double get_scaled_dpi_from_height(const poppler::page *page, const int height); // dpi fits page height to vp height
double get_scaled_dpi_to_fit(poppler::page *page, ptv::Config &conf); // dpi fits entire page in viewport

std::vector<std::string> get_dir_img_paths(std::string dir_path);
void add_dir_images(const std::string dir_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf);
void add_pdf_images(const std::string pdf_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf);
void add_gif_images(const std::string gif_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf);

void generate_video_sequence(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf);

double get_pixels_per_frame(const std::vector<cv::Mat> &imgs, ptv::Config &conf);
void generate_video_scroll_up(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf);
// void generate_video_scroll_down(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf);
void generate_video_scroll_left(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf);
// void generate_video_scroll_right(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf);

// ============= //
// Main Function //
// ============= //

int main(int argc, char **argv) {
    ptv::Config conf(argc, argv);

    // Start timer after accepting settings
    time_t start_time = time(NULL);

    // == FIGURE OUT CONDITION FOR NATIVE RESOLUTION (-r 0x0) ==
    //
    // If -r 0x0, adjusts resolution to fit first page
    // if (pg == 0) {
    //     poppler::rectf rect = page->page_rect(poppler::media_box);
    //     conf.set_resolution(rect);
    // }
    // Sets video resolution to resolution of first image in the sequence.
    // cv::Mat img = cv::imread(img_map[-1]);
    // conf.set_resolution(img);
    // img_map.erase(-1);

    std::cout << "Loading Images..." << std::endl;
    std::vector<cv::Mat> images = {};
    std::vector<std::string> paths = conf.get_input_paths();
    std::vector<std::string> types = conf.get_input_types();
    for (size_t i = 0; i < paths.size(); i++) {
        if (types[i] == "pdf") {
            add_pdf_images(paths[i], images, conf);
        } else if (types[i] == "dir") {
            add_dir_images(paths[i], images, conf);
        }
    }

    std::cout << "Initializing Video Renderer..." << std::endl;
    cv::Size frame_size(conf.get_width(), conf.get_height());
    cv::VideoWriter video = cv::VideoWriter(conf.get_output(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), conf.get_fps(), frame_size, true);

    std::cout << "Generating Video..." << std::endl;
    if (conf.get_style() == FRAMES) {
        generate_video_sequence(video, images, conf);
    } else if (conf.get_style() == UP ) {
        generate_video_scroll_up(video, images, conf);
    } else if (conf.get_style() == DOWN) {
        // generate_video_scroll_down(video, images, conf);
    } else if (conf.get_style() == LEFT) {
        generate_video_scroll_left(video, images, conf);
    } else if (conf.get_style() == RIGHT) {
        // generate_video_scroll_right(video, images, conf);
    }

    // Finish Video
    video.release();
    std::cout << "Finished Generating Video!" << std::endl;

    // Clean Up
    for (auto &img : images) {
        img.release();
    }

    // Time
    size_t duration = difftime(time(NULL), start_time);
    size_t minutes = duration / 60;
    size_t seconds = duration % 60;
    std::cout << "Time: " << minutes << "m " << seconds << "s" << std::endl;

    return 0;
}

// ========= //
// Functions //
// ========= //

void scale_image_to_width(cv::Mat &img, const int dst_width) {
    double scale = (double)dst_width / (double)img.cols;
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image_to_height(cv::Mat &img, const int dst_height) {
    double scale = (double)dst_height / (double)img.rows;
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image_to_fit(cv::Mat &img, ptv::Config &conf) {
    double scale_w;
    double scale_h;
    double scale = 1.0;
    if (img.cols > conf.get_width() && img.cols > conf.get_height()) {
        scale_w = (double)conf.get_width() / (double)img.cols;
        scale_h = (double)conf.get_height() / (double)img.rows;
        scale = std::min(scale_w, scale_h);
    } else if (img.cols < conf.get_width() && img.rows < conf.get_height()) {
        scale_w = (double)conf.get_width() / (double)img.cols;
        scale_h = (double)conf.get_height() / (double)img.rows;
        scale = std::min(scale_w, scale_h);
    } else if (img.cols > conf.get_width() && img.rows <= conf.get_height()) {
        scale_w = (double)conf.get_width() / (double)img.cols;
        scale = scale_w;
    } else if (img.cols <= conf.get_width() && img.rows > conf.get_height()) {
        scale_h = (double)conf.get_height() / (double)img.rows;
        scale = scale_h;
    }
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image(cv::Mat &img, ptv::Config &conf) {
    if (conf.get_style() == FRAMES) {
        scale_image_to_fit(img, conf);
    } else if (conf.get_style() == UP || conf.get_style() == DOWN) {
        scale_image_to_width(img, conf.get_width());
    } else if (conf.get_style() == LEFT || conf.get_style() == RIGHT) {
        scale_image_to_height(img, conf.get_height());
    }
}

// Returns dpi to scale page to viewport width
double get_scaled_dpi_from_width(const poppler::page *page, const int width) {
    auto rect = page->page_rect(poppler::media_box);
    if (rect.width() == width) {
        return DEFAULT_DPI;
    }
    return ((double)width * DEFAULT_DPI) / (double)rect.width();
}

// Returns dpi to scale page to viewport width
double get_scaled_dpi_from_height(const poppler::page *page, const int height) {
    auto rect = page->page_rect(poppler::media_box);
    if (rect.height() == height) {
        return DEFAULT_DPI;
    }
    return ((double)height * DEFAULT_DPI) / (double)rect.height();
}

// Returns dpi that will scale the pdf page to fit the viewport dimentions
double get_scaled_dpi_to_fit(poppler::page *page, ptv::Config &conf) {
    double dpi_w;
    double dpi_h;
    poppler::rectf rect = page->page_rect(poppler::media_box);
    if (rect.width() > conf.get_width() && rect.height() > conf.get_height()) {
        dpi_w = ((double)conf.get_width() * DEFAULT_DPI) / rect.width();
        dpi_h = ((double)conf.get_height() * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    } else if (rect.width() < conf.get_width() && rect.height() < conf.get_height()) {
        dpi_w = ((double)conf.get_width() * DEFAULT_DPI) / rect.width();
        dpi_h = ((double)conf.get_height() * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    } else if (rect.width() > conf.get_width() && rect.height() <= conf.get_height()) {
        dpi_w = ((double)conf.get_width() * DEFAULT_DPI) / rect.width();
        return dpi_w;
    } else if (rect.width() <= conf.get_width() && rect.height() > conf.get_height()) {
        dpi_h = ((double)conf.get_height() * DEFAULT_DPI) / rect.height();
        return dpi_h;
    }
    return DEFAULT_DPI;
}

// Returns paths of each file in the directory, excludes nested directories.
std::vector<std::string> get_dir_img_paths(std::string dir_path) {
    std::map<int, std::string> image_map;
    std::vector<int> file_nums;
    std::vector<std::string> image_paths;

    // Creates hash-map of valid image paths
    for (const auto &entry : fs::directory_iterator(dir_path)) {
        if (!fs::is_directory(entry)) {
            int key;
            std::string path = entry.path().string();
            std::string name = entry.path().filename().string();
            try {
                key = std::stoi(name.substr(0, name.length() - name.find_last_of('.')));
            } catch (const std::invalid_argument& e) {
                std::cerr << "<!> Note: " << name << " skipped. File name is not a number." << std::endl;
                continue;
            } catch (const std::out_of_range& e) {
                std::cerr << "<!> Out of Range: getting int value from image name, get_images()." << std::endl;
                continue;
            }
            image_map.insert(std::make_pair(key, path));
            file_nums.push_back(key);
        }
    }

    // Numerically sorts each image path into a vector.
    std::sort(file_nums.begin(), file_nums.end());
    for (int num : file_nums) {
        image_paths.push_back(image_map[num]);
    }

    return image_paths;
}

// Returns images read from image sequence directory
void add_dir_images(const std::string dir_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf) {
    std::vector<std::string> img_paths = get_dir_img_paths(dir_path);

    // Reads images in numerical order
    size_t count = 0;
    size_t size = img_paths.size();
    size_t i = conf.get_is_reverse() ? size - 1 : 0;
    while (count < size) {
        // Renders .pdf files
        if ((int)img_paths[i].find(".pdf") != -1) {
            add_pdf_images(img_paths[i], vid_images, conf);
            count++;
            i += conf.get_is_reverse() ? -1 : 1;
            continue;
        }

        // Renders .gif files
        if ((int)img_paths[i].find(".gif") != -1) {
            if (conf.get_render_gifs()) {
                add_gif_images(img_paths[i], vid_images, conf);
            } else {
                std::cout << "Note: '" << img_paths[i] << "' skipped. Use --gif to render gif files as support is limited." << std::endl;
            }
            count++;
            i += conf.get_is_reverse() ? -1 : 1;
            continue;
        }

        cv::Mat mat = cv::imread(img_paths[i]);
        scale_image(mat, conf);
        vid_images.push_back(mat.clone());
        count++;
        i += conf.get_is_reverse() ? -1 : 1;
    }
}

// !!! Needs more control over fps and such !!!
// Adds each frame from the .gif file to the image vector.
void add_gif_images(const std::string gif_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf) {
    cv::VideoCapture cap(gif_path);
    if (!cap.isOpened()) {
        std::cerr << "<!> Error: Could not open GIF file." << std::endl;
        exit(1);
    }
    cv::Mat frame;
    while (cap.read(frame)) {
        if (frame.empty()) {
            break;
        }
        scale_image(frame, conf);
        vid_images.push_back(frame.clone());
    }
}

// Returns images read from pdf files
void add_pdf_images(const std::string pdf_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf) {
    auto renderer = poppler::page_renderer();
    poppler::document *pdf = poppler::document::load_from_file(pdf_path);

    // Gets pages of individual pdf files
    for (int pg = 0; pg < pdf->pages(); pg++) {
        double dpi = DEFAULT_DPI;
        poppler::page *page = pdf->create_page(pg);

        // Scales pages to correctly fit inside video resolution.
        if (conf.get_style()== FRAMES) {
            dpi = get_scaled_dpi_to_fit(page, conf);
        } else if (conf.get_style() == UP || conf.get_style() == DOWN) {
            dpi = get_scaled_dpi_from_width(page, conf.get_width());
        } else if (conf.get_style() == LEFT || conf.get_style() == RIGHT) {
            dpi = get_scaled_dpi_from_height(page, conf.get_height());
        }

        poppler::image img = renderer.render_page(page, dpi, dpi);
        cv::Mat mat;
        // Determine color space
        if (img.data() == nullptr) {
            std::cerr << "<!> Page " << pg << " has no data to load. Skipped." << std::endl;
            vid_images.erase(vid_images.begin() + pg);
            continue;
        } else if (img.format() == poppler::image::format_invalid) {
            std::cerr << "<!> Page " << pg << " has invalid image format. Skipped." << std::endl;
            vid_images.erase(vid_images.begin() + pg);
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
        vid_images.push_back(mat.clone());
    }
}

// Classic image sequence effect
void generate_video_sequence(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf) {
    for (size_t i = 0; i < imgs.size(); i ++) {
        cv::Mat img = imgs[i];
        cv::Mat vp_img = cv::Mat(conf.get_height(), conf.get_width(), img.type(), cv::Scalar(0, 0, 0));
        int x = 0;
        int y = 0;

        // Adds offset
        if (vp_img.cols - img.cols >= 2) {
            x += (vp_img.cols - img.cols) / 2;
        } else if (vp_img.rows - img.rows >= 2) {
            y += (vp_img.rows - img.rows) / 2;
        }

        // Prevents stretching of images when being rendered.
        // Keeps them within the vp.
        cv::Rect2i roi(x, y, img.cols, img.rows);
        img.copyTo(vp_img(roi));
        vid.write(vp_img);
    }
}

double get_pixels_per_frame(const std::vector<cv::Mat> &imgs, ptv::Config &conf) {
    // Find px_per_frame
    double px_per_frame = 1.0;

    if (conf.get_style() == UP || conf.get_style() == DOWN) {
        int height_of_imgs = 0;
        for (size_t i = 0; i < imgs.size(); i++) {
            height_of_imgs += imgs[i].size().height;
        }
        if (conf.get_duration() == 0) {
            px_per_frame += height_of_imgs / (conf.get_fps() * conf.get_spp() * imgs.size());
        } else {
            px_per_frame = height_of_imgs / (conf.get_fps() * conf.get_duration());
        }
    } else if (conf.get_style() == LEFT || conf.get_style() == RIGHT) {
        int width_of_imgs = 0;
        for (size_t i = 0; i < imgs.size(); i++) {
            width_of_imgs += imgs[i].size().width;
        }
        if (conf.get_duration() == 0) {
            px_per_frame += width_of_imgs / (conf.get_fps() * conf.get_spp() * imgs.size());
        } else {
            px_per_frame = width_of_imgs / (conf.get_fps() * conf.get_duration());
        }
    }
    if (px_per_frame <= 0.0f) {
        px_per_frame = 1.0f;
        std::cout << "<!> Warning: pixels per frame value was <= 0.0, set value to 1.0" << std::endl;
    }
    std::cout << "Pixels per frame: " << px_per_frame << std::endl;

    return px_per_frame;
}

// ===== SCROLL EFFECTS =====
void generate_video_scroll_up(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf) {
    double px_per_frame = get_pixels_per_frame(imgs, conf);
    double y_pos = 0.0f;
    cv::Mat dst_img(conf.get_height() + imgs[0].rows, conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0)); // Black Box ( video height + first img height by video width )
    cv::Mat vp_img(conf.get_height(), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0));

    // Generates and writes frames to video file. Prevents creating space between each new img.
    for (size_t i = 0; i < imgs.size(); i++) {
        if (i == 0) {
            // Stores first img outside of viewports ROI
            cv::Rect2d roi(0, conf.get_height(), imgs[0].cols, imgs[0].rows);
            imgs[0].copyTo(dst_img(roi));
        } else {
            // Logic to readjust the translation of video frames
            double unused_height = dst_img.rows - (y_pos + conf.get_height()); // Height not yet rendered in vp.
            double new_dst_h = conf.get_height() + unused_height + imgs[i].rows;

            // Allows video to scoll to black screen at end
            cv::Mat new_dst_img = (imgs.size() - 1 == i) ?
                cv::Mat((conf.get_height() + std::ceil(new_dst_h) + px_per_frame), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0))
                : cv::Mat(std::ceil(new_dst_h), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0));

            cv::Rect2d dst_roi(0.0f, y_pos, conf.get_width(), (conf.get_height() + unused_height));
            cv::Rect2d new_dst_roi(0.0f, 0.0f, conf.get_width(), (conf.get_height() + unused_height));
            cv::Rect2d next_roi(0.0f, (conf.get_height() + unused_height), conf.get_width(), imgs[i].rows);

            dst_img(dst_roi).copyTo(new_dst_img(new_dst_roi));
            imgs[i].copyTo(new_dst_img(next_roi));
            dst_img = new_dst_img;
            y_pos = 0.0f;
        }

        // Write frames to video.
        while (((double)dst_img.rows - (y_pos+ conf.get_height())) > px_per_frame) {
            cv::Rect2d roi(0.0f, y_pos, conf.get_width(), conf.get_height());
            dst_img(roi).copyTo(vp_img);
            vid.write(vp_img);
            y_pos += px_per_frame;
        }

        // Finished Rendering Current Image
        std::cout << i + 1 << "/" << imgs.size() << std::endl;
    }
}

//
void generate_video_scroll_left(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, ptv::Config &conf) {
    double px_per_frame = get_pixels_per_frame(imgs, conf);
    double x_pos = 0.0f;
    cv::Mat dst_img(conf.get_height(), (conf.get_width() + imgs[0].cols), CV_8UC3, cv::Scalar(0, 0, 0)); // Contains frame content ( video height by video width + first img width)
    cv::Mat vp_img(conf.get_height(), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0)); // Frame content gets rendered here

    for (size_t i = 0; i < imgs.size(); i++) {
        if (i == 0) {
            // Stores first img just outside of viewport's ROI
            cv::Rect2d roi(conf.get_width(), 0, imgs[0].cols, imgs[0].rows);
            imgs[0].copyTo(dst_img(roi));
        } else {
            // Logic to readjust the translation of video frames
            double unused_width = dst_img.cols - (x_pos + conf.get_width()); // Width not yet rendered in vp.
            double new_dst_w = conf.get_width() + unused_width + imgs[i].cols; // New width to account for unused pixels

            // Allows video to scoll to black screen at end
            cv::Mat new_dst_img;
            if (imgs.size() - 1 == i) {
                new_dst_img = cv::Mat(conf.get_height(), (conf.get_width() + std::ceil(new_dst_w) + px_per_frame), CV_8UC3, cv::Scalar(0, 0, 0));
            } else {
                new_dst_img = cv::Mat(conf.get_height(), std::ceil(new_dst_w), CV_8UC3, cv::Scalar(0, 0, 0));
            }

            cv::Rect2d dst_roi(x_pos, 0.0f, (conf.get_width() + unused_width), conf.get_height()); // Content not yet rendered.
            cv::Rect2d new_dst_roi(0.0f, 0.0f, (conf.get_width() + unused_width), conf.get_height()); // Reseting content not yet rendered.
            cv::Rect2d next_roi((conf.get_width() + unused_width), 0.0f, imgs[i].cols, conf.get_height());

            dst_img(dst_roi).copyTo(new_dst_img(new_dst_roi));
            imgs[i].copyTo(new_dst_img(next_roi));
            dst_img = new_dst_img;
            x_pos = 0.0f;
        }

        // Write frames to video
        while (((double)dst_img.cols - (conf.get_width() + x_pos)) > px_per_frame) {
            cv::Rect2d roi(x_pos, 0.0f, conf.get_width(), conf.get_height());
            dst_img(roi).copyTo(vp_img);
            vid.write(vp_img);
            x_pos += px_per_frame;
        }

        // Finished Rendering Current Image
        std::cout << i + 1 << "/" << imgs.size() << std::endl;
    }
}
