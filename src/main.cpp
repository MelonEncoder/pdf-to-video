#include "cpp/poppler-global.h"
#include "poppler.hpp"
#include "opencv.hpp"
#include "logic.hpp"
#include "ptv.hpp"
#include <cctype>
#include <filesystem>
#include <libavformat/avformat.h>

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
    int width = 1920;
    int height = 1080;
    double fps = 1.0;
    double spp = 1.0;
    string vid_fmt = "MP4";
    Style style = Style::SEQUENCE;
    string pdf_path = "";
    string img_seq_dir = "";
    bool keep = false;
    poppler::document *pdf;

    if (argc < 2) {
        std::cerr << HELP_TXT << std::endl;
        return 1;
    }

    // CLI
    for (int i = 1; i < argc; i++) {
        string arg = string(argv[i]);
        if (arg == "-h" || arg == "--help") {
            std::cout << HELP_TXT << std::endl;
            return 0;
        } else if (arg == "-r") {
            i++;
            width = std::stoi(argv[i]);
            i++;
            height = std::stoi(argv[i]);
        } else if (arg == "-p") {
            i++;
            string path = argv[i];
            if (img_seq_dir != "") {
                std::cerr << "Error: Cannot convert image directory and PDF. Choose one." << std::endl;
            }
            if (!fs::exists(path)) {
                std::cerr << "Error: PDF path does not exist." << std::endl;
            }
            if (fs::is_directory(path)) {
                std::cerr << "Error: PDF path is a directory. Try again." << std::endl;
                return 1;
            }
            if (path.substr(path.length() - 4, 4) != ".pdf") {
                std::cout << "Error: PDF path not a .pdf file" << std::endl;
                return 1;
            }
            pdf_path = argv[i];
        } else if (arg == "-i") {
            i++;
            string dir = argv[i];
            if (pdf_path != "") {
                std::cerr << "Error: Cannot convert image directory and PDF. Choose one." << std::endl;
            }
            if (!fs::exists(dir)) {
                std::cerr << "Error: dir path does not exist." << std::endl;
            }
            if (!fs::is_directory(dir)) {
                std::cerr << "Error: dir path is not a directory. Try again." << std::endl;
                return 1;
            }
            img_seq_dir = argv[i];
        } else if (arg == "-f") {
            i++;
            fps = std::stof(argv[i]);
        } else if (arg == "-s") {
            i++;
            spp = std::stof(argv[i]);
        } else if (arg == "--format") {
            i++;
            vid_fmt = argv[i];
            if (vid_fmt != "MP4" && vid_fmt != "AVI" && vid_fmt != "MOV") {
                std::cerr << "Error: Video format not currently supported." << std::endl;
                return 1;
            }
        } else if (arg == "--style") {
            i++;
            string s = argv[i];
            if (s != "SCROLL" && s != "SEQUENCE") {
                std::cerr << "Error: style argument is not valid." << std::endl;
                return 1;
            }
            if (s == "SCROLL") {
                style = Style::SCROLL;
            } else if (s == "SEQUENCE") {
                style = Style::SEQUENCE;
            }
        } else if (arg == "--keep") {
            keep = true;
        } else {
            std::cerr << "Error: unknown argument detected: " << argv[i] << std::endl;
            return 1;
        }
    }
    if (pdf_path != "") {
        std::cout << "PDF Path: " << pdf_path << std::endl;
    }
    if (img_seq_dir != "") {
        std::cout << "Image Sequence Dir: " << img_seq_dir << std::endl;
    }
    std::cout << "Width: " << width << "\nHeight: " << height << "" << std::endl;
    std::cout << "FPS: " << fps << std::endl;
    std::cout << "SPP: " << spp << std::endl;
    std::cout << "Format: " << vid_fmt << std::endl;
    if (style == Style::SCROLL) {
        std::cout << "Style: SCROLL" << std::endl;
    } else {
        std::cout << "Style: SEQUENCE" << std::endl;
    }

    // Safety check
    string check;
    std::cout << "Are these values correct: [Y/n]" << std::endl;
    std::getline(std::cin, check);
    if (!check.empty() && check != "Y" && check != "y") {
        return 1;
    }
    std::cin.clear();

    // lowercase video format
    for (auto &c : vid_fmt) {
        c = std::tolower(c);
    }

    // creates the vp
    struct VP vp = {
        width,
        height,
        fps,
        spp,
        vid_fmt
    };

    // Application Logic
    // converting pdf to video
    if (pdf_path != "") {
        string pdf_dir = get_pdf_dir(pdf_path);
        string frames_dir = get_frames_dir(pdf_dir);

        make_pdf_dir(pdf_dir);
        make_frames_dir(frames_dir);

        pdf = poppler::document::load_from_file(pdf_path);
        int pages = pdf->pages();
        poppler::rectf rect = pdf->create_page(0)->page_rect(poppler::media_box);

        // std::cout << "RECT_W: " << rect.width() << std::endl;
        // std::cout << "RECT_H: " << rect.height() << std::endl;
        if (width == -1) {
            vp.width = rect.width();
            // std::cout << "w: " << vp.width << std::endl;
        }
        if (height == -1) {
            vp.height = rect.height();
            // std::cout << "h: " << vp.height << std::endl;
        }

        pdf_to_images(pdf_dir, pdf, vp, style);

        if (style == Style::SCROLL) {
            cv::Mat long_img = get_long_image(pages, pdf_dir, vp);
            generate_scroll_frames(frames_dir, pages, long_img, vp);
        } else if (style == Style::SEQUENCE) {
            vector<cv::Mat> images = get_images(pdf_dir);
            generate_sequence_frames(frames_dir, pages, images, vp);
        }

        string output = pdf_path.substr(0, pdf_path.length() - 4) + "." + vp.fmt;
        generate_video(pdf_dir, frames_dir, output, vp);

        if (!keep) {
            delete_dir(frames_dir);
            delete_dir(pdf_dir);
        }

        std::cout << "Finished!" << std::endl;
    }

    // converting image sequence to video
    if (img_seq_dir != "") {

    }


    return 0;
}
