//! H.264 encoding (OpenH264, statically compiled) and MP4 muxing (pure Rust).
//!
//! OpenH264 encodes one frame at a time on a single core, so the video is split into
//! chunks that are encoded in parallel, each by its own encoder starting with a keyframe.
//! All encoders share identical settings, so they emit identical SPS/PPS headers and the
//! chunks join into one valid H.264 stream. The main thread writes finished chunks to the
//! MP4 in order.

use anyhow::{Context, Result, anyhow, bail};
use bytes::Bytes;
use image::RgbImage;
use mp4::{AvcConfig, MediaConfig, Mp4Config, Mp4Sample, Mp4Writer, TrackConfig, TrackType};
use openh264::OpenH264API;
use openh264::encoder::{
    BitRate, EncodedBitStream, Encoder, EncoderConfig, FrameRate, FrameType, IntraFramePeriod,
    QpRange, RateControlMode, UsageType, VuiConfig,
};
use openh264::formats::{RgbSliceU8, YUVBuffer};
use std::collections::BTreeMap;
use std::fs::File;
use std::io::BufWriter;
use std::ops::Range;
use std::panic::AssertUnwindSafe;
use std::path::{Path, PathBuf};
use std::sync::{Condvar, Mutex, mpsc};

use crate::scale::{MAX_LONG_SIDE, MAX_SHORT_SIDE};
use crate::ui::Progress;

/// Something that can draw any frame of the video on demand, from any thread.
pub trait FrameSource: Sync {
    fn len(&self) -> usize;
    fn width(&self) -> u32;
    fn height(&self) -> u32;
    /// Progress bar label for the encoding stage.
    fn label(&self) -> String;
    /// Draw frame `k` into `frame` (already sized `width()`x`height()`).
    fn render(&self, k: usize, frame: &mut RgbImage);
    /// True if frame `k` is identical to frame `k - 1`, letting the encoder skip
    /// redrawing and colour conversion.
    fn same_as_previous(&self, _k: usize) -> bool {
        false
    }
    /// Pages/images that frame `k` draws from (appended to `out`), so their pixels can be
    /// freed once every chunk using them is done.
    fn items(&self, _k: usize, _out: &mut Vec<usize>) {}
    /// One more chunk will use `item`.
    fn retain(&self, _item: usize) {}
    /// A chunk using `item` is finished.
    fn release(&self, _item: usize) {}
}

/// Track timescale (ticks per second). 90 kHz is the MPEG convention and represents
/// common and fractional frame rates (e.g. 29.97, 0.5) accurately.
const TIMESCALE: u32 = 90_000;
const TRACK_ID: u32 = 1;
/// Smallest chunk worth a keyframe of its own (unless the keyframe interval is shorter).
const MIN_CHUNK: usize = 15;

/// Frames between keyframes: roughly every 5 seconds, which keeps seeking responsive.
fn keyframe_interval(fps: f64) -> usize {
    (fps * 5.0).round().clamp(1.0, u32::MAX as f64) as usize
}

/// Frames per parallel chunk: enough chunks to keep every thread busy, never longer than
/// the keyframe interval (so chunking adds no keyframes beyond what we'd emit anyway when
/// there's plenty of work), and not so short that keyframes dominate the file size.
fn chunk_len(total: usize, threads: usize, fps: f64) -> usize {
    let even_split = total.div_ceil(threads.max(1));
    even_split.max(MIN_CHUNK).min(keyframe_interval(fps)).max(1)
}

fn make_encoder(fps: f64) -> Result<Encoder> {
    // Constant quality rather than a bitrate target: slides and documents have sharp text
    // that blurs under bitrate starvation, while static content compresses to almost
    // nothing anyway. The bitrate is only a nominal value OpenH264 requires.
    let config = EncoderConfig::new()
        .rate_control_mode(RateControlMode::Off)
        .qp(QpRange::new(18, 24))
        .bitrate(BitRate::from_bps(20_000_000))
        .max_frame_rate(FrameRate::from_hz(fps as f32))
        .skip_frames(false)
        .usage_type(UsageType::ScreenContentRealTime)
        // Not supported for screen content; disabling avoids OpenH264 warnings on stderr.
        .adaptive_quantization(false)
        .background_detection(false)
        .intra_frame_period(IntraFramePeriod::from_num_frames(
            keyframe_interval(fps) as u32
        ))
        // openh264's RGB->YUV conversion uses BT.601 limited-range coefficients; tag the
        // stream to match so players don't assume BT.709 and shift the colours.
        .vui(VuiConfig::bt601())
        .debug(false);
    Encoder::with_api_config(OpenH264API::from_source(), config)
        .map_err(|e| anyhow!("could not start H.264 encoder: {e}"))
}

