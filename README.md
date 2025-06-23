# About PTV
This is a CLI tool used to convert **PDF** files and numbered **image sequences** into a **video** (.mp4) file.

## Features
- Convert .pdf files to video
- Convert image_sequence/ directories to video
- Animation styles:
	- sequence (each image is a frame, on by default)
	- scroll
- Chain PDFs and/or Image Sequences together. Ex:
```
ptv 1.pdf 2.pdf seq/ --> output.mp4
```
- Change output settings (fps, resolution, duration, output path)
- Minimal support for .gif files

### Flags
```
Help Options:
   -h, --help                             :  show this help text.
Application Options:
   [pdf_paths...]                         :  PDF file path. /home/usr/example.pdf
   [img_sequence_dirs...]                 :  image sequence directory. /home/usr/example_seq/
   -r <int>x<int>                         :  set output resolution. Use 0 to preserve resolution of original content, default: 1280x720
   -f <float>                             :  frames per second.
   -s <float>                             :  seconds per frame.
   -d <float>                             :  duration in seconds. NOTE: overides -s (seconds per frame)
   -o [output_path]                       :  currently only support .mp4 files, leave blank for auto output
   -a [Up|Down|Left|Right]                :  scrolls content instead of making each page a frame (like a slideshow).
   --gif                                  :  render .gif files in image sequences\n\
   --reverse-seq                          :  load numbered imgs from dir in decending order, larger # to smaller #
```
## Dependencies
1. [poppler](https://poppler.freedesktop.org/) >= 25.01.0 - pdf to image
2. [opencv](https://opencv.org/) >= 4.10.0 - image manipulation
3. [ffmpeg](https://ffmpeg.org/) >= 7.0.0 - video rendering backend (not a build dependency)

## Build System
I'm using the [Meson](https://mesonbuild.com/) build system for my project. It's simple, modern, and easy to learn.
