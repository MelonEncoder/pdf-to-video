#ifndef PTVMACROS_HPP
#define PTVMACROS_HPP

#include <string>

#define HELP_TXT "\
ptv [options...]\n\
Help Options: \n\
   -h, --help                             :  show help options.\n\n\
Application Options: \n\
   [pdf_path]                             :  PDF file path. /home/usr/example.pdf\n\
   [img_sequence_dir]                     :  image sequence directory. /home/usr/example_seq/\n\
   -r <int> <int>                         :  set output resolution. use -1 to keep scale, default: 1920 1080 \n\
   -f <float>                             :  frames per second.\n\
   -s <float>                             :  seconds per page.\n\
   --format AVI,MP4,MKV,MOV               :  select encoding file format.\n\
   --scroll                               :  scrolls content (vertically) instead of slideshow.\n\
   --keep                                 :  don't delete directors created by program.\
"
#define DEFAULT_DPI 72.0

enum class Style {
    SCROLL,
    SEQUENCE
};

struct VP {
    int width;
    int height;
    double fps;
    double spp;
    std::string fmt;
};

#endif
