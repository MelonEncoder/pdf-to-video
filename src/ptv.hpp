#ifndef PTV_HPP
#define PTV_HPP

// std
#include <string>
#include <vector>
// cv
#include <opencv2/core/persistence.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
// poppler
#include <poppler/cpp/poppler-global.h>
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-image.h>
#include <poppler/cpp/poppler-page.h>
#include <poppler/cpp/poppler-destination.h>
#include <poppler/cpp/poppler-embedded-file.h>
#include <poppler/cpp/poppler-page-renderer.h>

namespace ptv {

// TERMINAL STYLES
#define COLOR_RESET "\x1b[0m"
#define COLOR_BOLD "\x1b[1m"
#define COLOR_DIM "\x1b[2m"
#define COLOR_GREEN "\x1b[32m"
#define COLOR_CYAN "\x1b[36m"
#define COLOR_ORANGE "\x1b[38;2;255;165;0m"

#define HELP_TXT "\
Usage:\n\
   ptv [...options]\n\
Options: \n\
   [...pdf_paths]                         :  PDF file paths. Ex: /home/usr/example.pdf\n\
   [...img_sequence_dirs]                 :  image sequence directorys. Ex: /home/usr/example_seq/\n\
   -r <int>x<int>                         :  set output resolution. Use 0 to preserve resolution of original content, default: 1280x720 \n\
   -f <float>                             :  frames per second.\n\
   -s <float>                             :  seconds per frame.\n\
   -d <float>                             :  duration in seconds. NOTE: overides -s (seconds per frame)\n\
   -o [output_path]                       :  currently only support .mp4 files, leave blank for auto output\n\
   -a [Up|Down|Left|Right]                :  scrolls content instead of making each page a frame (like a slideshow).\n\
   -h, --help                             :  show this help text.\n\
   --gif                                  :  render .gif files in image sequences\n\
   --rev-seq                              :  load numbered imgs from dir in decending order, larger # to smaller #\n\
"
#define FRAMES "FRAMES"
#define UP "UP"
#define DOWN "DOWN"
#define LEFT "LEFT"
#define RIGHT "RIGHT"

#define DEFAULT_DPI 72.0f

}

class Config {
    private:
        bool render_gifs_;
        bool is_reverse_;
        int width_;
        int height_;
        float fps_;
        float spp_;
        float duration_;
        std::string style_;
        std::string output_;
        std::string container_;
        std::string codec_;
        std::vector<std::string> input_paths_;
        std::vector<std::string> input_types_;
    public:
        Config(int argc, char **argv);
        // Setters
        void set_width(int w);
        void set_height(int h);
        void set_resolution(cv::Mat img);
        void set_resolution(poppler::rectf rect);
        // Getters
        bool get_render_gifs();
        bool get_is_reverse();
        int get_width();
        int get_height();
        float get_fps();
        float get_spp();
        float get_duration();
        std::string get_style();
        std::string get_output();
        std::string get_codec();
        std::vector<std::string> get_input_paths();
        std::vector<std::string> get_input_types();
};

// HELPER
void scale_image_to_width(cv::Mat &img, const int dst_width);
void scale_image_to_height(cv::Mat &img, const int dst_height);
void scale_image_to_fit(cv::Mat& img, Config &conf);
void scale_image(cv::Mat &img, Config &conf);

double get_scaled_dpi_from_width(const poppler::page *page, const int width); // dpi fits page width to vp width
double get_scaled_dpi_from_height(const poppler::page *page, const int height); // dpi fits page height to vp height
double get_scaled_dpi_to_fit(poppler::page *page, Config &conf); // dpi fits entire page in viewport

std::vector<std::string> get_dir_img_paths(std::string dir_path);

double get_pixels_per_frame(const std::vector<cv::Mat> &imgs, Config &conf);

// LOADING
void add_dir_images(const std::string dir_path, std::vector<cv::Mat> &vid_images, Config &conf);
void add_pdf_images(const std::string pdf_path, std::vector<cv::Mat> &vid_images, Config &conf);
void add_gif_images(const std::string gif_path, std::vector<cv::Mat> &vid_images, Config &conf);

// RENDERING
void render_video_sequence(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf);
void render_video_scroll_up(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf);
// void render_video_scroll_down(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf);
void render_video_scroll_left(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf);
// void render_video_scroll_right(cv::VideoWriter &vid, const std::vector<cv::Mat> &imgs, Config &conf);

// MISC
void print_duration(const time_t start_time);
void print_progress_bar(const std::string& label, const int current_value, const int total, const bool first_call, const int width = 44);
void print_banner(const std::string &title);
std::string make_scroll_label(const double px_per_frame);

void set_default_resolution(const std::string path, const std::string type, Config &conf);

#endif
