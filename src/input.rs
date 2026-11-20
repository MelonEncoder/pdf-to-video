//! Loading inputs (image sequence directories, images, GIFs) into RGB frames.

use anyhow::{Context, Result};
use image::codecs::gif::GifDecoder;
use image::imageops::{self, FilterType};
use image::{AnimationDecoder, ImageDecoder, ImageReader, RgbImage, RgbaImage};
use rayon::prelude::*;
use std::fs::File;
use std::io::BufReader;
use std::path::{Path, PathBuf};

use crate::cli::{Config, Input};
use crate::pdf::{add_pdf_images, first_page_size, open_pdf, render_pages};
use crate::scale::target_size;
use crate::ui::{self, SharedProgress};

/// Load every input in order, returning frames already scaled for the configured style.
pub fn load_all(conf: &mut Config) -> Result<Vec<RgbImage>> {
    let mut images = Vec::new();
    for input in conf.inputs.clone() {
        match input {
            Input::Pdf(p) => add_pdf_images(&p, &mut images, conf)?,
            Input::Dir(p) => add_dir_images(&p, &mut images, conf)?,
        }
    }
    Ok(images)
}

/// Resize an image to its exact target size for the configured style and resolution.
pub fn conform(img: RgbImage, conf: &Config) -> RgbImage {
    let (tw, th) = target_size(
        conf.style,
        img.width(),
        img.height(),
        conf.width,
        conf.height,
    );
    if (tw, th) == img.dimensions() {
        img
    } else {
        imageops::resize(&img, tw, th, FilterType::Triangle)
    }
}

/// Numeric sort key: the leading digits of the file name (`012_title.png` -> 12).
fn sort_key(name: &str) -> Option<u64> {
    let digits: &str = &name[..name.bytes().take_while(u8::is_ascii_digit).count()];
    digits.parse().ok()
}

/// Files in `dir` (not subdirectories) whose names start with a number, in numeric order.
pub fn dir_image_paths(dir: &Path, reverse: bool) -> Result<Vec<PathBuf>> {
    let mut entries: Vec<(u64, String, PathBuf)> = Vec::new();
    for entry in
        std::fs::read_dir(dir).with_context(|| format!("reading directory '{}'", dir.display()))?
    {
        let entry = entry?;
        let path = entry.path();
        if path.is_dir() {
            continue;
        }
        let name = entry.file_name().to_string_lossy().into_owned();
        match sort_key(&name) {
            Some(key) => entries.push((key, name, path)),
            None => ui::note(format!(
                "'{}' skipped. File name is not a number.",
                path.display()
            )),
        }
    }
    // Tie-break on name so duplicates like `1.png` / `01.png` have a stable order.
    entries.sort_by(|a, b| (a.0, &a.1).cmp(&(b.0, &b.1)));
    if reverse {
        entries.reverse();
    }
    Ok(entries.into_iter().map(|(_, _, p)| p).collect())
}

fn has_ext(path: &Path, ext: &str) -> bool {
    path.extension()
        .is_some_and(|e| e.eq_ignore_ascii_case(ext))
}

/// What a file in an image sequence directory is, and how to load it.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Kind {
    Pdf,
    Gif,
    /// A GIF without `--gif`: skipped with a note.
    SkippedGif,
    Image,
}

fn kind(path: &Path, conf: &Config) -> Kind {
    if has_ext(path, "pdf") {
        Kind::Pdf
    } else if has_ext(path, "gif") {
        if conf.render_gifs {
            Kind::Gif
        } else {
            Kind::SkippedGif
        }
    } else {
        Kind::Image
    }
}

/// Native size of a file, reading only headers where possible.
fn native_size(path: &Path, conf: &Config) -> Option<(u32, u32)> {
    match kind(path, conf) {
        Kind::Pdf => open_pdf(path)
            .ok()
            .and_then(|pdf| first_page_size(&pdf, conf.reverse)),
        Kind::Gif => {
            let file = BufReader::new(File::open(path).ok()?);
            GifDecoder::new(file).ok().map(|d| d.dimensions())
        }
        Kind::Image => ImageReader::open(path)
            .ok()?
            .with_guessed_format()
            .ok()?
            .into_dimensions()
            .ok(),
        Kind::SkippedGif => None,
    }
}

