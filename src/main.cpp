#include "opencv.hpp"
#include "poppler.hpp"
#include "ptv.hpp"

namespace fs = std::filesystem;

// ======= //
// Headers //
// ======= //

void scale_image_to_width(cv::Mat &img, int dst_width);
void scale_image_to_fit(cv::Mat& img, ptv::Config &conf);

float get_scaled_dpi_from_width(poppler::page *page, int width); // dpi fits page to vp width
float get_scaled_dpi_to_fit(poppler::page *page, ptv::Config &conf); // dpi fits page in viewport

std::map<int, std::string> get_dir_entry_map(std::string dir_path);
void add_dir_images(std::string dir_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf);
void add_pdf_images(std::string pdf_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf);
void add_gif_images(std::string gif_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf);

void generate_scroll_video(cv::VideoWriter &vid, std::vector<cv::Mat> &imgs, ptv::Config &conf);
void generate_sequence_video(cv::VideoWriter &vid, std::vector<cv::Mat> &imgs, ptv::Config &conf);

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
        generate_sequence_video(video, images, conf);
    } else {
        generate_scroll_video(video, images, conf); // TODO: Add scroll Down, Left, and Right
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
    } else if (img.cols < conf.get_width() && img.rows < conf.get_height()) {
        scale_w = (float)conf.get_width() / (float)img.cols;
        scale_h = (float)conf.get_height() / (float)img.rows;
        scale = std::min(scale_w, scale_h);
    } else if (img.cols > conf.get_width() && img.rows <= conf.get_height()) {
        scale_w = (float)conf.get_width() / (float)img.cols;
        scale = scale_w;
    } else if (img.cols <= conf.get_width() && img.rows > conf.get_height()) {
        scale_h = (float)conf.get_height() / (float)img.rows;
        scale = scale_h;
    }
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

// Returns dpi to scale page to viewport width
float get_scaled_dpi_from_width(poppler::page *page, int width) {
    auto rect = page->page_rect(poppler::media_box);
    if (rect.width() == width) {
        return DEFAULT_DPI;
    }
    return ((float)width * DEFAULT_DPI) / (float)rect.width();
}

