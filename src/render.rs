//! Turning loaded images into video frames: per-image frames, or continuous scrolling.
//!
//! Each style is a [`FrameSource`] that can draw any frame on demand, so the encoder can
//! render and encode different parts of the video on different threads.

use image::RgbImage;
use image::imageops;

use crate::cli::{Config, Style};
use crate::encode::FrameSource;
use crate::ui;

/// How long each image is shown in frames mode: `-d` split evenly, else `-s`.
fn frames_per_image(conf: &Config, n_images: usize) -> usize {
    let secs = conf.duration.map_or(conf.spp, |d| d / n_images as f64);
    ((secs * conf.fps).round() as usize).max(1)
}

/// Slideshow: each image centred on a black frame and held for its share of the video.
pub struct Slideshow<'a> {
    images: &'a [RgbImage],
    repeat: usize,
    width: u32,
    height: u32,
}

impl<'a> Slideshow<'a> {
    pub fn new(images: &'a [RgbImage], conf: &Config) -> Self {
        Self {
            images,
            repeat: frames_per_image(conf, images.len()),
            width: conf.width,
            height: conf.height,
        }
    }
}

impl FrameSource for Slideshow<'_> {
    fn len(&self) -> usize {
        self.images.len() * self.repeat
    }
    fn width(&self) -> u32 {
        self.width
    }
    fn height(&self) -> u32 {
        self.height
    }
    fn label(&self) -> String {
        "Rendering Video".into()
    }
    fn render(&self, k: usize, frame: &mut RgbImage) {
        let img = &self.images[k / self.repeat];
        frame.fill(0);
        let x = (self.width.saturating_sub(img.width()) / 2) as i64;
        let y = (self.height.saturating_sub(img.height()) / 2) as i64;
        imageops::replace(frame, img, x, y);
    }
    fn same_as_previous(&self, k: usize) -> bool {
        !k.is_multiple_of(self.repeat)
    }
}

/// One image placed along the scroll axis of the virtual content strip.
struct Segment<'a> {
    start: usize,
    len: usize,
    img: &'a RgbImage,
}

/// The content laid out as one long strip: `[blank viewport][img…][blank viewport]`.
/// For Down/Right the images are laid out in reverse so the first one still enters first.
struct Strip<'a> {
    segments: Vec<Segment<'a>>,
    vertical: bool,
    /// Viewport length along the scroll axis.
    view: usize,
    /// Total distance the viewport travels: from all-blank before the content to all-blank after.
    travel: usize,
}

impl<'a> Strip<'a> {
    fn new(images: &'a [RgbImage], style: Style, width: u32, height: u32) -> Self {
        let vertical = matches!(style, Style::Up | Style::Down);
        let view = if vertical { height } else { width } as usize;
        let ordered: Vec<&RgbImage> = match style {
            Style::Down | Style::Right => images.iter().rev().collect(),
            _ => images.iter().collect(),
        };
        let mut pos = view;
        let segments = ordered
            .into_iter()
            .map(|img| {
                let len = if vertical { img.height() } else { img.width() } as usize;
                let seg = Segment {
                    start: pos,
                    len,
                    img,
                };
                pos += len;
                seg
            })
            .collect();
        // Viewport starts at 0 (showing the leading blank) and ends at `pos`, where it shows
        // only the trailing blank.
        Self {
            segments,
            vertical,
            view,
            travel: pos,
        }
    }

    /// Draw the viewport at `offset` along the strip into `frame`.
    fn compose(&self, frame: &mut RgbImage, offset: usize) {
        frame.fill(0);
        let (fw, fh) = (frame.width() as usize, frame.height() as usize);
        let view_end = offset + self.view;
        // First segment that ends after the viewport starts.
        let first = self.segments.partition_point(|s| s.start + s.len <= offset);
        let buf: &mut [u8] = frame;
        for seg in &self.segments[first..] {
            if seg.start >= view_end {
                break;
            }
            let a = offset.max(seg.start); // overlap in strip coordinates
            let b = view_end.min(seg.start + seg.len);
            let (src_from, dst_from, n) = (a - seg.start, a - offset, b - a);
            let src = seg.img.as_raw();
            if self.vertical {
                // Rows are contiguous: one copy for the whole overlap. Image width == frame width.
                let row = fw * 3;
                buf[dst_from * row..(dst_from + n) * row]
                    .copy_from_slice(&src[src_from * row..(src_from + n) * row]);
            } else {
                let src_w = seg.img.width() as usize;
                for y in 0..fh {
                    let d = (y * fw + dst_from) * 3;
                    let s = (y * src_w + src_from) * 3;
                    buf[d..d + n * 3].copy_from_slice(&src[s..s + n * 3]);
                }
            }
        }
    }
}

