#include "poppler.hpp"
#include "opencv.hpp"
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <filesystem>
#include <vector>
#include <cstdlib>

using std::string;
using std::vector;
namespace fs = std::filesystem;

class Viewport;
void scale_img_to_width(cv::Mat &image, int new_width);
void scale_img_to_height(cv::Mat &image, int new_height);
void scale_img_to_width(vector<cv::Mat> &images, int new_width);
void scale_img_to_height(vector<cv::Mat> &images, int new_height);
void pad_image_names(string dir);
void save_pages(string pdf_path, double scaled_dpi, bool keep_resolution, poppler::document *pdf);
double get_scaled_dpi(poppler::page *page, Viewport &vp); // returns dpi that will scale the pdf page to fit viewport width
string get_frames_dir(string pdf_dir);
string get_frame_name(int index);
string get_pdf_dir(string pdf_path);
string get_pdf_name(string pdf_path);
string get_page_name(int index, int pages);
cv::Mat get_long_image(int pages, string pdf_dir, Viewport &vp);
void make_frames_dir(string frames_dir);
void make_pdf_dir(string pdf_dir);
void generate_video(string pdf_path, string pdf_dir, Viewport &vp);
void generate_frames(string frames_dir, cv::Mat &long_image, poppler::document *pdf, Viewport &vp);
