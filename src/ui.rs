//! Terminal output: colours, banner, progress bar and timing.

use std::io::{IsTerminal, Write};
use std::sync::Mutex;
use std::time::Instant;

pub const RESET: &str = "\x1b[0m";
pub const BOLD: &str = "\x1b[1m";
pub const DIM: &str = "\x1b[2m";
pub const GREEN: &str = "\x1b[32m";
pub const CYAN: &str = "\x1b[36m";
pub const ORANGE: &str = "\x1b[38;2;255;165;0m";

pub fn help_text() -> String {
    let opt = |flag: &str, desc: &str| format!("  {CYAN}{flag:<24}{RESET}  {desc}");
    let input = |name: &str, desc: &str| format!("  {ORANGE}{name:<24}{RESET}  {desc}");
    [
        String::new(),
        format!("{BOLD}ptv{RESET} \u{2014} PDF / Image Sequence to Video"),
        String::new(),
        format!("{BOLD}Usage:{RESET}"),
        "  ptv [input paths...] [options]".to_string(),
        String::new(),
        format!("{BOLD}Inputs:{RESET}"),
        input("<path.pdf>", "PDF file(s) to render."),
        input("<sequence_dir/>", "Directory of numbered images."),
        String::new(),
        format!("{BOLD}Options:{RESET}"),
        opt(
            "-r <int>x<int>",
            &format!("Output resolution. 0 preserves original. {DIM}(default: 1280x720){RESET}"),
        ),
        opt(
            "-f <float>",
            &format!("Frames per second. {DIM}(default: 1){RESET}"),
        ),
        opt(
            "-s <float>",
            &format!("Seconds each page/image is shown. {DIM}(default: 1){RESET}"),
        ),
        opt(
            "-d <float>",
            &format!("Duration of video in seconds. {DIM}(overrides -s){RESET}"),
        ),
        opt(
            "-o <output_path>",
            &format!("Output file path. {DIM}(.mp4 only, auto-named if blank){RESET}"),
        ),
        opt(
            "-a <Up|Down|Left|Right>",
            "Scroll content continuously instead of per-page frames.",
        ),
        opt("--gif", "Render .gif files found in image sequences."),
        opt(
            "--rev",
            "Reverse order: PDF pages last-to-first, images by descending number.",
        ),
        opt(
            "-j <int>",
            &format!("Worker threads. {DIM}(default: all CPU cores){RESET}"),
        ),
        opt("-y, --yes", "Skip the confirmation prompt."),
        opt("-h, --help", "Show this help text."),
        String::new(),
        format!("{BOLD}Examples:{RESET}"),
        format!("  {DIM}ptv slides.pdf -r 1920x1080 -f 30 -s 2{RESET}"),
        format!("  {DIM}ptv frames/ -a Up -f 60 -o output.mp4{RESET}"),
    ]
    .join("\n")
}

pub fn print_banner(title: &str) {
    let bar = "\u{2500}".repeat(title.chars().count() + 4);
    println!("\u{250C}{bar}\u{2510}");
    println!("\u{2502}  {BOLD}{title}{RESET}  \u{2502}");
    println!("\u{2514}{bar}\u{2518}\n");
}

pub fn scroll_label(px_per_frame: f64) -> String {
    format!("Rendering Video {DIM}({px_per_frame:.2} px/frame){RESET}")
}

/// Two-line progress display:
/// ```text
/// ▶ Label
///   [######------] 42% (n/N)
/// ```
pub struct Progress {
    label: String,
    total: usize,
    started: bool,
    last_pct: Option<usize>,
}

const BAR_WIDTH: usize = 44;

impl Progress {
    pub fn new(label: impl Into<String>, total: usize) -> Self {
        Self {
            label: label.into(),
            total,
            started: false,
            last_pct: None,
        }
    }

    pub fn set(&mut self, current: usize) {
        let total = self.total.max(1);
        let current = current.min(total);
        let frac = current as f64 / total as f64;
        let pct = (frac * 100.0) as usize;
        let done = current == total;
        // When piped or redirected, skip the animated redraws and only report completion.
        if !done && !std::io::stdout().is_terminal() {
            return;
        }
        // Redrawing is cheap, but skip it when nothing visible changed (scroll mode
        // updates once per frame, which can be tens of thousands of times).
        if self.started && self.last_pct == Some(pct) && !done {
            return;
        }
        self.last_pct = Some(pct);

        let mut out = std::io::stdout().lock();
        if self.started {
            let _ = write!(out, "\x1b[1A\r\x1b[2K"); // move up one line and clear it
        }
        self.started = true;
        let icon = if done {
            format!("{GREEN}\u{2713}\u{FE0E} {RESET}")
        } else {
            format!("{ORANGE}\u{25B6}\u{FE0E} {RESET}")
        };
        let filled = (BAR_WIDTH as f64 * frac) as usize;
        let bar: String = (0..BAR_WIDTH)
            .map(|i| if i < filled { '#' } else { '-' })
            .collect();
        let _ = write!(
            out,
            "{icon}{}\x1b[K\n\r\x1b[2K  [{bar}] {pct:>3}% ({current}/{})",
            self.label, self.total
        );
        if done {
            let _ = writeln!(out);
        }
        let _ = out.flush();
    }
}

/// A [`Progress`] that several threads can advance, one item at a time.
pub struct SharedProgress(Mutex<(Progress, usize)>);

impl SharedProgress {
    pub fn new(label: impl Into<String>, total: usize) -> Self {
        Self(Mutex::new((Progress::new(label, total), 0)))
    }

    pub fn tick(&self) {
        let mut guard = self.0.lock().unwrap();
        let (progress, done) = &mut *guard;
        *done += 1;
        progress.set(*done);
    }
}

pub fn print_duration(start: Instant) {
    let secs = start.elapsed().as_secs();
    println!(
        "{GREEN}\u{2713}\u{FE0E} {RESET}Done in {}m {}s",
        secs / 60,
        secs % 60
    );
}

pub fn note(msg: impl std::fmt::Display) {
    eprintln!("{DIM}<!> Note:{RESET} {msg}");
}

pub fn warn(msg: impl std::fmt::Display) {
    eprintln!("{ORANGE}<!> Warning:{RESET} {msg}");
}
