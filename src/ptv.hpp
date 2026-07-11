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


#define FRAMES "FRAMES"
#define UP "UP"
#define DOWN "DOWN"
#define LEFT "LEFT"
#define RIGHT "RIGHT"

#define DEFAULT_DPI 72.0f

}
const std::string HELP_TXT =
    "\n" COLOR_BOLD "ptv" COLOR_RESET " \u2014 PDF / Image Sequence to Video\n\n"
    COLOR_BOLD "Usage:\n" COLOR_RESET
    "  ptv [input paths...] [options]\n\n"

    COLOR_BOLD "Inputs:\n" COLOR_RESET
    "  " COLOR_ORANGE "<path.pdf>" COLOR_RESET "                PDF file(s) to render.\n"
    "  " COLOR_ORANGE "<sequence_dir/>" COLOR_RESET "           Directory of numbered images.\n\n"

    COLOR_BOLD "Options:\n" COLOR_RESET
    "  " COLOR_CYAN "-r <int>x<int>" COLOR_RESET "            Output resolution. 0 preserves original. " COLOR_DIM "(default: 1280x720)" COLOR_RESET "\n"
    "  " COLOR_CYAN "-f <float>" COLOR_RESET "                Frames per second.\n"
    "  " COLOR_CYAN "-s <float>" COLOR_RESET "                Seconds per frame.\n"
    "  " COLOR_CYAN "-d <float>" COLOR_RESET "                Duration of video in seconds. " COLOR_DIM "(overrides -s)" COLOR_RESET "\n"
    "  " COLOR_CYAN "-o <output_path>" COLOR_RESET "          Output file path. " COLOR_DIM "(.mp4 only, auto-named if blank)" COLOR_RESET "\n"
    "  " COLOR_CYAN "-a <Up|Down|Left|Right>" COLOR_RESET "   Scroll content continuously instead of per-page frames.\n"
    "  " COLOR_CYAN "--gif" COLOR_RESET "                     Render .gif files found in image sequences.\n"
    "  " COLOR_CYAN "--rev-seq" COLOR_RESET "                 Load numbered images in descending order.\n"
    "  " COLOR_CYAN "-h, --help" COLOR_RESET "                Show this help text.\n\n"

    COLOR_BOLD "Examples:\n" COLOR_RESET
    "  " COLOR_DIM "ptv slides.pdf -r 1920x1080 -f 30 -s 2" COLOR_RESET "\n"
    "  " COLOR_DIM "ptv frames/ -a Up -f 60 -o output.mp4" COLOR_RESET "\n";

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
