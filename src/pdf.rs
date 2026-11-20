//! PDF page rendering via hayro (pure Rust).

use anyhow::{Context, Result, anyhow};
use hayro::hayro_interpret::InterpreterSettings;
use hayro::hayro_syntax::Pdf;
use hayro::vello_cpu::color::palette::css::WHITE;
use hayro::{PixmapSettings, RenderCache, RenderSettings, render};
use image::RgbImage;
use std::path::Path;
use std::sync::Arc;

use crate::cli::Config;
use crate::input::conform;
use crate::scale::style_scale;
use crate::ui::{self, Progress};

/// Largest pixmap side hayro can render (its buffers are sized with u16).
const MAX_SIDE: f64 = u16::MAX as f64;

/// Render every page of `path`, scaled for the configured style, onto `out`.
pub fn add_pdf_images(path: &Path, out: &mut Vec<RgbImage>, conf: &mut Config) -> Result<()> {
    let data = std::fs::read(path).with_context(|| format!("reading '{}'", path.display()))?;
    let pdf = Pdf::new(Arc::new(data))
        .map_err(|e| anyhow!("'{}' could not be opened as a PDF: {e:?}", path.display()))?;
    let pages = pdf.pages();
    if pages.is_empty() {
        ui::warn(format!("'{}' has no pages. Skipped.", path.display()));
        return Ok(());
    }

    let cache = RenderCache::new();
    let interpreter = InterpreterSettings::default();
    let render_settings = RenderSettings::default();
    let name = path.file_name().map_or_else(
        || path.display().to_string(),
        |n| n.to_string_lossy().into_owned(),
    );
    let mut progress = Progress::new(format!("Loading {name}"), pages.len());

    // With --rev, render pages last-to-first. `idx` stays the real page index for messages.
    let order: Vec<usize> = if conf.reverse {
        (0..pages.len()).rev().collect()
    } else {
        (0..pages.len()).collect()
    };
    for (done, idx) in order.into_iter().enumerate() {
        let page = &pages[idx];
        // Page size in points (1pt = 1px at 72 DPI), after rotation and cropping.
        let (pw, ph) = page.render_dimensions();
        if !(pw > 0.0 && ph > 0.0) {
            ui::warn(format!(
                "Page {} of '{}' has no size. Skipped.",
                idx + 1,
                path.display()
            ));
            progress.set(done + 1);
            continue;
        }
        conf.resolve_resolution(pw.round() as u32, ph.round() as u32, path);

        let mut scale = style_scale(conf.style, pw as f64, ph as f64, conf.width, conf.height);
        // Very long pages in scroll mode could exceed the renderer's limit; render as large as
        // allowed and let `conform` stretch the rest of the way.
        scale = scale.min(MAX_SIDE / pw as f64).min(MAX_SIDE / ph as f64);

        let pixmap = render(
            page,
            &cache,
            &interpreter,
            &render_settings,
            &PixmapSettings {
                x_scale: scale as f32,
                y_scale: scale as f32,
                bg_color: WHITE,
            },
        );
        let (w, h) = (pixmap.width() as u32, pixmap.height() as u32);
        if w == 0 || h == 0 {
            ui::warn(format!(
                "Page {} of '{}' rendered empty. Skipped.",
                idx + 1,
                path.display()
            ));
            progress.set(done + 1);
            continue;
        }
        // Opaque white background, so premultiplied RGBA == straight RGBA; just drop alpha.
        let rgb: Vec<u8> = pixmap
            .data_as_u8_slice()
            .as_chunks::<4>()
            .0
            .iter()
            .flat_map(|&[r, g, b, _]| [r, g, b])
            .collect();
        let img = RgbImage::from_raw(w, h, rgb).context("pixmap size mismatch")?;
        out.push(conform(img, conf));
        progress.set(done + 1);
    }
    Ok(())
}
