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

void scale_image_to_width(cv::Mat &img, int dst_width);
void scale_image_to_fit(cv::Mat& img, struct VP &vp);

double get_scaled_dpi_from_width(poppler::page *page, int width); // dpi fits page to vp width
double get_scaled_dpi(poppler::page *page, struct VP &vp); // dpi fits page in viewport

string get_frames_dir(string pdf_dir);
string get_frame_name(int index);
string get_scaled_dir(string img_seq_dir);
cv::Mat get_long_image(vector<cv::Mat> &imgs, struct VP &vp);
std::map<int, string> get_image_sequence_map(string dir);
vector<cv::Mat> get_images(std::map<int, string> img_map, Style style, struct VP &vp);
vector<cv::Mat> get_images(poppler::document *pdf, Style style, struct VP &vp);

void make_frames_dir(string frames_dir);
void make_scaled_dir(string scaled_dir);

string format_path(string str);

void generate_video(string frames_dir, string output, struct VP &vp);
void generate_scroll_frames(string frames_dir, int pages, cv::Mat long_image, struct VP &vp);
void generate_sequence_frames(string frames_dir, vector<cv::Mat> imgs, struct VP &vp);

bool delete_dir(string dir);
