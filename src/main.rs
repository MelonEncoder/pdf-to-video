mod cli;
mod encode;
mod input;
mod pdf;
mod render;
mod scale;
mod ui;

use anyhow::Result;
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

    // Lists pages/images and their sizes; pixels are rendered on demand while encoding.
    let library = input::scan(&mut conf)?;
    let source = render::frame_source(&library, &conf);
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
