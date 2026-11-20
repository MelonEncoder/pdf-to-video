//! Loading inputs (image sequence directories, images, GIFs) into RGB frames.

use anyhow::{Context, Result};
use image::codecs::gif::GifDecoder;
use image::imageops::{self, FilterType};
use image::{AnimationDecoder, ImageReader, RgbImage, RgbaImage};
use std::fs::File;
use std::io::BufReader;
use std::path::{Path, PathBuf};

use crate::cli::{Config, Input};
use crate::pdf::add_pdf_images;
use crate::scale::target_size;
use crate::ui::{self, Progress};

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

fn add_dir_images(dir: &Path, out: &mut Vec<RgbImage>, conf: &mut Config) -> Result<()> {
    let paths = dir_image_paths(dir, conf.reverse)?;
    if paths.is_empty() {
        ui::warn(format!("'{}' contains no numbered images.", dir.display()));
        return Ok(());
    }
    let mut progress = Progress::new(format!("Loading {}", dir.display()), paths.len());

    for (i, path) in paths.iter().enumerate() {
        if has_ext(path, "pdf") {
            add_pdf_images(path, out, conf)?;
        } else if has_ext(path, "gif") {
            if conf.render_gifs {
                add_gif_images(path, out, conf)?;
            } else {
                ui::note(format!(
                    "'{}' skipped. Use --gif to render gif files as support is limited.",
                    path.display()
                ));
            }
        } else {
            match load_image(path) {
                Ok(img) => {
                    conf.resolve_resolution(img.width(), img.height(), path);
                    out.push(conform(img, conf));
                }
                Err(e) => ui::warn(format!(
                    "'{}' could not be read ({e:#}). Skipped.",
                    path.display()
                )),
            }
        }
        progress.set(i + 1);
    }
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

/// Add each frame of a GIF as one video frame.
fn add_gif_images(path: &Path, out: &mut Vec<RgbImage>, conf: &mut Config) -> Result<()> {
    let file =
        BufReader::new(File::open(path).with_context(|| format!("opening '{}'", path.display()))?);
    let decoder = GifDecoder::new(file)
        .with_context(|| format!("could not open GIF '{}'", path.display()))?;
    for frame in decoder.into_frames() {
        let frame = frame.with_context(|| format!("decoding GIF '{}'", path.display()))?;
        let img = flatten_on_black(frame.buffer());
        conf.resolve_resolution(img.width(), img.height(), path);
        out.push(conform(img, conf));
    }
    Ok(())
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
