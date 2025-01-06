#ifndef PTV_HPP
#define PTV_HPP

#define HELP_TXT "\
ptv [options...]\n\
Help Options: \n\
   -h, --help                             :  show help options.\n\
Application Options: \n\
   [pdf_paths...]                         :  PDF file path. /home/usr/example.pdf\n\
   [img_sequence_dirs...]                 :  image sequence directory. /home/usr/example_seq/\n\
   -r <int> <int>                         :  set output resolution. use -1 to keep scale, default: 1280 720 \n\
   -f <float>                             :  frames per second.\n\
   -s <float>                             :  seconds per frame.\n\
   -d <float>                             :  duration in seconds. *NOTE: overides -s (or seconds per frame)*\n\
   -o [output_path]                       :  currently only support .mp4 files, leave blank for auto\n\
   --scroll                               :  scrolls content (vertically) instead of slideshow.\n\
"
#define DEFAULT_DPI 72.0

enum class Style {
    SCROLL,
    SEQUENCE
};

struct VP {
    float width;
    float height;
    float fps;
    float spp;
    float duration;
};

#endif
