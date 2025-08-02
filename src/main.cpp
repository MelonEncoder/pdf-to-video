#include "ptv.hpp"

int main(int argc, char **argv) {
    Config conf(argc, argv);

    // Start timer after accepting settings
    time_t start_time = time(NULL);

    std::cout << "Loading Images..." << std::endl;
    std::vector<cv::Mat> images = {};
    const std::vector<std::string> input_paths = conf.get_input_paths();
    const std::vector<std::string> input_types = conf.get_input_types();

    for (size_t i = 0; i < input_paths.size(); i++) {
        if (input_types[i] == "pdf") {
            add_pdf_images(input_paths[i], images, conf);
        } else if (input_types[i] == "dir") {
            add_dir_images(input_paths[i], images, conf);
        }
    }

    // Initializing video renderer
    std::string codec = conf.get_codec();
    cv::Size frame_size(conf.get_width(), conf.get_height());
    cv::VideoWriter video = cv::VideoWriter(conf.get_output(), cv::CAP_FFMPEG, cv::VideoWriter::fourcc(codec[0], codec[1], codec[2], codec[3]), conf.get_fps(), frame_size, true);

    std::cout << "Generating Video..." << std::endl;
    if (conf.get_style() == FRAMES) {
        generate_video_sequence(video, images, conf);
    } else if (conf.get_style() == UP ) {
        generate_video_scroll_up(video, images, conf);
    } else if (conf.get_style() == DOWN) {
        // generate_video_scroll_down(video, images, conf);
    } else if (conf.get_style() == LEFT) {
        generate_video_scroll_left(video, images, conf);
    } else if (conf.get_style() == RIGHT) {
        // generate_video_scroll_right(video, images, conf);
    }

    // Finish Video
    video.release();
    std::cout << "Finished Generating Video!" << std::endl;

    // Clean Up
    for (auto &img : images) {
        img.release();
    }

    // Time
    size_t duration = difftime(time(NULL), start_time);
    size_t minutes = duration / 60;
    size_t seconds = duration % 60;
    std::cout << "Time: " << minutes << "m " << seconds << "s" << std::endl;

    return 0;
}
