#include "ptv.hpp"
#include <filesystem>

Config::Config(int argc, char **argv) :
render_gifs_(false),
is_reverse_(false),
width_(1280),
height_(720),
fps_(1.0f),
spp_(1.0f),
duration_(0.0f),
style_(FRAMES),
output_(""),
container_(".mp4"),
codec_("avc1"),
input_paths_({}),
input_types_({}) {
    if (argc < 2) {
        std::cout << HELP_TXT << std::endl;
        exit(1);
    }

    // Update Default Settings
    for (int i = 1; i < argc; i++) {
        std::string arg = std::string(argv[i]);
        if (arg == "-h" || arg == "--help") {
            std::cout << HELP_TXT << std::endl;
            exit(1);
        } else if ((int)arg.find(".pdf") > -1) {
            if (!std::filesystem::exists(arg)) {
                std::cerr << "<!> Error: '" << arg << "' does not exist." << std::endl;
                exit(1);
            }
            if (arg.substr(arg.length() - 4, 4) != ".pdf") {
                std::cout << "<!> Error: '" << arg << "' is not a PDF file." << std::endl;
                exit(1);
            }
            input_paths_.push_back(arg);
            input_types_.push_back("pdf");
        } else if ((int)arg.find('/') > -1) {
            if (!std::filesystem::is_directory(arg)) {
                std::cerr << "<!> Error: '" << arg <<  "' is not a valid directory." << std::endl;
                exit(1);
            }
            if (arg[arg.length() - 1] != '/') {
                arg.push_back('/');
            }
            input_paths_.push_back(arg);
            input_types_.push_back("dir");
        } else if (arg == "-r") {
            i++;
            std::string currArg = std::string(argv[i]);
            if ((int)currArg.find('x') == -1) {
                std::cerr << "<!> Error: '" << argv[i] << "' is not valid input for '-r'. Correct: 0x0 or 1920x1080 or 0x720" << std::endl;
                exit(1);
            }
            if ((int)currArg.size() < 3) {
                std::cerr << "<!> Error: '" << argv[i] << "' is not valid input for '-r'. Correct: 0x0 or 1920x1080 or 0x720" << std::endl;
            }
            width_ = std::stoi(currArg.substr(0, currArg.find('x')));
            height_ = std::stoi(currArg.substr(currArg.find('x') + 1));
            set_width(width_);
            set_height(height_);
            if (width_ < 0 || height_ < 0) {
                std::cerr << "<!> Resolution input cannot be negative." << std::endl;
                exit(1);
            }
        } else if (arg == "-f") {
            i++;
            fps_ = std::stof(argv[i]);
        } else if (arg == "-s") {
            i++;
            spp_ = std::stof(argv[i]);
        } else if (arg == "-d") {
            i++;
            duration_ = std::stof(argv[i]);
        } else if (arg == "-o") {
            i++;
            arg = argv[i];
            if ((int)arg.find('/') != -1) {
                std::string dir = arg.substr(0, arg.find_last_of('/') + 1);
                if (!std::filesystem::exists(dir)) {
                    std::cout << "<!> Error: Output directory does not exists." << std::endl;
                    exit(1);
                }
            }
            if ((size_t)arg.find_last_of('/') == arg.size() - 1) {
                std::cerr << "<!> Error: Output file cannot be a directory." << std::endl;
                exit(1);
            }
            if ((int)arg.find(container_) == -1) {
                output_ = arg + container_;
            } else {
                output_ = arg;
            }
        } else if (arg == "-a") {
            i++;
            std::string a = std::string(argv[i]);
            for (size_t i = 0; i < a.size(); i++) {
                a[i] = toupper(a[i]);
            }
            if (a != UP && a != DOWN && a != LEFT && a != RIGHT) {
                std::cerr << "<!> Invalid input for '-a'. Must be [Up|Down|Left|Right]" << std::endl;
                exit(1);
            }
            style_ = a;
        } else if (arg == "--gif") {
            render_gifs_ = true;
        } else if (arg == "--rev-seq") {
            is_reverse_ = true;
        } else {
            std::cerr << "<!> Unknown argument detected: " << argv[i] << std::endl;
            exit(1);
        }
    }

    // Check input
    if (input_paths_.size() == 0) {
        std::cerr << "<!> No input path was specified." << std::endl;
        exit(1);
    }

    // Print Settings
    if (input_paths_.size() > 1) {
        std::cout << "(" << input_paths_.size() << ") Inputs: ";
    } else {
        std::cout << "(1) Input: ";
    }
    for (size_t i = 0; i < input_paths_.size(); i++) {
        if (i > 0) {
            std::cout << " + ";
        }
        std::cout << "" + input_paths_[i];
    }
    std::cout << std::endl;
    if (output_ == "" && input_types_[0] == "pdf") {
        std::string path = input_paths_[0];
        output_ = path.substr(0, path.find_last_of('.')) + container_;
    } else if (output_ == "" && input_types_[0] == "dir") {
        std::string path = input_paths_[0];
        output_ = path.substr(0, path.find_last_of('/')) + container_;
    }
    std::cout << "Output: " << output_ << std::endl;
    std::cout << "Resolution: " << width_ << "x" << height_ << std::endl;
    std::cout << "FPS: " << fps_ << std::endl;
    if (duration_ != 0 && style_ != FRAMES) {
        std::cout << "Duration: " << duration_ << "s" << std::endl;
    } else if (style_ != FRAMES) {
        std::cout << "SPP: " << spp_ << std::endl;
    }
    std::cout << "Animated: " << style_ << std::endl;

    // User Confirm Setttings
    std::string check;
    std::cout << "Are these values correct: [Y/n]" << std::endl;
    std::getline(std::cin, check);
    if (!check.empty() && check != "Y" && check != "y") {
        exit(1);
    }
    std::cin.clear();
}
// Setters
void Config::set_width(int w) { width_ = w % 2 == 0 ? w : w + 1; }
void Config::set_height(int h) { height_ = h % 2 == 0 ? h : h + 1; }
void Config::set_resolution(cv::Mat img) {
    if (width_ == 0) {
        set_width(img.cols);
    }
    if (height_ == 0) {
        set_height(img.rows);
    }
}
void Config::set_resolution(poppler::rectf rect) {
    if (width_ == 0) {
        set_width(rect.width());
    }
    if (height_ == 0) {
        set_height(rect.height());
    }
}
// Getters
bool Config::get_render_gifs() { return render_gifs_; }
bool Config::get_is_reverse() { return is_reverse_; }
int Config::get_width() { return width_; }
int Config::get_height() { return height_; }
float Config::get_fps() { return fps_; }
float Config::get_spp() { return spp_; }
float Config::get_duration() { return  duration_; }
std::string Config::get_style() { return style_; }
std::string Config::get_output() { return output_; }
std::string Config::get_codec() { return codec_; }
std::vector<std::string> Config::get_input_paths() { return input_paths_; }
std::vector<std::string> Config::get_input_types() { return input_types_; }

