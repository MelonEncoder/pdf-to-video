#ifndef PTV_HPP
#define PTV_HPP

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

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
"
#define FRAMES "Frames"
#define UP "Up"
#define DOWN "Down"
#define LEFT "Left"
#define RIGHT "Right"

#define DEFAULT_DPI 72.0f

class Config {
    bool is_pdf_ = false;
    bool is_seq_ = false;
    int width_ = 1280;
    int height_ = 720;
    float fps_ = 1;
    float spp_ = 1;
    float duration_ = 0;
    std::string style_ = FRAMES;
    std::string output_ = "";
    std::string format_ = ".mp4";
    std::vector<std::string> pdf_paths_ = {};
    std::vector<std::string> seq_dirs_ = {};

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
                    if (is_seq_) {
                        std::cerr << "<!> Error: Cannot convert Image Sequence and PDF at the same time." << std::endl;
                        exit(1);
                    } else {
                        is_pdf_ = true;
                    }
                    if (!std::filesystem::exists(arg)) {
                        std::cerr << "<!> Error: '" << arg << "' does not exist." << std::endl;
                        exit(1);
                    }
                    if (arg.substr(arg.length() - 4, 4) != ".pdf") {
                        std::cout << "<!> Error: '" << arg << "' is not a PDF file." << std::endl;
                        exit(1);
                    }
                    pdf_paths_.push_back(arg);
                } else if ((int)arg.find('/') > -1) {
                    if (is_pdf_) {
                        std::cerr << "<!> Error: Cannot convert PDF and Image Sequence at the same time." << std::endl;
                        exit(1);
                    } else {
                        is_seq_ = true;
                    }
                    if (!std::filesystem::is_directory(arg)) {
                        std::cerr << "<!> Error: '" << arg <<  "' is not a valid directory." << std::endl;
                        exit(1);
                    }
                    if (arg[arg.length() - 1] != '/') {
                        arg.push_back('/');
                    }
                    seq_dirs_.push_back(arg);
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
                    if ((int)arg.find(format_) == -1) {
                        std::cerr << "<!> Error: output file extension must be " + format_ << std::endl;
                        exit(1);
                    }
                    if ((int)arg.find('/') == -1) {
                        output_ = arg;
                    } else {
                        std::string dir = arg.substr(0, arg.find_last_of('/') + 1);
                        if (!std::filesystem::exists(dir)) {
                            std::cout << "<!> Error: Output directory does not exists." << std::endl;
                            exit(1);
                        }
                    }
                    output_ = arg;
                } else if (arg == "-a") {
                    i++;
                    std::string a = std::string(argv[i]);
                    if (a != UP && a != DOWN && a != LEFT && a != RIGHT) {
                        std::cerr << "<!> Invalid input for '-a'. Must be [Up|Down|Left|Right]" << std::endl;
                        exit(1);
                    }
                    style_ = a;
                } else {
                    std::cerr << "<!> Unknown argument detected: " << argv[i] << std::endl;
                    exit(1);
                }
            }

            if (pdf_paths_.size() < 1 && seq_dirs_.size() < 1) {
                std::cerr << "<!> No pdf path or image sequence director was specified." << std::endl;
                exit(1);
            }

            // Print Current Settings
            if (is_pdf_) {
                std::cout << "PDF Paths (" << pdf_paths_.size() << "): ";
                for (size_t i = 0; i < pdf_paths_.size(); i++) {
                    if (i > 0) {
                        std::cout << " + ";
                    }
                    std::cout << "" + pdf_paths_[i];
                }
            } else if (is_seq_) {
                std::cout << "Sequence Directories (" << seq_dirs_.size() << "): ";
                for (size_t i = 0; i < seq_dirs_.size(); i++) {
                    if (i > 0) {
                        std::cout << " + ";
                    }
                    std::cout << "" + seq_dirs_[i];
                }
            }
            std::cout << std::endl;

            if (output_ == "" && is_seq_) {
                std::string path = seq_dirs_[0];
                output_ = path.substr(0, path.find_last_of('/')) + format_;
            } else if (output_ == "" && is_pdf_) {
                std::string path = pdf_paths_[0];
                output_ = path.substr(0, path.find_last_of('.')) + format_;
            }
            std::cout << "Output: " << output_ << std::endl;
            std::cout << "Resolution: " << width_ << "x" << height_ << std::endl;
            std::cout << "FPS: " << fps_ << std::endl;
            if (duration_ != 0 && style_ == UP) {
                std::cout << "Duration: " << duration_ << "s" << std::endl;
            } else if (style_ == UP) {
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

        // Getters
        bool get_is_pdf() { return is_pdf_; }
        bool get_is_seq() { return is_seq_; }
        int get_width() { return width_; }
        int get_height() { return height_; }
        float get_fps() { return fps_; }
        float get_spp() { return spp_; }
        float get_duration() { return  duration_; }
        std::string get_style() { return style_; }
        std::string get_output() { return output_; }
        std::string get_format() { return format_; }
        std::vector<std::string> get_pdf_paths() { return pdf_paths_; }
        std::vector<std::string> get_seq_dirs() { return seq_dirs_; }

        // Setters
        void set_width(unsigned int w) { width_ = w; }
        void set_height(unsigned int h) { height_ = h; }
};

}
#endif