/// Load one file of a sequence as zero or more frames. Unreadable files are skipped with
/// a warning rather than aborting the whole video.
fn load_file(path: &Path, conf: &Config) -> Vec<RgbImage> {
    let loaded = match kind(path, conf) {
        Kind::Pdf => open_pdf(path).map(|pdf| render_pages(&pdf, path, conf, None)),
        Kind::Gif => gif_frames(path, conf),
        Kind::Image => load_image(path).map(|img| vec![conform(img, conf)]),
        Kind::SkippedGif => {
            ui::note(format!(
                "'{}' skipped. Use --gif to render gif files as support is limited.",
                path.display()
            ));
            Ok(Vec::new())
        }
    };
    loaded.unwrap_or_else(|e| {
        ui::warn(format!(
            "'{}' could not be read ({e:#}). Skipped.",
            path.display()
        ));
        Vec::new()
    })
}

/// Load a numbered image sequence, decoding and scaling files in parallel.
fn add_dir_images(dir: &Path, out: &mut Vec<RgbImage>, conf: &mut Config) -> Result<()> {
    let paths = dir_image_paths(dir, conf.reverse)?;
    if paths.is_empty() {
        ui::warn(format!("'{}' contains no numbered images.", dir.display()));
        return Ok(());
    }
    // Every file is scaled to the output resolution, so settle it (from the first readable
    // file) before loading anything in parallel.
    if (conf.width == 0 || conf.height == 0)
        && let Some(((w, h), path)) = paths
            .iter()
            .find_map(|p| native_size(p, conf).map(|size| (size, p)))
    {
        conf.resolve_resolution(w, h, path);
    }
    let conf: &Config = conf;
    let progress = SharedProgress::new(format!("Loading {}", dir.display()), paths.len());
    let per_file: Vec<Vec<RgbImage>> = paths
        .par_iter()
        .map(|path| {
            let frames = load_file(path, conf);
            progress.tick();
            frames
        })
        .collect();
    out.extend(per_file.into_iter().flatten());
    Ok(())
}

fn load_image(path: &Path) -> Result<RgbImage> {
    Ok(ImageReader::open(path)?
        .with_guessed_format()?
        .decode()?
        .to_rgb8())
}

/// Composite RGBA over black (transparent GIF areas become black, like the video background).
fn flatten_on_black(rgba: &RgbaImage) -> RgbImage {
    RgbImage::from_fn(rgba.width(), rgba.height(), |x, y| {
        let [r, g, b, a] = rgba.get_pixel(x, y).0;
        let a = a as u16;
        image::Rgb([
            (r as u16 * a / 255) as u8,
            (g as u16 * a / 255) as u8,
            (b as u16 * a / 255) as u8,
        ])
    })
}

/// Each frame of a GIF as one video frame.
fn gif_frames(path: &Path, conf: &Config) -> Result<Vec<RgbImage>> {
    let file = BufReader::new(File::open(path)?);
    let decoder = GifDecoder::new(file).context("could not open GIF")?;
    decoder
        .into_frames()
        .map(|frame| {
            let frame = frame.context("decoding GIF frame")?;
            Ok(conform(flatten_on_black(frame.buffer()), conf))
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sort_key_uses_leading_digits() {
        assert_eq!(sort_key("12.png"), Some(12));
        assert_eq!(sort_key("007_intro.jpg"), Some(7));
        assert_eq!(sort_key("img12.png"), None);
        assert_eq!(sort_key(".hidden"), None);
    }

    #[test]
    fn dir_listing_is_numeric_and_reversible() {
        let dir = std::env::temp_dir().join(format!("ptv-test-{}", std::process::id()));
        std::fs::create_dir_all(dir.join("sub")).unwrap();
        for name in ["10.png", "2.png", "1.png", "notes.txt", "03.jpg"] {
            std::fs::write(dir.join(name), b"").unwrap();
        }
        let names = |v: Vec<PathBuf>| -> Vec<String> {
            v.iter()
                .map(|p| p.file_name().unwrap().to_string_lossy().into_owned())
                .collect()
        };
        assert_eq!(
            names(dir_image_paths(&dir, false).unwrap()),
            ["1.png", "2.png", "03.jpg", "10.png"]
        );
        assert_eq!(
            names(dir_image_paths(&dir, true).unwrap()),
            ["10.png", "03.jpg", "2.png", "1.png"]
        );
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn transparent_pixels_become_black() {
        let mut rgba = RgbaImage::new(2, 1);
        rgba.put_pixel(0, 0, image::Rgba([255, 255, 255, 0]));
        rgba.put_pixel(1, 0, image::Rgba([200, 100, 50, 255]));
        let rgb = flatten_on_black(&rgba);
        assert_eq!(rgb.get_pixel(0, 0).0, [0, 0, 0]);
        assert_eq!(rgb.get_pixel(1, 0).0, [200, 100, 50]);
    }
}