/// Number of frames in a scroll video: `-d` seconds, or `-s` seconds per image.
fn scroll_frame_count(conf: &Config, n_images: usize) -> usize {
    let secs = conf.duration.unwrap_or(conf.spp * n_images as f64);
    ((secs * conf.fps).round() as usize).max(2)
}

/// Continuous scroll in any direction. Content enters from the edge opposite the scroll
/// direction and the video ends once it has fully left the screen.
pub struct Scroll<'a> {
    strip: Strip<'a>,
    frames: usize,
    forward: bool,
    width: u32,
    height: u32,
}

impl<'a> Scroll<'a> {
    pub fn new(images: &'a [RgbImage], conf: &Config) -> Self {
        let scroll = Self {
            strip: Strip::new(images, conf.style, conf.width, conf.height),
            frames: scroll_frame_count(conf, images.len()),
            forward: matches!(conf.style, Style::Up | Style::Left),
            width: conf.width,
            height: conf.height,
        };
        if scroll.px_per_frame() < 0.25 {
            ui::note(format!(
                "scrolling very slowly ({:.2} px/frame); motion may look jerky.",
                scroll.px_per_frame()
            ));
        }
        scroll
    }

    fn px_per_frame(&self) -> f64 {
        self.strip.travel as f64 / (self.frames - 1) as f64
    }

    fn offset(&self, k: usize) -> usize {
        let t = k as f64 / (self.frames - 1) as f64;
        let t = if self.forward { t } else { 1.0 - t };
        (t * self.strip.travel as f64).round() as usize
    }
}

impl FrameSource for Scroll<'_> {
    fn len(&self) -> usize {
        self.frames
    }
    fn width(&self) -> u32 {
        self.width
    }
    fn height(&self) -> u32 {
        self.height
    }
    fn label(&self) -> String {
        ui::scroll_label(self.px_per_frame())
    }
    fn render(&self, k: usize, frame: &mut RgbImage) {
        self.strip.compose(frame, self.offset(k));
    }
    fn same_as_previous(&self, k: usize) -> bool {
        k > 0 && self.offset(k) == self.offset(k - 1)
    }
}

