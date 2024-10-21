#include "logic.hpp"
#include <opencv2/core/hal/interface.h>

using std::string;
// using std::vector;
namespace fs = std::filesystem;

// Input:
// pdf || dir of numbered images
//
// Default:
// pdf_to_video(sequence, full_res) 1sec, 1fps,
//
// Functions:
// Viewport create_viewport(keep_aspect_ratio)
//
// void pdf_to_video(scroll, scaled)
// void pdf_to_video(scroll, full_res)
// void pdf_to_video(sequence, scaled)
// void pdf_to_video(sequence, full_res)
//
// void images_to_video(scroll, scaled)
// void images_to_video(scroll, full_res)
// void images_to_video(sequence, scaled)
// void images_to_video(sequence, full_res)
//
// void pdf_to_images(scaled)
// void pdf_to_images(full_res)
//
// void pad_image_names(dir) : [ 1, 10, 100 ] -> [ 001, 010, 100 ]
// void scale_images_to_width(&vector<cv::Mat>, dst_width) keeps og aspect ratio
// void scale_images_to_height(&vector<cv::Mat>, dst_height) keeps og aspect ratio

int main(int argc, char **argv) {
    string pdf_path = "";
    string pdf_output_dir = "";
    cv::Mat viewport;

    if (argc != 2) {
        std::cerr << "not enough arguments" << std::endl;
        return 1;
    }

    pdf_path = argv[1];

    if (!fs::exists(pdf_path)) {
        std::cerr << "PDF path does not exist" << std::endl;
        return 1;
    }

    bool valid_path = (pdf_path.find(".pdf") == pdf_path.length() - 4);
    if (!valid_path) {
        std::cerr << "path is not a PDF file" << std::endl;
        return 1;
    }

    return 0;
}
