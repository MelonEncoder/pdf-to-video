//! PDF page rendering via hayro (pure Rust).
//!
//! Pages are rendered on demand, at exactly their final size, by whichever encoder thread
//! needs them.

use anyhow::{Context, Result, anyhow};
use hayro::hayro_interpret::InterpreterSettings;
use hayro::hayro_syntax::Pdf;
use hayro::vello_cpu::color::palette::css::WHITE;
use hayro::{PixmapSettings, RenderCache, RenderSettings, render};
use image::RgbImage;
use std::cell::RefCell;
use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;

use crate::input::resize_exact;

/// Largest pixmap side hayro can render (its buffers are sized with u16).
const MAX_SIDE: u32 = u16::MAX as u32;

/// Open a PDF for the rest of the program's run. Documents are leaked deliberately: pages
/// are rendered lazily from many threads until the video is finished, and a `'static`
/// document lets each thread keep a font/glyph cache for it (see [`CACHES`]).
pub fn open_pdf(path: &Path) -> Result<&'static Pdf> {
    let data = std::fs::read(path).with_context(|| format!("reading '{}'", path.display()))?;
    let pdf = Pdf::new(Arc::new(data))
        .map_err(|e| anyhow!("'{}' could not be opened as a PDF: {e:?}", path.display()))?;
    Ok(Box::leak(Box::new(pdf)))
}

/// Page indices in playback order (last-to-first with `--rev`).
pub fn page_order(n: usize, reverse: bool) -> Vec<usize> {
    if reverse {
        (0..n).rev().collect()
    } else {
        (0..n).collect()
    }
}

/// Size in points (1pt = 1px at 72 DPI) of a page, after rotation and cropping.
pub fn page_size(pdf: &Pdf, idx: usize) -> Option<(f32, f32)> {
    let (w, h) = pdf.pages()[idx].render_dimensions();
    (w > 0.0 && h > 0.0).then_some((w, h))
}

thread_local! {
    /// One render cache per document per thread. The cache isn't thread-safe, but reusing
    /// it across pages avoids re-parsing fonts for every page.
    static CACHES: RefCell<HashMap<usize, RenderCache<'static>>> = RefCell::new(HashMap::new());
}

/// Render a page to exactly `width`x`height` pixels.
pub fn render_page(pdf: &'static Pdf, idx: usize, width: u32, height: u32) -> RgbImage {
    let page = &pdf.pages()[idx];
    let (pw, ph) = page.render_dimensions();
    // hayro sizes the pixmap as `(points * scale) as u16` (truncating), so aim half a pixel
    // past the target to land on it exactly; no second resample needed.
    let (rw, rh) = (width.min(MAX_SIDE), height.min(MAX_SIDE));
    let settings = PixmapSettings {
        x_scale: ((rw as f64 + 0.5) / pw as f64) as f32,
        y_scale: ((rh as f64 + 0.5) / ph as f64) as f32,
        bg_color: WHITE,
    };
    let pixmap = CACHES.with(|caches| {
        let mut caches = caches.borrow_mut();
        let cache = caches
            .entry(pdf as *const Pdf as usize)
            .or_insert_with(RenderCache::new);
        render(
            page,
            cache,
            &InterpreterSettings::default(),
            &RenderSettings::default(),
            &settings,
        )
    });
    let (w, h) = (pixmap.width() as u32, pixmap.height() as u32);
    // Opaque white background, so premultiplied RGBA == straight RGBA; just drop alpha.
    let rgb: Vec<u8> = pixmap
        .data_as_u8_slice()
        .as_chunks::<4>()
        .0
        .iter()
        .flat_map(|&[r, g, b, _]| [r, g, b])
        .collect();
    let img = RgbImage::from_raw(w, h, rgb).unwrap_or_else(|| RgbImage::new(width, height));
    // Only differs for pages longer than the renderer's limit (or float edge cases).
    resize_exact(img, width, height)
}
