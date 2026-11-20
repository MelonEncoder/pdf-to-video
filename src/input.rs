//! Finding and loading inputs (PDF pages, images, GIF frames).
//!
//! Inputs are first *scanned*: every page/image is listed with its final size, reading only
//! file headers. Pixels are produced later, on demand, by the [`Library`], and dropped as
//! soon as no remaining part of the video needs them, so memory stays roughly constant no
//! matter how many pages there are.

use anyhow::{Context, Result, bail};
use image::codecs::gif::GifDecoder;
use image::imageops::{self, FilterType};
use image::{AnimationDecoder, ImageReader, RgbImage, RgbaImage};
use rayon::prelude::*;
use std::fs::File;
use std::io::BufReader;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use crate::cli::{Config, Input};
use crate::pdf::{open_pdf, page_order, page_size, render_page};
use crate::scale::target_size;
use crate::ui;
use hayro::hayro_syntax::Pdf;

/// Resize to exactly `width`x`height` (no-op if already that size).
pub fn resize_exact(img: RgbImage, width: u32, height: u32) -> RgbImage {
    if img.dimensions() == (width, height) {
        img
    } else {
        imageops::resize(&img, width, height, FilterType::Triangle)
    }
}

/// Where an item's pixels come from.
enum Source {
    Pdf {
        pdf: &'static Pdf,
        page: usize,
    },
    File(PathBuf),
    /// Already decoded (GIF frames), at native size.
    Memory(RgbImage),
}

/// One page or image of the video.
struct Item {
    source: Source,
    /// Final (scaled) size.
    width: u32,
    height: u32,
    /// The scaled pixels, while some part of the video still needs them.
    pixels: Mutex<Option<Arc<RgbImage>>>,
    /// How many encoder chunks still need this item; pixels are dropped at zero.
    needed_by: AtomicUsize,
}

/// All pages/images of the video in playback order, rendered on demand.
pub struct Library {
    items: Vec<Item>,
}

impl Library {
    pub fn len(&self) -> usize {
        self.items.len()
    }

    /// Final size of item `i`.
    pub fn size(&self, i: usize) -> (u32, u32) {
        (self.items[i].width, self.items[i].height)
    }

    /// The scaled pixels of item `i`, rendering/decoding them if needed. Threads asking for
    /// the same item wait for one render instead of duplicating it.
    pub fn get(&self, i: usize) -> Arc<RgbImage> {
        let item = &self.items[i];
        let mut pixels = item.pixels.lock().unwrap();
        if let Some(img) = pixels.as_ref() {
            return img.clone();
        }
        let (w, h) = (item.width, item.height);
        let img = Arc::new(match &item.source {
            Source::Pdf { pdf, page } => render_page(pdf, *page, w, h),
            Source::Memory(img) => resize_exact(img.clone(), w, h),
            Source::File(path) => match load_image(path) {
                Ok(img) => resize_exact(img, w, h),
                Err(e) => {
                    // The header read fine during scanning, so this is rare (a truncated
                    // or changed file). Show black rather than abort a long render.
                    ui::warn(format!(
                        "'{}' could not be decoded ({e:#}); showing black.",
                        path.display()
                    ));
                    RgbImage::new(w, h)
                }
            },
        });
        *pixels = Some(img.clone());
        img
    }

    /// Record that one more encoder chunk will use item `i`.
    pub fn retain(&self, i: usize) {
        self.items[i].needed_by.fetch_add(1, Ordering::Relaxed);
    }

    /// A chunk that used item `i` is done; free the pixels once nothing else needs them.
    pub fn release(&self, i: usize) {
        let item = &self.items[i];
        if item.needed_by.fetch_sub(1, Ordering::AcqRel) == 1 {
            *item.pixels.lock().unwrap() = None;
        }
    }

    /// A library of ready-made images (already at final size), for tests.
    #[cfg(test)]
    pub fn from_images(images: Vec<RgbImage>) -> Self {
        let items = images
            .into_iter()
            .map(|img| Item {
                width: img.width(),
                height: img.height(),
                source: Source::Memory(img),
                pixels: Mutex::new(None),
                needed_by: AtomicUsize::new(0),
            })
            .collect();
        Self { items }
    }
}

/// An item found while scanning, before the output resolution is known.
struct Found {
    source: Source,
    /// Native size: pixels, or points for PDF pages.
    native: (f64, f64),
    /// File it came from, for messages.
    origin: Arc<Path>,
}

