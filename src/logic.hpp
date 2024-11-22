#include "poppler.hpp"
#include "opencv.hpp"
#include "ptv.hpp"
#include <opencv2/core.hpp>
#include <opencv2/core/hal/interface.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <cstdlib>

using std::string;
using std::vector;

// void pdf_loading_test();

void scale_images_to_width(vector<cv::Mat> &images, int dst_width);
void scale_images_to_height(vector<cv::Mat> &images, int dst_height);
void scale_images_to_fit(vector<cv::Mat> &images, struct VP &vp);

void pdf_to_images(string pdf_dir, poppler::document *pdf, struct VP &vp, Style style);

double get_scaled_dpi_from_width(poppler::page *page, int width); // dpi fits page to vp width
double get_scaled_dpi(poppler::page *page, struct VP &vp); // dpi fits page in viewport

string get_frames_dir(string pdf_dir);
string get_frame_name(int index);
string get_pdf_dir(string pdf_path);
string get_pdf_name(string pdf_path);
string get_scaled_dir(string img_seq_dir);
string get_page_name(int index, int pages);
cv::Mat get_long_image(int pages, string images_dir, struct VP &vp);
cv::Mat get_long_image(int pages, vector<cv::Mat> &images, struct VP &vp);
vector<cv::Mat> get_images(string dir);

void make_frames_dir(string frames_dir);
void make_scaled_dir(string scaled_dir);
void make_pdf_dir(string pdf_dir);

string format_path(string str);

void generate_video(string frames_dir, string output, struct VP &vp);
void generate_scroll_frames(string frames_dir, int pages, cv::Mat long_image, struct VP &vp);
void generate_sequence_frames(string frames_dir, int pages, vector<cv::Mat> images, struct VP &vp);

bool delete_dir(string dir);