void scale_image_to_width(cv::Mat &img, const int dst_width) {
    double scale = (double)dst_width / (double)img.cols;
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image_to_height(cv::Mat &img, const int dst_height) {
    double scale = (double)dst_height / (double)img.rows;
    cv::resize(img, img, cv::Size(), scale, scale, cv::INTER_LINEAR);
}

void scale_image_to_fit(cv::Mat &img, Config &conf) {
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

void scale_image(cv::Mat &img, Config &conf) {
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
double get_scaled_dpi_to_fit(poppler::page *page, Config &conf) {
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
    for (const auto &entry : std::filesystem::directory_iterator(dir_path)) {
        if (!std::filesystem::is_directory(entry)) {
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
void add_dir_images(const std::string dir_path, std::vector<cv::Mat> &vid_images, Config &conf) {
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
        // Updates resolution to first image
        if (vid_images.size() == 0 && (conf.get_width() == 0 || conf.get_height() == 0)) {
            if (conf.get_width() == 0) {
                conf.set_width(mat.cols);
            }
            if (conf.get_height() == 0) {
                conf.set_height(mat.rows);
            }
        }
        scale_image(mat, conf);
        vid_images.push_back(mat.clone());
        count++;
        i += conf.get_is_reverse() ? -1 : 1;
    }
}

// !!! Needs more control over fps and such !!!
// Adds each frame from the .gif file to the image vector.
void add_gif_images(const std::string gif_path, std::vector<cv::Mat> &vid_images, Config &conf) {
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
void add_pdf_images(const std::string pdf_path, std::vector<cv::Mat> &vid_images, Config &conf) {
    auto renderer = poppler::page_renderer();
    poppler::document *pdf = poppler::document::load_from_file(pdf_path);

    // Gets pages of individual pdf files
    for (int pg = 0; pg < pdf->pages(); pg++) {
        double dpi = DEFAULT_DPI;
        poppler::page *page = pdf->create_page(pg);

        // Updates resolution to first image
        if (vid_images.size() == 0 && (conf.get_width() == 0 || conf.get_height() == 0)) {
            if (conf.get_width() == 0) {
                conf.set_width(page->page_rect().width());
            }
            if (conf.get_height() == 0) {
                conf.set_height(page->page_rect().height());
            }
        }

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
void generate_video_sequence(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf) {
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

double get_pixels_per_frame(const std::vector<cv::Mat> &imgs, Config &conf) {
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
void generate_video_scroll_up(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf) {
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
void generate_video_scroll_left(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf) {
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
