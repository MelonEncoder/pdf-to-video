#include "poppler.hpp"
#include "opencv.hpp"
#include <iostream>
#include <opencv2/core/hal/interface.h>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <string>
#include <filesystem>

using std::string;
namespace fs = std::filesystem;

// void save_pdf_as_images(string pdf_path, cv::Mat &vp);
// double get_dpi(poppler::page *page, cv::Mat &vp);
// string get_frame_name(const int index);
// string get_page_name(const int index, const int pages);
// string get_pdf_dir(const string pdf_path);
// void create_pdf_dir(const string pdf_dir);
// void create_image();

class PTV {
    string pdf_path;
    string pdf_dir;
    cv::Mat vp;


    void save_pdf_as_images() {
        poppler::document *pdf = poppler::document::load_from_file(pdf_path);
        int page_count = pdf->pages();
        auto renderer = poppler::page_renderer();
        string image_type = "jpg";
        string pdf_dir = get_pdf_dir(pdf_path);

        for (int i = 0; i < page_count; i++) {
            string pg_name = get_page_name(i, page_count);
            string path = pdf_dir + pg_name + "." + image_type;

            poppler::page *page = pdf->create_page(i);
            double dpi = get_dpi(page);

            poppler::image tmp_img = renderer.render_page(page, dpi, dpi);
            tmp_img.save(path, image_type);

            std::cout << std::to_string(i + 1) + "/" + std::to_string(page_count) << std::endl;
        }
    }

    void create_pdf_frames() {

    }

    // returns dpi that will scale the pdf page to fit viewport width
    double get_dpi(poppler::page *page) {
        double dpi = 0.0;
        auto rect = page->page_rect(poppler::media_box);

        dpi = ((double)vp.cols * 72.0) / rect.width();

        return dpi;
    }

    string get_pdf_dir(const string pdf_path) {
        string pdf_dir = pdf_path.substr(0, pdf_path.length() - 4) + "_" + pdf_path.substr(pdf_path.length() - 3) + "/";
        return pdf_dir;
    }

    void create_pdf_dir(const string pdf_dir) {
        if (!fs::exists(pdf_dir)) {
            fs::create_directory(pdf_dir);
            std::cout << "Created PDF directory." << std::endl;
        } else {
            std::cout << "PDF directory already exists." << std::endl;
        }
    }

    string get_frame_name(const int index) {
        if (index < 10) {
            return "0000" + std::to_string(index);
        } else if (index < 100) {
            return "000" + std::to_string(index);
        } else if (index < 1000) {
            return "00" + std::to_string(index);
        } else if (index < 10000) {
            return "0" + std::to_string(index);
        } else if (index < 100000) {
            return std::to_string(index);
        }
        return "";
    }

    string get_page_name(const int index, const int pages) {
        if (pages < 10) {
            return std::to_string(index);
        } else if (pages < 100) {
            if (index < 10) {
                return "0" + std::to_string(index);
            } else {
                return std::to_string(index);
            }
        } else if (pages < 1000) {
            if (index < 10) {
                return "00" + std::to_string(index);
            } else if (index < 100) {
                return "0" + std::to_string(index);
            } else {
                return std::to_string(index);
            }
        } else if (pages < 10000) {
            if (index < 10) {
                return "000" + std::to_string(index);
            } else if (index < 100) {
                return "00" + std::to_string(index);
            } else if (index < 1000) {
                return "0" + std::to_string(index);
            } else {
                return std::to_string(index);
            }
        } else {
            if (index < 10) {
                return "0000" + std::to_string(index);
            } else if (index < 100) {
                return "000" + std::to_string(index);
            } else if (index < 1000) {
                return "00" + std::to_string(index);
            } else if (index < 10000) {
                return "0" + std::to_string(index);
            } else if (index < 100000) {
                return std::to_string(index);
            }
        }
        return "";
    }

    public:
        PTV(string pdf_path_, int width, int height) {
            pdf_path = pdf_path_;
            pdf_dir = get_pdf_dir(pdf_path_);
            vp = cv::Mat(height, width, CV_8SC3);

            create_pdf_dir(pdf_dir);
        }

        void convert() {
            save_pdf_as_images();
            create_pdf_frames();
        }
};

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

    PTV ptv = PTV(pdf_path, 1920, 1080);

    ptv.convert();

    return 0;
}

// now make all pages in pdf to one cv::Mat (img)
// then make all the frames for the animation
// use ffmpeg cli (for right now) to export img sequence as video.
void create_image() {
    cv::Mat image1 = cv::imread("../test_pdf/00.jpg", cv::IMREAD_COLOR);
    cv::Mat image2 = cv::imread("../test_pdf/01.jpg", cv::IMREAD_COLOR);

    int totalRows = image1.rows + image2.rows;

    // Mat(rows, cols, _);
    cv::Mat longImage(totalRows, image1.cols, CV_8UC3);

    // ROI = Region of Interest
    // Rect(x, y, width, height);
    cv::Rect roi1(0, 0, image1.cols, image1.rows);
    cv::Rect roi2(0, image1.rows, image1.cols, image2.rows);

    image1.copyTo(longImage(roi1));
    image2.copyTo(longImage(roi2));

    cv::imshow("help me", longImage);

    cv::imwrite("../long.png", longImage);
    cv::waitKey(0);
}