/// List every page/image of every input, in playback order, then size them for the output.
pub fn scan(conf: &mut Config) -> Result<Library> {
    let mut found = Vec::new();
    for input in &conf.inputs {
        match input {
            Input::Pdf(path) => found.extend(scan_pdf(path, conf.reverse)?),
            Input::Dir(dir) => found.extend(scan_dir(dir, conf)?),
        }
    }
    let Some(first) = found.first() else {
        bail!("No pages or images were found, nothing to render.");
    };
    let (w, h) = first.native;
    conf.resolve_resolution(w.round() as u32, h.round() as u32, &first.origin);

    let items: Vec<Item> = found
        .into_iter()
        .map(|f| {
            let (width, height) =
                target_size(conf.style, f.native.0, f.native.1, conf.width, conf.height);
            Item {
                source: f.source,
                width,
                height,
                pixels: Mutex::new(None),
                needed_by: AtomicUsize::new(0),
            }
        })
        .collect();
    ui::print_done(format!(
        "Found {} page{}/image{}",
        items.len(),
        if items.len() == 1 { "" } else { "s" },
        if items.len() == 1 { "" } else { "s" },
    ));
    Ok(Library { items })
}

fn scan_pdf(path: &Path, reverse: bool) -> Result<Vec<Found>> {
    let pdf = open_pdf(path)?;
    let origin: Arc<Path> = path.into();
    let pages: Vec<Found> = page_order(pdf.pages().len(), reverse)
        .into_iter()
        .filter_map(|page| match page_size(pdf, page) {
            Some((w, h)) => Some(Found {
                source: Source::Pdf { pdf, page },
                native: (w as f64, h as f64),
                origin: origin.clone(),
            }),
            None => {
                ui::warn(format!(
                    "Page {} of '{}' has no size. Skipped.",
                    page + 1,
                    path.display()
                ));
                None
            }
        })
        .collect();
    if pages.is_empty() {
        ui::warn(format!("'{}' has no pages. Skipped.", path.display()));
    }
    Ok(pages)
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

/// Scan a numbered image sequence. Files are probed in parallel; order is preserved.
/// Unreadable files are skipped with a warning rather than aborting the whole video.
fn scan_dir(dir: &Path, conf: &Config) -> Result<Vec<Found>> {
    let paths = dir_image_paths(dir, conf.reverse)?;
    if paths.is_empty() {
        ui::warn(format!("'{}' contains no numbered images.", dir.display()));
    }
    let per_file: Vec<Vec<Found>> = paths
        .par_iter()
        .map(|path| {
            let scanned = if has_ext(path, "pdf") {
                scan_pdf(path, conf.reverse)
            } else if has_ext(path, "gif") {
                if conf.render_gifs {
                    gif_frames(path)
                } else {
                    ui::note(format!(
                        "'{}' skipped. Use --gif to render gif files as support is limited.",
                        path.display()
                    ));
                    Ok(Vec::new())
                }
            } else {
                image_header(path)
            };
            scanned.unwrap_or_else(|e| {
                ui::warn(format!(
                    "'{}' could not be read ({e:#}). Skipped.",
                    path.display()
                ));
                Vec::new()
            })
        })
        .collect();
    Ok(per_file.into_iter().flatten().collect())
}

/// An image file's size, from its header only.
fn image_header(path: &Path) -> Result<Vec<Found>> {
    let (w, h) = ImageReader::open(path)?
        .with_guessed_format()?
        .into_dimensions()?;
    Ok(vec![Found {
        source: Source::File(path.to_path_buf()),
        native: (w as f64, h as f64),
        origin: path.into(),
    }])
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

/// Each frame of a GIF as one item. GIF frames depend on the previous frame, so they're
/// decoded up front and kept in memory (GIF support is limited/small by design).
fn gif_frames(path: &Path) -> Result<Vec<Found>> {
    let file = BufReader::new(File::open(path)?);
    let decoder = GifDecoder::new(file).context("could not open GIF")?;
    let origin: Arc<Path> = path.into();
    decoder
        .into_frames()
        .map(|frame| {
            let img = flatten_on_black(frame.context("decoding GIF frame")?.buffer());
            Ok(Found {
                native: (img.width() as f64, img.height() as f64),
                source: Source::Memory(img),
                origin: origin.clone(),
            })
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

    #[test]
    fn pixels_are_freed_when_no_longer_needed() {
        let lib = Library::from_images(vec![RgbImage::new(4, 4)]);
        lib.retain(0);
        lib.retain(0);
        let img = lib.get(0);
        assert!(lib.items[0].pixels.lock().unwrap().is_some());
        lib.release(0);
        assert!(
            lib.items[0].pixels.lock().unwrap().is_some(),
            "still needed once"
        );
        lib.release(0);
        assert!(lib.items[0].pixels.lock().unwrap().is_none(), "freed");
        drop(img);
    }
}