/// Remove an Annex-B start code (`00 00 01` or `00 00 00 01`) from a NAL unit.
fn strip_start_code(nal: &[u8]) -> &[u8] {
    if nal.starts_with(&[0, 0, 0, 1]) {
        &nal[4..]
    } else if nal.starts_with(&[0, 0, 1]) {
        &nal[3..]
    } else {
        nal
    }
}

const NAL_SPS: u8 = 7;
const NAL_PPS: u8 = 8;

/// An encoded frame converted from Annex-B to MP4's length-prefixed (AVCC) layout.
#[derive(Default)]
struct AvccFrame {
    data: Vec<u8>,
    sps: Option<Vec<u8>>,
    pps: Option<Vec<u8>>,
}

impl AvccFrame {
    /// Append one Annex-B NAL unit. SPS/PPS are pulled out (MP4 stores them in the
    /// track header) rather than repeated in the sample.
    fn push(&mut self, nal: &[u8]) {
        let nal = strip_start_code(nal);
        let Some(&header) = nal.first() else { return };
        match header & 0x1f {
            NAL_SPS => self.sps = Some(nal.to_vec()),
            NAL_PPS => self.pps = Some(nal.to_vec()),
            _ => {
                self.data
                    .extend_from_slice(&(nal.len() as u32).to_be_bytes());
                self.data.extend_from_slice(nal);
            }
        }
    }

    fn from_bitstream(bs: &EncodedBitStream<'_>) -> Self {
        let mut out = Self::default();
        for l in 0..bs.num_layers() {
            let Some(layer) = bs.layer(l) else { continue };
            for n in 0..layer.nal_count() {
                if let Some(nal) = layer.nal_unit(n) {
                    out.push(nal);
                }
            }
        }
        out
    }
}

/// One encoded frame. Empty `data` means the encoder skipped it (previous frame holds).
struct EncodedFrame {
    data: Vec<u8>,
    is_sync: bool,
}

/// The output of one worker: consecutive encoded frames plus the stream headers.
struct Chunk {
    frames: Vec<EncodedFrame>,
    sps: Vec<u8>,
    pps: Vec<u8>,
}

fn encode_chunk(src: &dyn FrameSource, range: Range<usize>, fps: f64) -> Result<Chunk> {
    let (w, h) = (src.width(), src.height());
    let mut encoder = make_encoder(fps)?;
    let mut frame = RgbImage::new(w, h);
    let mut yuv = YUVBuffer::new(w as usize, h as usize);
    let (mut sps, mut pps) = (None, None);
    let mut frames = Vec::with_capacity(range.len());

    for k in range.clone() {
        // The first frame of a chunk is always drawn: this worker has no previous frame.
        if k == range.start || !src.same_as_previous(k) {
            src.render(k, &mut frame);
            // `read_rgb8` is ~20x faster than `read_rgb`, with identical output.
            yuv.read_rgb8(RgbSliceU8::new(frame.as_raw(), (w as usize, h as usize)));
        }
        let bs = encoder
            .encode(&yuv)
            .map_err(|e| anyhow!("H.264 encoding failed: {e}"))?;
        let frame_type = bs.frame_type();
        let avcc = AvccFrame::from_bitstream(&bs);
        sps = sps.or(avcc.sps);
        pps = pps.or(avcc.pps);
        let skipped = frame_type == FrameType::Skip || avcc.data.is_empty();
        frames.push(EncodedFrame {
            data: if skipped { Vec::new() } else { avcc.data },
            // Only IDR frames are safe seek points (a plain I-frame may be followed by frames
            // that reference earlier ones).
            is_sync: frame_type == FrameType::IDR,
        });
    }
    Ok(Chunk {
        frames,
        sps: sps.context("encoder did not emit an SPS")?,
        pps: pps.context("encoder did not emit a PPS")?,
    })
}

/// Writes encoded frames to the MP4, in order. The video is written to a temporary file
/// next to the destination and renamed into place only when complete, so a failed run
/// neither leaves a broken file nor destroys an existing one.
struct Mp4Output {
    path: PathBuf,
    tmp_path: PathBuf,
    done: bool,
    width: u32,
    height: u32,
    writer: Option<Mp4Writer<BufWriter<File>>>,
    headers: Option<(Vec<u8>, Vec<u8>)>,
    /// Encoded frame not yet written, so skipped frames can extend its duration.
    pending: Option<Mp4Sample>,
    next_start: u64,
    frame_ticks: u32,
}

