#include "logic.hpp"

using std::string;
// using std::vector;
namespace fs = std::filesystem;

// Input:
// pdf || dir of numbered images
//
// Default:
// output pdf_to_video(sequence, full_res) 1sec, 1fps,
//
// Functions:
// create_viewport(keep_aspect_ratio)
//
// pdf_to_video(scroll, scaled)
// pdf_to_video(scroll, full_res)
// pdf_to_video(sequence, scaled)
// pdf_to_video(sequence, full_res)
//
// images_to_video(scroll, scaled)
// images_to_video(scroll, full_res)
// images_to_video(sequence, scaled)
// images_to_video(sequence, full_res)
//
// pdf_to_images(scaled)
// pdf_to_images(full_res)
//
// pad_image_names() : [ 1, 10, 100 ] -> [ 001, 010, 100 ]

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

// void to_video() {
//     save_pages(false);
//     generate_frames();
//     generate_video();
// }

// void to_images() {
//     save_pages(true);
// }
