//! Command-line parsing, validation and the settings summary / confirmation prompt.

use anyhow::{Context, Result, bail};
use clap::{Parser, ValueEnum};
use std::ffi::OsString;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::scale::{clamp_to_encoder_max, encoder_max_text, even};
use crate::ui::{self, BOLD, RESET};

#[derive(Debug, Clone, Copy, PartialEq, Eq, ValueEnum)]
pub enum Style {
    #[value(skip)]
    Frames,
    Up,
    Down,
    Left,
    Right,
}

impl Style {
    pub fn name(self) -> &'static str {
        match self {
            Style::Frames => "FRAMES",
            Style::Up => "UP",
            Style::Down => "DOWN",
            Style::Left => "LEFT",
            Style::Right => "RIGHT",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Input {
    Pdf(PathBuf),
    Dir(PathBuf),
}

impl Input {
    pub fn path(&self) -> &Path {
        match self {
            Input::Pdf(p) | Input::Dir(p) => p,
        }
    }
}

#[derive(Parser, Debug)]
#[command(name = "ptv", disable_help_flag = true, disable_version_flag = true)]
struct Args {
    /// PDF files and/or image sequence directories.
    inputs: Vec<PathBuf>,

    #[arg(short = 'r', value_parser = parse_resolution)]
    resolution: Option<(u32, u32)>,

    #[arg(short = 'f', default_value_t = 1.0)]
    fps: f64,

    #[arg(short = 's', default_value_t = 1.0)]
    spp: f64,

    #[arg(short = 'd')]
    duration: Option<f64>,

    #[arg(short = 'o')]
    output: Option<PathBuf>,

    #[arg(short = 'a', ignore_case = true)]
    animation: Option<Style>,

    #[arg(long = "gif")]
    gif: bool,

    #[arg(long = "rev")]
    rev: bool,

    #[arg(short = 'y', long = "yes")]
    yes: bool,

    #[arg(short = 'h', long = "help")]
    help: bool,
}

#[derive(Debug, Clone)]
pub struct Config {
    /// Output width; 0 until resolved from the first input when the user passed 0.
    pub width: u32,
    pub height: u32,
    pub fps: f64,
    pub spp: f64,
    pub duration: Option<f64>,
    pub style: Style,
    pub output: PathBuf,
    pub inputs: Vec<Input>,
    pub render_gifs: bool,
    pub reverse: bool,
    pub assume_yes: bool,
}

impl Config {
    /// If the resolution was left as 0 (auto), take it from the first loaded content
    /// (`source` is only used in the warning if it has to be shrunk).
    pub fn resolve_resolution(&mut self, w: u32, h: u32, source: &Path) {
        if self.width != 0 && self.height != 0 {
            return;
        }
        if self.width == 0 {
            self.width = even(w.max(2));
        }
        if self.height == 0 {
            self.height = even(h.max(2));
        }
        if let Some((cw, ch)) = clamp_to_encoder_max(self.width, self.height) {
            ui::warn(format!(
                "'{}' resolution {}x{} is too big. Rendering at the max: {cw}x{ch} ({}).",
                source.display(),
                self.width,
                self.height,
                encoder_max_text()
            ));
            (self.width, self.height) = (cw, ch);
        }
    }
}

fn parse_resolution(s: &str) -> Result<(u32, u32), String> {
    let err = || format!("'{s}' is not a valid resolution. Examples: 1920x1080, 0x720, 0x0");
    let (w, h) = s.split_once(['x', 'X']).ok_or_else(err)?;
    let w: u32 = w.trim().parse().map_err(|_| err())?;
    let h: u32 = h.trim().parse().map_err(|_| err())?;
    Ok((even(w), even(h)))
}

/// Parse the process arguments. Prints help and exits on `-h` or no arguments.
pub fn parse() -> Result<Config> {
    let args: Vec<OsString> = std::env::args_os().collect();
    if args.len() < 2 {
        println!("{}", ui::help_text());
        std::process::exit(1);
    }
    let args = Args::try_parse_from(args).unwrap_or_else(|e| e.exit());
    if args.help {
        println!("{}", ui::help_text());
        std::process::exit(0);
    }
    build_config(args)
}

fn classify_input(path: PathBuf) -> Result<Input> {
    if path.is_dir() {
        return Ok(Input::Dir(path));
    }
    let is_pdf = path
        .extension()
        .is_some_and(|e| e.eq_ignore_ascii_case("pdf"));
    if !path.exists() {
        bail!("'{}' does not exist.", path.display());
    }
    if !is_pdf {
        bail!("'{}' is not a PDF file or a directory.", path.display());
    }
    Ok(Input::Pdf(path))
}

/// Output path given with `-o`: must not be a directory, its parent must exist,
/// and `.mp4` is appended if missing.
fn explicit_output(path: PathBuf) -> Result<PathBuf> {
    let s = path.as_os_str().to_string_lossy();
    if path.is_dir() || s.ends_with('/') || s.ends_with(std::path::MAIN_SEPARATOR) {
        bail!("Output file cannot be a directory.");
    }
    if let Some(parent) = path.parent().filter(|p| !p.as_os_str().is_empty())
        && !parent.is_dir()
    {
        bail!("Output directory '{}' does not exist.", parent.display());
    }
    Ok(with_mp4_extension(path))
}

fn with_mp4_extension(path: PathBuf) -> PathBuf {
    if path
        .extension()
        .is_some_and(|e| e.eq_ignore_ascii_case("mp4"))
    {
        return path;
    }
    let mut s = path.into_os_string();
    s.push(".mp4");
    PathBuf::from(s)
}

/// Output name derived from the first input: `slides.pdf` -> `slides.mp4`, `seq/` -> `seq.mp4`.
fn auto_output(first: &Input) -> Result<PathBuf> {
    match first {
        Input::Pdf(p) => Ok(p.with_extension("mp4")),
        Input::Dir(p) => {
            // `seq/` has file_name "seq"; `.` or `..` need resolving to get a real name.
            let resolved;
            let p = if p.file_name().is_some() {
                p.as_path()
            } else {
                resolved = p
                    .canonicalize()
                    .with_context(|| format!("resolving '{}'", p.display()))?;
                resolved.as_path()
            };
            let name = p
                .file_name()
                .context("cannot derive output name from input directory; use -o")?;
            let mut file = name.to_os_string();
            file.push(".mp4");
            Ok(p.with_file_name(file))
        }
    }
}

fn build_config(args: Args) -> Result<Config> {
    if args.inputs.is_empty() {
        bail!("No input path was specified.");
    }
    if !(args.fps > 0.0 && args.fps.is_finite()) {
        bail!("Frames per second (-f) must be greater than 0.");
    }
    if !(args.spp > 0.0 && args.spp.is_finite()) {
        bail!("Seconds per frame (-s) must be greater than 0.");
    }
    if let Some(d) = args.duration
        && !(d > 0.0 && d.is_finite())
    {
        bail!("Duration (-d) must be greater than 0.");
    }

    let inputs = args
        .inputs
        .into_iter()
        .map(classify_input)
        .collect::<Result<Vec<_>>>()?;
    let output = match args.output {
        Some(p) => explicit_output(p)?,
        None => auto_output(&inputs[0])?,
    };
    let (mut width, mut height) = args.resolution.unwrap_or((1280, 720));
    // With a 0 (auto) side the final size is only known after loading; that case is
    // checked in `Config::resolve_resolution`.
    if width != 0
        && height != 0
        && let Some((cw, ch)) = clamp_to_encoder_max(width, height)
    {
        ui::warn(format!(
            "Resolution {width}x{height} is too big. Rendering at the max: {cw}x{ch} ({}).",
            encoder_max_text()
        ));
        (width, height) = (cw, ch);
    }

    Ok(Config {
        width,
        height,
        fps: args.fps,
        spp: args.spp,
        duration: args.duration,
        style: args.animation.unwrap_or(Style::Frames),
        output,
        inputs,
        render_gifs: args.gif,
        reverse: args.rev,
        assume_yes: args.yes,
    })
}

/// Print the settings and ask the user to confirm. Returns `false` if they declined.
pub fn confirm(conf: &Config) -> bool {
    ui::print_banner("PTV - Render Videos from PDFs and Image Sequences");

    const GAP: usize = 20;
    let n = conf.inputs.len();
    let label = format!("  Input ({n} path{})", if n == 1 { "" } else { "s" });
    let inputs: Vec<String> = conf
        .inputs
        .iter()
        .map(|i| i.path().display().to_string())
        .collect();
    println!("{label:<GAP$}{}", inputs.join(" + "));
    println!("{:<GAP$}{}", "  Output", conf.output.display());
    let resolution = match (conf.width, conf.height) {
        (0, 0) => "auto (from first input)".to_string(),
        (0, h) => format!("auto x {h}"),
        (w, 0) => format!("{w} x auto"),
        (w, h) => format!("{w}x{h}"),
    };
    println!("{:<GAP$}{resolution}", "  Resolution");
    println!("{:<GAP$}{}", "  FPS", conf.fps);
    match conf.duration {
        Some(d) => println!("{:<GAP$}{d}s", "  Duration"),
        None => println!("{:<GAP$}{}", "  SPP", conf.spp),
    }
    println!("{:<GAP$}{}", "  Animation", conf.style.name());

    if conf.assume_yes {
        println!();
        return true;
    }
    print!("\n{BOLD}Proceed? [Y/n] {RESET}");
    let _ = std::io::stdout().flush();
    let mut answer = String::new();
    if std::io::stdin().read_line(&mut answer).is_err() {
        return false;
    }
    matches!(answer.trim(), "" | "y" | "Y")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resolution_parsing() {
        assert_eq!(parse_resolution("1920x1080"), Ok((1920, 1080)));
        assert_eq!(parse_resolution("0x720"), Ok((0, 720)));
        assert_eq!(parse_resolution("1279x719"), Ok((1280, 720)));
        assert!(parse_resolution("1920").is_err());
        assert!(parse_resolution("-1x5").is_err());
        assert!(parse_resolution("axb").is_err());
    }

    #[test]
    fn output_naming() {
        assert_eq!(
            auto_output(&Input::Pdf("a/slides.pdf".into())).unwrap(),
            PathBuf::from("a/slides.mp4")
        );
        assert_eq!(
            auto_output(&Input::Dir("a/seq/".into())).unwrap(),
            PathBuf::from("a/seq.mp4")
        );
        assert_eq!(
            auto_output(&Input::Dir("seq".into())).unwrap(),
            PathBuf::from("seq.mp4")
        );
        assert_eq!(with_mp4_extension("out".into()), PathBuf::from("out.mp4"));
        assert_eq!(
            with_mp4_extension("out.MP4".into()),
            PathBuf::from("out.MP4")
        );
        assert_eq!(
            with_mp4_extension("my.video".into()),
            PathBuf::from("my.video.mp4")
        );
    }

    #[test]
    fn animation_is_case_insensitive() {
        let a = Args::try_parse_from(["ptv", "x.pdf", "-a", "uP"]).unwrap();
        assert_eq!(a.animation, Some(Style::Up));
        assert!(Args::try_parse_from(["ptv", "x.pdf", "-a", "frames"]).is_err());
        assert!(Args::try_parse_from(["ptv", "x.pdf", "-a", "sideways"]).is_err());
    }
}
