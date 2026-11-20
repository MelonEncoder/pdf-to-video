//! PDF page rendering via hayro (pure Rust), parallelised across pages.

use anyhow::{Context, Result, anyhow};
use hayro::hayro_interpret::InterpreterSettings;
use hayro::hayro_syntax::Pdf;
use hayro::vello_cpu::color::palette::css::WHITE;
use hayro::{PixmapSettings, RenderCache, RenderSettings, render};
use image::RgbImage;
use rayon::prelude::*;
use std::path::Path;
use std::sync::Arc;

use crate::cli::Config;
use crate::input::conform;
use crate::scale::style_scale;
use crate::ui::{self, SharedProgress};

/// Largest pixmap side hayro can render (its buffers are sized with u16).
const MAX_SIDE: f64 = u16::MAX as f64;

pub fn open_pdf(path: &Path) -> Result<Pdf> {
    let data = std::fs::read(path).with_context(|| format!("reading '{}'", path.display()))?;
    Pdf::new(Arc::new(data))
        .map_err(|e| anyhow!("'{}' could not be opened as a PDF: {e:?}", path.display()))
}

/// Page indices in playback order (last-to-first with `--rev`).
fn page_order(n: usize, reverse: bool) -> Vec<usize> {
    if reverse {
        (0..n).rev().collect()
    } else {
        (0..n).collect()
    }
}

/// Size in points (1pt = 1px at 72 DPI) of a page, after rotation and cropping.
fn page_size(pdf: &Pdf, idx: usize) -> Option<(f32, f32)> {
    let (w, h) = pdf.pages()[idx].render_dimensions();
    (w > 0.0 && h > 0.0).then_some((w, h))
}

/// Native size of the first page (in playback order) that has one.
pub fn first_page_size(pdf: &Pdf, reverse: bool) -> Option<(u32, u32)> {
    page_order(pdf.pages().len(), reverse)
        .into_iter()
        .find_map(|i| page_size(pdf, i))
        .map(|(w, h)| (w.round() as u32, h.round() as u32))
}

/// Render one page scaled for the configured style. `None` (with a warning) if it can't be.
fn render_page<'a>(
    pdf: &'a Pdf,
    idx: usize,
    cache: &RenderCache<'a>,
    conf: &Config,
    path: &Path,
) -> Option<RgbImage> {
    let Some((pw, ph)) = page_size(pdf, idx) else {
        ui::warn(format!(
            "Page {} of '{}' has no size. Skipped.",
            idx + 1,
            path.display()
        ));
        return None;
    };
    let mut scale = style_scale(conf.style, pw as f64, ph as f64, conf.width, conf.height);
    // Very long pages in scroll mode could exceed the renderer's limit; render as large as
    // allowed and let `conform` stretch the rest of the way.
    scale = scale.min(MAX_SIDE / pw as f64).min(MAX_SIDE / ph as f64);

    let pixmap = render(
        &pdf.pages()[idx],
        cache,
        &InterpreterSettings::default(),
        &RenderSettings::default(),
        &PixmapSettings {
            x_scale: scale as f32,
            y_scale: scale as f32,
            bg_color: WHITE,
        },
    );
    let (w, h) = (pixmap.width() as u32, pixmap.height() as u32);
    // Opaque white background, so premultiplied RGBA == straight RGBA; just drop alpha.
    let rgb: Vec<u8> = pixmap
        .data_as_u8_slice()
        .as_chunks::<4>()
        .0
        .iter()
        .flat_map(|&[r, g, b, _]| [r, g, b])
        .collect();
    match RgbImage::from_raw(w, h, rgb).filter(|_| w > 0 && h > 0) {
        Some(img) => Some(conform(img, conf)),
        None => {
            ui::warn(format!(
                "Page {} of '{}' rendered empty. Skipped.",
                idx + 1,
                path.display()
            ));
            None
        }
    }
}

/// Render all pages in playback order, in parallel. The output resolution must already be
/// resolved (see [`first_page_size`]).
pub fn render_pages(
    pdf: &Pdf,
    path: &Path,
    conf: &Config,
    progress: Option<&SharedProgress>,
) -> Vec<RgbImage> {
    let order = page_order(pdf.pages().len(), conf.reverse);
    // Contiguous runs of pages per task, so each task can reuse one render cache (fonts,
    // glyphs) — the cache isn't thread-safe. A few runs per thread balances uneven pages.
    let run = order
        .len()
        .div_ceil(rayon::current_num_threads() * 4)
        .max(1);
    order
        .par_chunks(run)
        .flat_map_iter(|idxs| {
            let cache = RenderCache::new();
            idxs.iter()
                .filter_map(|&idx| {
                    let img = render_page(pdf, idx, &cache, conf, path);
                    if let Some(p) = progress {
                        p.tick();
                    }
                    img
                })
                .collect::<Vec<_>>()
        })
        .collect()
}

/// A PDF given directly on the command line: sets the resolution if it's still auto and
/// shows its own progress bar.
pub fn add_pdf_images(path: &Path, out: &mut Vec<RgbImage>, conf: &mut Config) -> Result<()> {
    let pdf = open_pdf(path)?;
    let n = pdf.pages().len();
    if n == 0 {
        ui::warn(format!("'{}' has no pages. Skipped.", path.display()));
        return Ok(());
    }
    if let Some((w, h)) = first_page_size(&pdf, conf.reverse) {
        conf.resolve_resolution(w, h, path);
    }
    let name = path.file_name().map_or_else(
        || path.display().to_string(),
        |n| n.to_string_lossy().into_owned(),
    );
    let progress = SharedProgress::new(format!("Loading {name}"), n);
    out.extend(render_pages(&pdf, path, conf, Some(&progress)));
    Ok(())
}
