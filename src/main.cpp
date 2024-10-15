#include "poppler.hpp"
#include "opencv.hpp"
#include <iostream>
#include <string>
#include <filesystem>

using std::string;
namespace fs = std::filesystem;

class Viewport {
    private:
        int w;
        int h;
    public:
        Viewport(int width, int height) {
            w = width;
            h = height;
        }
        Viewport() {
            w = 1920;
            h = 1080;
        }
        int width() {
            return w;
        }
        int height() {
            return h;
        }
        void set_width(int width) {
            w = width;
        }
        void set_height(int height) {
            h = height;
        }
};

void save_pdf_as_images(string pdf_path, Viewport &vp);
double get_dpi(poppler::page *page, Viewport &vp);
string get_frame_name(const int index);
string get_page_name(const int index, const int pages);
string get_pdf_dir(const string pdf_path);
void create_pdf_dir(const string pdf_dir);

int main(int argc, char **argv) {
    string pdf_path = "";
    string pdf_output_dir = "";
    Viewport viewport;

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

    save_pdf_as_images(pdf_path, viewport);

    return 0;
}

void save_pdf_as_images(string pdf_path, Viewport &vp) {
    poppler::document *pdf = poppler::document::load_from_file(pdf_path);
    int page_count = pdf->pages();
    auto renderer = poppler::page_renderer();
    string image_type = "jpg";
    string pdf_dir = get_pdf_dir(pdf_path);

    create_pdf_dir(pdf_dir);

    for (int i = 0; i < page_count; i++) {
        string pg_name = get_page_name(i, page_count);
        string path = pdf_dir + pg_name + "." + image_type;

        poppler::page *page = pdf->create_page(i);
        double dpi = get_dpi(page, vp);

        poppler::image tmp_img = renderer.render_page(page, dpi, dpi);
        tmp_img.save(path, image_type);

        std::cout << std::to_string(i + 1) + "/" + std::to_string(page_count) << std::endl;
    }
}

// returns dpi that will scale the pdf page to fit viewport width
double get_dpi(poppler::page *page, Viewport &vp) {
    double dpi = 0.0;
    auto rect = page->page_rect(poppler::media_box);

    dpi = ((double)vp.width() * 72.0) / rect.width();

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
