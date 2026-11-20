//! Scaling maths shared by image and PDF inputs.

use crate::cli::Style;

/// Scale factor that makes a `w`x`h` source fit inside a `vw`x`vh` viewport,
/// keeping aspect ratio (scales up or down).
pub fn fit_scale(w: f64, h: f64, vw: u32, vh: u32) -> f64 {
    (vw as f64 / w).min(vh as f64 / h)
}

/// Scale factor for a source of size `w`x`h` in the given style.
pub fn style_scale(style: Style, w: f64, h: f64, vw: u32, vh: u32) -> f64 {
    match style {
        Style::Frames => fit_scale(w, h, vw, vh),
        Style::Up | Style::Down => vw as f64 / w,
        Style::Left | Style::Right => vh as f64 / h,
    }
}

/// Exact pixel size an image of `w`x`h` should end up at for the given style.
///
/// Scroll styles pin the cross axis to the viewport exactly; frames mode never
/// exceeds the viewport.
pub fn target_size(style: Style, w: u32, h: u32, vw: u32, vh: u32) -> (u32, u32) {
    let s = style_scale(style, w as f64, h as f64, vw, vh);
    let sw = ((w as f64 * s).round() as u32).max(1);
    let sh = ((h as f64 * s).round() as u32).max(1);
    match style {
        Style::Frames => (sw.min(vw), sh.min(vh)),
        Style::Up | Style::Down => (vw, sh),
        Style::Left | Style::Right => (sw, vh),
    }
}

/// OpenH264's largest frame: 3840x2160 landscape or 2160x3840 portrait.
pub const MAX_LONG_SIDE: u32 = 3840;
pub const MAX_SHORT_SIDE: u32 = 2160;

/// Human-readable form of the encoder limit, for messages.
pub fn encoder_max_text() -> String {
    format!(
        "{MAX_LONG_SIDE}x{MAX_SHORT_SIDE} horizontal or {MAX_SHORT_SIDE}x{MAX_LONG_SIDE} vertical"
    )
}

/// Shrink `w`x`h` (keeping aspect ratio) so the encoder accepts it.
/// Returns `None` if it already fits.
pub fn clamp_to_encoder_max(w: u32, h: u32) -> Option<(u32, u32)> {
    let (max_w, max_h) = if w >= h {
        (MAX_LONG_SIDE, MAX_SHORT_SIDE)
    } else {
        (MAX_SHORT_SIDE, MAX_LONG_SIDE)
    };
    if w <= max_w && h <= max_h {
        return None;
    }
    let s = (max_w as f64 / w as f64).min(max_h as f64 / h as f64);
    // Round *down* to even so the result never exceeds the limit.
    let fit = |n: u32, max: u32| (((n as f64 * s) as u32).min(max) & !1).max(2);
    Some((fit(w, max_w), fit(h, max_h)))
}

/// Round a dimension up to the next even number (H.264 requires even sizes).
pub fn even(n: u32) -> u32 {
    n + (n % 2)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fit_downscales_by_tighter_axis() {
        // 2000x1000 into 1280x720: width is the limiting axis.
        assert_eq!(
            target_size(Style::Frames, 2000, 1000, 1280, 720),
            (1280, 640)
        );
        // Tall page: height limits.
        assert_eq!(target_size(Style::Frames, 612, 792, 1280, 720), (556, 720));
    }

    #[test]
    fn fit_upscales_small_images() {
        assert_eq!(target_size(Style::Frames, 320, 180, 1280, 720), (1280, 720));
        assert_eq!(target_size(Style::Frames, 100, 50, 1280, 720), (1280, 640));
    }

    #[test]
    fn fit_wide_but_short_image() {
        // Wider than viewport but shorter: the C++ version mis-compared width to height here.
        assert_eq!(target_size(Style::Frames, 1500, 100, 1280, 720), (1280, 85));
    }

    #[test]
    fn scroll_pins_cross_axis() {
        assert_eq!(target_size(Style::Up, 612, 792, 1280, 720), (1280, 1656));
        assert_eq!(target_size(Style::Down, 333, 111, 1280, 720), (1280, 427));
        assert_eq!(target_size(Style::Left, 612, 792, 1280, 720), (556, 720));
        assert_eq!(target_size(Style::Right, 10, 10, 1281, 721), (721, 721));
    }

    #[test]
    fn encoder_max_clamping() {
        assert_eq!(clamp_to_encoder_max(1920, 1080), None);
        assert_eq!(clamp_to_encoder_max(3840, 2160), None);
        assert_eq!(clamp_to_encoder_max(2160, 3840), None);
        // Landscape, too wide: 7680x4320 -> exactly 4K.
        assert_eq!(clamp_to_encoder_max(7680, 4320), Some((3840, 2160)));
        // Portrait page larger than the vertical limit, aspect kept.
        assert_eq!(clamp_to_encoder_max(3000, 4000), Some((2160, 2880)));
        // Fits 3840 wide but too tall for landscape (4000x2500): height limits.
        assert_eq!(clamp_to_encoder_max(4000, 2500), Some((3456, 2160)));
        // Result is always even and within limits.
        let (w, h) = clamp_to_encoder_max(5001, 3333).unwrap();
        assert!(w % 2 == 0 && h % 2 == 0 && w <= 3840 && h <= 2160);
    }

    #[test]
    fn even_rounds_up() {
        assert_eq!(even(0), 0);
        assert_eq!(even(1279), 1280);
        assert_eq!(even(720), 720);
    }
}