// Returns dpi that will scale the pdf page to fit the viewport dimentions
float get_scaled_dpi_to_fit(poppler::page *page, ptv::Config &conf) {
    float dpi_w;
    float dpi_h;
    poppler::rectf rect = page->page_rect(poppler::media_box);
    if (rect.width() > conf.get_width() && rect.height() > conf.get_height()) {
        dpi_w = ((float)conf.get_width() * DEFAULT_DPI) / rect.width();
        dpi_h = ((float)conf.get_height() * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    } else if (rect.width() < conf.get_width() && rect.height() < conf.get_height()) {
        dpi_w = ((float)conf.get_width() * DEFAULT_DPI) / rect.width();
        dpi_h = ((float)conf.get_height() * DEFAULT_DPI) / rect.height();
        return std::min(dpi_w, dpi_h);
    } else if (rect.width() > conf.get_width() && rect.height() <= conf.get_height()) {
        dpi_w = ((float)conf.get_width() * DEFAULT_DPI) / rect.width();
        return dpi_w;
    } else if (rect.width() <= conf.get_width() && rect.height() > conf.get_height()) {
        dpi_h = ((float)conf.get_height() * DEFAULT_DPI) / rect.height();
        return dpi_h;
    }
    return DEFAULT_DPI;
}

// Returns paths of each file in the directory, excludes nested directories.
std::map<int, std::string> get_dir_entry_map(std::string dir_path) {
    std::map<int, std::string> image_map;

    // Creates hash-map of valid image paths in numerical order
    for (const auto &entry : fs::directory_iterator(dir_path)) {
        if (!fs::is_directory(entry)) {
            int index;
            std::string path = entry.path().string();
            std::string name = entry.path().filename().string();
            try {
                index = std::stoi(name.substr(0, name.length() - name.find_last_of('.')));
            } catch (const std::invalid_argument& e) {
                std::cerr << "<!> Note: " << name << " skipped. File name is not a number." << std::endl;
                continue;
            } catch (const std::out_of_range& e) {
                std::cerr << "<!> Out of Range: geting int value from image name, get_images()." << std::endl;
                continue;
            }
            image_map.insert(std::make_pair(index, path));
        }
    }
    return image_map;
}

// Returns images read from image sequence directory
void add_dir_images(std::string dir_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf) {
    std::map<int, std::string> entry_map = get_dir_entry_map(dir_path);

    // Reads images in numerical order
    size_t count = 0;
    size_t map_size = entry_map.size();

    for (size_t i = 0; count < map_size; i++) {
        if (entry_map[i] == "") {
            continue;
        }

        // Renders .pdf files
        if ((int)entry_map[i].find(".pdf") != -1) {
            add_pdf_images(entry_map[i], vid_images, conf);
            count++;
            continue;
        }

        // Renders .gif files
        if ((int)entry_map[i].find(".gif") != -1) {
            if (conf.get_render_gifs()) {
                add_gif_images(entry_map[i], vid_images, conf);
            } else {
                std::cout << "Note: '" << i << ".gif' skipped. Use --gif to render gif files as support is limited." << std::endl;
            }
            count++;
            continue;
        }

        cv::Mat mat = cv::imread(entry_map[i]);
        if (conf.get_style() == FRAMES) {
            scale_image_to_fit(mat, conf);
        } else {
            scale_image_to_width(mat, conf.get_width());
        }

        vid_images.push_back(mat.clone());
        count++;
    }
}

// !!! NEEDS WORK !!!
void add_gif_images(std::string gif_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf) {

    cv::VideoCapture cap(gif_path);
    if (!cap.isOpened()) {
        std::cerr << "<!> Error: Could not open GIF file." << std::endl;
        exit(1);
    }

    cv::Mat frame;
    while (cap.read(frame)) {
        if (frame.empty()) break;

        if (conf.get_style() == FRAMES) {
            scale_image_to_fit(frame, conf);
        } else {
            scale_image_to_width(frame, conf.get_width());
        }

        vid_images.push_back(frame.clone());
    }
}

// Returns images read from pdf files
void add_pdf_images(std::string pdf_path, std::vector<cv::Mat> &vid_images, ptv::Config &conf) {
    auto renderer = poppler::page_renderer();
    poppler::document *pdf = poppler::document::load_from_file(pdf_path);

    // Gets pages of individual pdf files
    for (int pg = 0; pg < pdf->pages(); pg++) {
        float dpi = DEFAULT_DPI;
        poppler::page *page = pdf->create_page(pg);

        // Scales pages to correctly fit inside video resolution.
        if (conf.get_style()== FRAMES) {
            dpi = get_scaled_dpi_to_fit(page, conf);
        } else {
            dpi = get_scaled_dpi_from_width(page, conf.get_width());
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

// Scroll effect
void generate_scroll_video(cv::VideoWriter &vid, std::vector<cv::Mat> &imgs, ptv::Config &conf) {
    float px_per_frame = 0.0f;
    float h = 0.0f;
    cv::Mat dst_img(conf.get_height() + imgs[0].rows, conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0)); // Black Box ( 2x video height by 1x video width )
    cv::Mat vp_img(conf.get_height(), conf.get_width(), CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Rect2d roi(0, conf.get_height(), imgs[0].cols, imgs[0].rows);
    imgs[0].copyTo(dst_img(roi));

    // Find px_per_frame
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

    //
    for (size_t i = 1; i < imgs.size(); i++) {
        // Generates and writes frames to video file
        while (((float)dst_img.rows - (h + conf.get_height())) > px_per_frame) {
            cv::Rect2d roi(0.0f, h, conf.get_width(), conf.get_height());
            dst_img(roi).copyTo(vp_img);
            vid.write(vp_img);
            h += px_per_frame;
        }

        // Logic to readjust the translation of video frames
        float unused_height = dst_img.rows - (h + conf.get_height()); // Height not yet rendered in vp.
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

// Classic image sequence effect
void generate_sequence_video(cv::VideoWriter &vid, std::vector<cv::Mat> &imgs, ptv::Config &conf) {
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
