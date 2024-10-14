#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-image.h>
#include <poppler/cpp/poppler-page-renderer.h>
#include <poppler/cpp/poppler-destination.h>
#include <iostream>
#include <string>
#include <filesystem>

int main(int argc, char **argv) {

    if (argc != 2) {
        std::cerr << "not enough arguments" << std::endl;
        return -1;
    }
    std::string pdfPath = argv[1];


    if (!std::filesystem::exists(pdfPath)) {
        std::cerr << "PDF path does not exist" << std::endl;
        return -1;
    }

    bool validPath = (pdfPath.find(".pdf") == pdfPath.length() - 4);
    if (!validPath) {
        std::cerr << "path is not a PDF file" << std::endl;
        return -1;
    }

    poppler::document *pdf = poppler::document::load_from_file(pdfPath);

    std::cout << pdf->pages() << std::endl;

    poppler::page *page0 = pdf->create_page(0);
    poppler::page *page1 = pdf->create_page(1);


    auto renderer = poppler::page_renderer();

    poppler::image page_1 = renderer.render_page(page0);
    poppler::image page_2 = renderer.render_page(page1);

    page_1.save("../hello.png", "png", 72);

    return 0;
}
