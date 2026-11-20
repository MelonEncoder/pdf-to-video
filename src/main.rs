mod cli;
mod encode;
mod input;
mod pdf;
mod render;
mod scale;
mod ui;

use anyhow::{Result, bail};
use std::time::Instant;

fn run() -> Result<()> {
    let mut conf = cli::parse()?;
    if !cli::confirm(&conf) {
        std::process::exit(1);
    }
    let start = Instant::now();
    rayon::ThreadPoolBuilder::new()
        .num_threads(conf.threads)
        .build_global()?;

    let images = input::load_all(&mut conf)?;
    if images.is_empty() {
        bail!("No pages or images were loaded, nothing to render.");
    }

    let source = render::frame_source(&images, &conf);
    encode::encode_video(source.as_ref(), &conf.output, conf.fps, conf.threads)?;

    ui::print_duration(start);
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        eprintln!("{}<!> Error:{} {e:#}", ui::ORANGE, ui::RESET);
        std::process::exit(1);
    }
}
