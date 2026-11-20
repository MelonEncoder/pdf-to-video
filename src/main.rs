mod cli;
mod encode;
mod input;
mod pdf;
mod render;
mod scale;
mod ui;

use anyhow::{Result, bail};
use std::time::Instant;

use crate::encode::Mp4Encoder;

fn run() -> Result<()> {
    let mut conf = cli::parse()?;
    if !cli::confirm(&conf) {
        std::process::exit(1);
    }
    let start = Instant::now();

    let images = input::load_all(&mut conf)?;
    if images.is_empty() {
        bail!("No pages or images were loaded, nothing to render.");
    }

    let mut encoder = Mp4Encoder::new(&conf.output, conf.width, conf.height, conf.fps)?;
    render::render(&images, &conf, &mut encoder)?;
    encoder.finish()?;

    ui::print_duration(start);
    Ok(())
}

fn main() {
    if let Err(e) = run() {
        eprintln!("{}<!> Error:{} {e:#}", ui::ORANGE, ui::RESET);
        std::process::exit(1);
    }
}
