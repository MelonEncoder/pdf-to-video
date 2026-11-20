# About PTV
This is a CLI tool used to convert **PDF** files and numbered **image sequences** into a **video** (.mp4) file.

## Features
- Convert .pdf files to video
- Convert image_sequence/ directories to video (numbered files: `1.png`, `2.png`, `010_title.jpg`, ...)
- Animation styles:
	- sequence (each image is shown for `-s` seconds, on by default)
	- scroll Up, Down, Left or Right
- Chain PDFs and/or Image Sequences together. Ex:
```
ptv 1.pdf 2.pdf seq/ --> output.mp4
```
- Change output settings (fps, resolution, duration, output path)
- Minimal support for .gif files

### Flags
```
Inputs:
  <path.pdf>                  PDF file(s) to render.
  <sequence_dir/>             Directory of numbered images.

Options:
  -r <int>x<int>              Output resolution. 0 preserves original. (default: 1280x720)
  -f <float>                  Frames per second. (default: 1)
  -s <float>                  Seconds each page/image is shown. (default: 1)
  -d <float>                  Duration of video in seconds. (overrides -s)
  -o <output_path>            Output file path. (.mp4 only, auto-named if blank)
  -a <Up|Down|Left|Right>     Scroll content continuously instead of per-page frames.
  --gif                       Render .gif files found in image sequences.
  --rev                       Reverse order: PDF pages last-to-first, images by descending number.
  -y, --yes                   Skip the confirmation prompt.
  -h, --help                  Show this help text.
```

## Building
PTV is written in Rust. Everything it needs is compiled into the binary, so system
updates (OpenCV, Poppler, FFmpeg, ...) can't break it and nothing needs to be installed
to run it.

```
cargo build --release
./target/release/ptv --help
```

Requires a Rust toolchain ([rustup](https://rustup.rs/)) and a C/C++ compiler (used to
build the bundled OpenH264 encoder).

## Dependencies
All are fetched and pinned by Cargo (`Cargo.lock`):
1. [hayro](https://github.com/LaurenzV/hayro) - PDF rendering (pure Rust)
2. [image](https://github.com/image-rs/image) - image decoding and resizing (pure Rust)
3. [openh264](https://github.com/ralfbiedert/openh264-rs) - H.264 encoding (Cisco's OpenH264, compiled from source and statically linked)
4. [mp4](https://github.com/alfg/mp4-rust) - MP4 container writing (pure Rust)
5. [clap](https://github.com/clap-rs/clap) - argument parsing