impl Mp4Output {
    fn new(path: &Path, width: u32, height: u32, fps: f64) -> Self {
        let name = path
            .file_name()
            .map_or_else(|| "video.mp4".into(), |n| n.to_string_lossy().into_owned());
        Self {
            path: path.to_path_buf(),
            tmp_path: path.with_file_name(format!(".{name}.ptv-partial")),
            done: false,
            width,
            height,
            writer: None,
            headers: None,
            pending: None,
            next_start: 0,
            frame_ticks: ((TIMESCALE as f64 / fps).round() as u32).max(1),
        }
    }

    fn push_chunk(&mut self, chunk: Chunk) -> Result<()> {
        match &self.headers {
            None => {
                self.open_writer(chunk.sps.clone(), chunk.pps.clone())?;
                self.headers = Some((chunk.sps, chunk.pps));
            }
            Some((sps, pps)) => {
                // Should never happen: every encoder is configured identically.
                if *sps != chunk.sps || *pps != chunk.pps {
                    bail!("parallel encoders produced different stream headers; try -j 1");
                }
            }
        }
        for frame in chunk.frames {
            self.push(frame)?;
        }
        Ok(())
    }

    fn push(&mut self, frame: EncodedFrame) -> Result<()> {
        if frame.data.is_empty() {
            // Nothing new to show: hold the previous frame for longer.
            if let Some(prev) = self.pending.as_mut() {
                prev.duration += self.frame_ticks;
            }
            self.next_start += self.frame_ticks as u64;
            return Ok(());
        }
        self.flush_pending()?;
        self.pending = Some(Mp4Sample {
            start_time: self.next_start,
            duration: self.frame_ticks,
            rendering_offset: 0,
            is_sync: frame.is_sync,
            bytes: Bytes::from(frame.data),
        });
        self.next_start += self.frame_ticks as u64;
        Ok(())
    }

    fn flush_pending(&mut self) -> Result<()> {
        if let (Some(sample), Some(writer)) = (self.pending.take(), self.writer.as_mut()) {
            writer
                .write_sample(TRACK_ID, &sample)
                .context("writing MP4 sample")?;
        }
        Ok(())
    }

    /// Flush the last frame and write the MP4 index. Without this the file is unplayable.
    fn finish(mut self) -> Result<()> {
        self.flush_pending()?;
        let mut writer = self
            .writer
            .take()
            .context("no frames were rendered, nothing to write")?;
        writer.write_end().context("finalising MP4")?;
        writer
            .into_writer()
            .into_inner()
            .map_err(|e| e.into_error())
            .context("writing MP4")?;
        std::fs::rename(&self.tmp_path, &self.path)
            .with_context(|| format!("saving '{}'", self.path.display()))?;
        self.done = true;
        Ok(())
    }

    fn open_writer(&mut self, sps: Vec<u8>, pps: Vec<u8>) -> Result<()> {
        let file = File::create(&self.tmp_path)
            .with_context(|| format!("creating '{}'", self.tmp_path.display()))?;
        let brand = |s: &str| s.parse().expect("valid fourcc");
        let mut writer = Mp4Writer::write_start(
            BufWriter::new(file),
            &Mp4Config {
                major_brand: brand("isom"),
                minor_version: 512,
                compatible_brands: vec![brand("isom"), brand("iso2"), brand("avc1"), brand("mp41")],
                timescale: 1000,
            },
        )?;
        writer.add_track(&TrackConfig {
            track_type: TrackType::Video,
            timescale: TIMESCALE,
            language: "und".into(),
            media_conf: MediaConfig::AvcConfig(AvcConfig {
                width: self.width as u16,
                height: self.height as u16,
                seq_param_set: sps,
                pic_param_set: pps,
            }),
        })?;
        self.writer = Some(writer);
        Ok(())
    }
}

impl Drop for Mp4Output {
    fn drop(&mut self) {
        if !self.done && self.writer.is_some() {
            drop(self.writer.take()); // close the file first (required on Windows)
            let _ = std::fs::remove_file(&self.tmp_path);
        }
    }
}

/// Turn a worker panic into an ordinary error, so the writer reports it instead of
/// waiting forever for a chunk that will never arrive.
fn panic_to_error(payload: Box<dyn std::any::Any + Send>) -> anyhow::Error {
    let msg = payload
        .downcast_ref::<&str>()
        .map(|s| s.to_string())
        .or_else(|| payload.downcast_ref::<String>().cloned())
        .unwrap_or_else(|| "unknown panic".into());
    anyhow!("internal error while encoding: {msg}")
}

/// Hands out chunk indices in order, keeping workers at most `window` chunks ahead of
/// the writer so finished-but-unwritten chunks can't pile up in memory.
struct Scheduler {
    state: Mutex<SchedState>,
    cond: Condvar,
    window: usize,
    total: usize,
}

