# PDF to Video
This is a CLI tool that is written in C++ and is used to convert a `.pdf` to `.mp4` file. It can also convert image sequences into videos.

## Features
- Convert PDF to video\
`ptv example.pdf` --> `example.mp4`
- Convert image sequence to video\
`ptv img_seq/` --> `img_seq.mp4`
- Video animation styles:
	- sequence (on by default)
	- vertical scroll
- Chain PDFs or Image Sequences together. (1.pdf + 2.pdf -> output.mp4)
- Change output settings (fps, resolution, duration, output path)

### Flags
`-r <int> <int>             :  set output resolution. use -1 to keep scale, default: 1280 720`\
`-f <float>                 :  frames per second.`\
`-s <float>                 :  seconds per frame.`\
`-d <float>                 :  duration in seconds. NOTE: overides -s`\
`-o [output_path]           :  currently only support .mp4 files, leave blank for auto`\
`--scroll`

## Dependencies
1. [poppler](https://poppler.freedesktop.org/)>=25.01.0 - pdf to image
2. [opencv](https://opencv.org/)>=4.10.0 - image manipulation
3. [ffmpeg](https://ffmpeg.org/)>=7.0.0 - video rendering backend (not a build dependency)

## Build System
I'm using the [Meson](https://mesonbuild.com/) build system for my project. It's simple, modern, and easy to learn.
