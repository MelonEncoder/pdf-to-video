#ifndef PTV_HPP
#define PTV_HPP

#include <cctype>
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include "opencv.hpp"
#include "poppler.hpp"
#include "poppler-rectangle.h"

namespace ptv {

#define HELP_TXT "\
ptv [options...]\n\
Help Options: \n\
   -h, --help                             :  show this help text.\n\
Application Options: \n\
   [pdf_paths...]                         :  PDF file path. /home/usr/example.pdf\n\
   [img_sequence_dirs...]                 :  image sequence directory. /home/usr/example_seq/\n\
   -r <int>x<int>                         :  set output resolution. Use 0 to preserve resolution of original content, default: 1280x720 \n\
   -f <float>                             :  frames per second.\n\
   -s <float>                             :  seconds per frame.\n\
   -d <float>                             :  duration in seconds. NOTE: overides -s (seconds per frame)\n\
   -o [output_path]                       :  currently only support .mp4 files, leave blank for auto output\n\
   -a [Up|Down|Left|Right]                :  scrolls content instead of making each page a frame (like a slideshow).\n\
   --gif                                  :  render .gif files in image sequences\n\
   --rev-seq                              :  load numbered imgs from dir in decending order, larger # to smaller #\n\
"
#define FRAMES "FRAMES"
#define UP "UP"
#define DOWN "DOWN"
#define LEFT "LEFT"
#define RIGHT "RIGHT"

#define DEFAULT_DPI 72.0f

class Config {
    bool render_gifs_ = false;
    bool is_reverse_ = false;
    int width_ = 1280;
    int height_ = 720;
    float fps_ = 1;
    float spp_ = 1;
    float duration_ = 0;
    std::string style_ = FRAMES;
    std::string output_ = "";
    std::string container_ = ".mp4";
    std::string fourcc_codec_ = "avc1";
    std::vector<std::string> input_paths_ = {};
    std::vector<std::string> input_types_ = {};

    public:
        Config(int argc, char **argv) {
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
                    if ((int)arg.find_last_of('/') == arg.size() - 1) {
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

        void set_resolution(cv::Mat img) {
            if (width_ == 0) {
                set_width(img.cols);
            }
            if (height_ == 0) {
                set_height(img.rows);
            }
        }

        void set_resolution(poppler::rectf rect) {
            if (width_ == 0) {
                set_width(rect.width());
            }
            if (height_ == 0) {
                set_height(rect.height());
            }
        }

        // Getters
        bool get_render_gifs() { return render_gifs_; }
        bool get_is_reverse() { return is_reverse_; }
        int get_width() { return width_; }
        int get_height() { return height_; }
        float get_fps() { return fps_; }
        float get_spp() { return spp_; }
        float get_duration() { return  duration_; }
        std::string get_style() { return style_; }
        std::string get_output() { return output_; }
        std::string get_codec() { return fourcc_codec_; }
        std::vector<std::string> get_input_paths() { return input_paths_; }
        std::vector<std::string> get_input_types() { return input_types_; }

        // Setters
        void set_width(int w) { width_ = w % 2 == 0 ? w : w + 1; }
        void set_height(int h) { height_ = h % 2 == 0 ? h : h + 1; }
};

}
#endif