struct SchedState {
    next: usize,
    written: usize,
    stopped: bool,
}

impl Scheduler {
    fn take(&self) -> Option<usize> {
        let mut s = self.state.lock().unwrap();
        loop {
            if s.stopped || s.next >= self.total {
                return None;
            }
            if s.next < s.written + self.window {
                s.next += 1;
                return Some(s.next - 1);
            }
            s = self.cond.wait(s).unwrap();
        }
    }

    fn set_written(&self, written: usize) {
        self.state.lock().unwrap().written = written;
        self.cond.notify_all();
    }

    fn stop(&self) {
        self.state.lock().unwrap().stopped = true;
        self.cond.notify_all();
    }
}

/// Encode every frame of `src` to an H.264 MP4 at `path`, using up to `threads` cores.
pub fn encode_video(src: &dyn FrameSource, path: &Path, fps: f64, threads: usize) -> Result<()> {
    let (w, h) = (src.width(), src.height());
    if w == 0 || h == 0 || !w.is_multiple_of(2) || !h.is_multiple_of(2) {
        bail!("video size must be even and non-zero, got {w}x{h}");
    }
    let (max_w, max_h) = if w >= h {
        (MAX_LONG_SIDE, MAX_SHORT_SIDE)
    } else {
        (MAX_SHORT_SIDE, MAX_LONG_SIDE)
    };
    if w > max_w || h > max_h {
        bail!("video size {w}x{h} exceeds the encoder maximum {max_w}x{max_h}");
    }
    let total = src.len();
    if total == 0 {
        bail!("no frames were rendered, nothing to write");
    }

    let threads = threads.max(1);
    let chunk = chunk_len(total, threads, fps);
    let n_chunks = total.div_ceil(chunk);
    let workers = threads.min(n_chunks);
    let sched = Scheduler {
        state: Mutex::new(SchedState {
            next: 0,
            written: 0,
            stopped: false,
        }),
        cond: Condvar::new(),
        window: workers * 2,
        total: n_chunks,
    };
    // Which pages/images each chunk draws from. Registering every use up front lets the
    // library free a page's pixels as soon as the last chunk needing it is encoded.
    let chunk_items: Vec<Vec<usize>> = (0..n_chunks)
        .map(|i| {
            let mut items = Vec::new();
            for k in i * chunk..((i + 1) * chunk).min(total) {
                src.items(k, &mut items);
            }
            items.sort_unstable();
            items.dedup();
            for &item in &items {
                src.retain(item);
            }
            items
        })
        .collect();

    let mut out = Mp4Output::new(path, w, h, fps);
    let mut progress = Progress::new(src.label(), total);

    let result = std::thread::scope(|scope| -> Result<()> {
        let (tx, rx) = mpsc::channel::<(usize, Result<Chunk>)>();
        for _ in 0..workers {
            let (tx, sched, chunk_items) = (tx.clone(), &sched, &chunk_items);
            scope.spawn(move || {
                while let Some(i) = sched.take() {
                    let range = i * chunk..((i + 1) * chunk).min(total);
                    let result = std::panic::catch_unwind(AssertUnwindSafe(|| {
                        encode_chunk(src, range, fps)
                    }))
                    .unwrap_or_else(|p| Err(panic_to_error(p)));
                    for &item in &chunk_items[i] {
                        src.release(item);
                    }
                    if tx.send((i, result)).is_err() {
                        return; // writer gave up
                    }
                }
            });
        }
        drop(tx);

        // Chunks can finish out of order; buffer them and write in sequence.
        let mut ready: BTreeMap<usize, Result<Chunk>> = BTreeMap::new();
        let (mut next_write, mut frames_done) = (0, 0);
        for (i, chunk) in rx {
            ready.insert(i, chunk);
            while let Some(chunk) = ready.remove(&next_write) {
                let written = chunk.and_then(|c| {
                    let n = c.frames.len();
                    out.push_chunk(c).map(|()| n)
                });
                match written {
                    Ok(n) => frames_done += n,
                    Err(e) => {
                        sched.stop();
                        return Err(e);
                    }
                }
                next_write += 1;
                sched.set_written(next_write);
                progress.set(frames_done);
            }
        }
        Ok(())
    });
    result?;
    out.finish()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn annexb_to_avcc() {
        let sps: &[u8] = &[0, 0, 0, 1, 0x67, 1, 2];
        let pps: &[u8] = &[0, 0, 0, 1, 0x68, 3];
        let idr: &[u8] = &[0, 0, 1, 0x65, 9, 9, 9];
        let mut f = AvccFrame::default();
        for nal in [sps, pps, idr] {
            f.push(nal);
        }
        assert_eq!(f.sps.as_deref(), Some(&[0x67, 1, 2][..]));
        assert_eq!(f.pps.as_deref(), Some(&[0x68, 3][..]));
        assert_eq!(f.data, [0, 0, 0, 4, 0x65, 9, 9, 9]);
    }

    #[test]
    fn chunk_sizes() {
        // Plenty of frames: split evenly, capped at the keyframe interval (5s).
        assert_eq!(chunk_len(1800, 24, 60.0), 75);
        assert_eq!(chunk_len(100_000, 24, 30.0), 150);
        // Few frames: never below the minimum...
        assert_eq!(chunk_len(40, 24, 30.0), 15);
        // ...unless keyframes are that frequent anyway (1 fps slideshow: every 5 frames).
        assert_eq!(chunk_len(120, 24, 1.0), 5);
        assert_eq!(chunk_len(3, 24, 0.1), 1);
    }

    /// Frames whose colour changes every frame, so every frame encodes to new data.
    struct Ramp {
        n: usize,
    }

    impl FrameSource for Ramp {
        fn len(&self) -> usize {
            self.n
        }
        fn width(&self) -> u32 {
            64
        }
        fn height(&self) -> u32 {
            48
        }
        fn label(&self) -> String {
            "test".into()
        }
        fn render(&self, k: usize, frame: &mut RgbImage) {
            frame.fill((k * 7 % 256) as u8);
        }
    }

    fn read_back(path: &Path) -> (u32, f64, Vec<bool>) {
        let file = File::open(path).unwrap();
        let size = file.metadata().unwrap().len();
        let mut reader = mp4::Mp4Reader::read_header(std::io::BufReader::new(file), size).unwrap();
        let count = reader.tracks().get(&TRACK_ID).unwrap().sample_count();
        let secs = reader.duration().as_secs_f64();
        let syncs = (1..=count)
            .map(|i| reader.read_sample(TRACK_ID, i).unwrap().unwrap().is_sync)
            .collect();
        (count, secs, syncs)
    }

    #[test]
    fn parallel_encode_keeps_every_frame_in_order() {
        let path = std::env::temp_dir().join(format!("ptv-enc-test-{}.mp4", std::process::id()));
        // 100 frames at 10 fps over 4 threads -> chunks of 25 frames.
        encode_video(&Ramp { n: 100 }, &path, 10.0, 4).unwrap();
        let (count, secs, syncs) = read_back(&path);
        assert_eq!(count, 100);
        assert_eq!(secs, 10.0);
        // Each chunk starts on a keyframe.
        for start in [0, 25, 50, 75] {
            assert!(syncs[start], "frame {start} should be a keyframe");
        }
        std::fs::remove_file(&path).unwrap();
    }

    /// Panics while drawing frame `bad`.
    struct Faulty {
        bad: usize,
    }

    impl FrameSource for Faulty {
        fn len(&self) -> usize {
            100
        }
        fn width(&self) -> u32 {
            64
        }
        fn height(&self) -> u32 {
            48
        }
        fn label(&self) -> String {
            "test".into()
        }
        fn render(&self, k: usize, frame: &mut RgbImage) {
            assert!(k != self.bad, "boom at frame {k}");
            frame.fill(0);
        }
    }

    #[test]
    fn worker_panic_is_an_error_and_keeps_existing_output() {
        let dir = std::env::temp_dir().join(format!("ptv-enc-fail-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("out.mp4");
        std::fs::write(&path, b"previous video").unwrap();

        // Must return (not hang) with the panic message.
        let err = encode_video(&Faulty { bad: 60 }, &path, 10.0, 4).unwrap_err();
        assert!(format!("{err:#}").contains("boom at frame 60"), "{err:#}");

        // The old file is untouched and no partial file is left behind.
        assert_eq!(std::fs::read(&path).unwrap(), b"previous video");
        let leftovers: Vec<_> = std::fs::read_dir(&dir).unwrap().collect();
        assert_eq!(leftovers.len(), 1, "partial file left behind");
        std::fs::remove_dir_all(&dir).unwrap();
    }

    #[test]
    fn single_thread_matches_frame_count() {
        let path = std::env::temp_dir().join(format!("ptv-enc-test1-{}.mp4", std::process::id()));
        encode_video(&Ramp { n: 7 }, &path, 2.0, 1).unwrap();
        let (count, secs, _) = read_back(&path);
        assert_eq!((count, secs), (7, 3.5));
        std::fs::remove_file(&path).unwrap();
    }
}