/// The frame source for the configured style.
pub fn frame_source<'a>(images: &'a [RgbImage], conf: &Config) -> Box<dyn FrameSource + 'a> {
    match conf.style {
        Style::Frames => Box::new(Slideshow::new(images, conf)),
        _ => Box::new(Scroll::new(images, conf)),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use image::Rgb;
    use std::path::PathBuf;

    fn conf(style: Style, w: u32, h: u32, fps: f64, duration: Option<f64>) -> Config {
        Config {
            width: w,
            height: h,
            fps,
            spp: 1.0,
            duration,
            style,
            output: PathBuf::from("x.mp4"),
            inputs: vec![],
            render_gifs: false,
            reverse: false,
            assume_yes: true,
            threads: 1,
        }
    }

    /// Render every frame of a source, as the encoder would.
    fn all_frames(src: &dyn FrameSource) -> Vec<RgbImage> {
        (0..src.len())
            .map(|k| {
                let mut f = RgbImage::new(src.width(), src.height());
                src.render(k, &mut f);
                f
            })
            .collect()
    }

    const RED: Rgb<u8> = Rgb([255, 0, 0]);
    const BLUE: Rgb<u8> = Rgb([0, 0, 255]);
    const BLACK: Rgb<u8> = Rgb([0, 0, 0]);

    #[test]
    fn frames_are_centred_on_both_axes() {
        let c = conf(Style::Frames, 10, 10, 1.0, None);
        let imgs = [RgbImage::from_pixel(4, 2, RED)];
        let out = all_frames(&Slideshow::new(&imgs, &c));
        assert_eq!(out.len(), 1);
        let f = &out[0];
        assert_eq!(*f.get_pixel(3, 4), RED); // top-left of the centred image
        assert_eq!(*f.get_pixel(6, 5), RED); // bottom-right
        assert_eq!(*f.get_pixel(2, 4), BLACK);
        assert_eq!(*f.get_pixel(3, 3), BLACK);
    }

    #[test]
    fn frames_respect_seconds_per_image_and_duration() {
        let imgs = vec![RgbImage::new(2, 2), RgbImage::new(2, 2)];
        let mut c = conf(Style::Frames, 2, 2, 30.0, None);
        c.spp = 2.0;
        let s = Slideshow::new(&imgs, &c);
        assert_eq!(s.len(), 120);
        // Frame 0 and 60 start a new image; the rest repeat the previous frame.
        assert!(!s.same_as_previous(0) && !s.same_as_previous(60));
        assert!(s.same_as_previous(1) && s.same_as_previous(119));

        let c = conf(Style::Frames, 2, 2, 30.0, Some(1.0));
        assert_eq!(Slideshow::new(&imgs, &c).len(), 30);
    }

    /// Two 4px images (red then blue) in a 4x4 viewport.
    fn scroll(style: Style) -> Vec<RgbImage> {
        let imgs = [
            RgbImage::from_pixel(4, 4, RED),
            RgbImage::from_pixel(4, 4, BLUE),
        ];
        // travel = 4 (blank) + 8 (content) = 12; 13 frames => exactly 1px per frame.
        let c = conf(style, 4, 4, 13.0, Some(1.0));
        all_frames(&Scroll::new(&imgs, &c))
    }

    #[test]
    fn scroll_up_enters_from_bottom_and_ends_black() {
        let f = scroll(Style::Up);
        assert_eq!(f.len(), 13);
        assert!(f[0].pixels().all(|p| *p == BLACK));
        // After 1px, red occupies only the bottom row.
        assert_eq!(*f[1].get_pixel(0, 3), RED);
        assert_eq!(*f[1].get_pixel(0, 2), BLACK);
        // At offset 4 the red image fills the screen; at 6 it's half red (top) half blue.
        assert!(f[4].pixels().all(|p| *p == RED));
        assert_eq!(*f[6].get_pixel(0, 1), RED);
        assert_eq!(*f[6].get_pixel(0, 2), BLUE);
        assert!(f[12].pixels().all(|p| *p == BLACK));
    }

    #[test]
    fn scroll_down_enters_from_top() {
        let f = scroll(Style::Down);
        assert!(f[0].pixels().all(|p| *p == BLACK));
        assert_eq!(*f[1].get_pixel(0, 0), RED);
        assert_eq!(*f[1].get_pixel(0, 1), BLACK);
        assert!(f[4].pixels().all(|p| *p == RED));
        // Blue (second image) comes in above red.
        assert_eq!(*f[6].get_pixel(0, 1), BLUE);
        assert_eq!(*f[6].get_pixel(0, 2), RED);
        assert!(f[12].pixels().all(|p| *p == BLACK));
    }

    #[test]
    fn scroll_left_enters_from_right() {
        let f = scroll(Style::Left);
        assert_eq!(*f[1].get_pixel(3, 0), RED);
        assert_eq!(*f[1].get_pixel(2, 0), BLACK);
        assert_eq!(*f[6].get_pixel(1, 2), RED);
        assert_eq!(*f[6].get_pixel(2, 2), BLUE);
        assert!(f[12].pixels().all(|p| *p == BLACK));
    }

    #[test]
    fn scroll_right_enters_from_left() {
        let f = scroll(Style::Right);
        assert_eq!(*f[1].get_pixel(0, 3), RED);
        assert_eq!(*f[1].get_pixel(1, 3), BLACK);
        assert_eq!(*f[6].get_pixel(1, 0), BLUE);
        assert_eq!(*f[6].get_pixel(2, 0), RED);
        assert!(f[12].pixels().all(|p| *p == BLACK));
    }

    #[test]
    fn scroll_duration_is_exact() {
        let imgs = vec![RgbImage::new(4, 100)];
        let c = conf(Style::Up, 4, 10, 30.0, Some(5.0));
        assert_eq!(Scroll::new(&imgs, &c).len(), 150);
    }

    #[test]
    fn slow_scroll_reuses_frames() {
        // 10px of travel over 100 frames: most frames repeat the previous offset.
        let imgs = vec![RgbImage::new(4, 6)];
        let c = conf(Style::Up, 4, 4, 100.0, Some(1.0));
        let s = Scroll::new(&imgs, &c);
        let repeats = (1..s.len()).filter(|&k| s.same_as_previous(k)).count();
        assert!(
            repeats > 80,
            "expected most frames to repeat, got {repeats}"
        );
    }
}
