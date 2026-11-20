//! H.264 encoding (OpenH264, statically compiled) and MP4 muxing (pure Rust).

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
use std::fs::File;
use std::io::BufWriter;
use std::path::{Path, PathBuf};

/// Anything that accepts rendered frames. The real one is [`Mp4Encoder`]; tests use a `Vec`.
pub trait FrameSink {
    fn write_frame(&mut self, frame: &RgbImage) -> Result<()>;
}

/// Track timescale (ticks per second). 90 kHz is the MPEG convention and represents
/// common and fractional frame rates (e.g. 29.97, 0.5) accurately.
const TIMESCALE: u32 = 90_000;
const TRACK_ID: u32 = 1;

pub struct Mp4Encoder {
    encoder: Encoder,
    yuv: YUVBuffer,
    path: PathBuf,
    width: u32,
    height: u32,
    writer: Option<Mp4Writer<BufWriter<File>>>,
    /// Encoded frame not yet written, so skipped frames can extend its duration.
    pending: Option<Mp4Sample>,
    next_start: u64,
    frame_ticks: u32,
}

impl Mp4Encoder {
    pub fn new(path: &Path, width: u32, height: u32, fps: f64) -> Result<Self> {
        if !width.is_multiple_of(2) || !height.is_multiple_of(2) || width == 0 || height == 0 {
            bail!("video size must be even and non-zero, got {width}x{height}");
        }
        if width > u16::MAX as u32 || height > u16::MAX as u32 {
            bail!("video size {width}x{height} is too large");
        }
        // A keyframe roughly every 5 seconds keeps seeking responsive.
        let gop = (fps * 5.0).round().clamp(1.0, u32::MAX as f64) as u32;
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
            .intra_frame_period(IntraFramePeriod::from_num_frames(gop))
            // openh264's RGB->YUV conversion uses BT.601 limited-range coefficients; tag the
            // stream to match so players don't assume BT.709 and shift the colours.
            .vui(VuiConfig::bt601())
            .debug(false);
        let encoder = Encoder::with_api_config(OpenH264API::from_source(), config)
            .map_err(|e| anyhow!("could not start H.264 encoder: {e}"))?;

        Ok(Self {
            encoder,
            yuv: YUVBuffer::new(width as usize, height as usize),
            path: path.to_path_buf(),
            width,
            height,
            writer: None,
            pending: None,
            next_start: 0,
            frame_ticks: ((TIMESCALE as f64 / fps).round() as u32).max(1),
        })
    }

    /// Flush the last frame and write the MP4 index. Must be called or the file is unplayable.
    pub fn finish(mut self) -> Result<()> {
        self.flush_pending()?;
        let mut writer = self
            .writer
            .take()
            .context("no frames were rendered, nothing to write")?;
        writer.write_end().context("finalising MP4")?;
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

    fn open_writer(&mut self, sps: Vec<u8>, pps: Vec<u8>) -> Result<()> {
        let file = File::create(&self.path)
            .with_context(|| format!("creating '{}'", self.path.display()))?;
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

impl FrameSink for Mp4Encoder {
    fn write_frame(&mut self, frame: &RgbImage) -> Result<()> {
        if frame.dimensions() != (self.width, self.height) {
            bail!(
                "frame is {:?}, expected {}x{}",
                frame.dimensions(),
                self.width,
                self.height
            );
        }
        self.yuv.read_rgb(RgbSliceU8::new(
            frame.as_raw(),
            (self.width as usize, self.height as usize),
        ));
        let bs = self
            .encoder
            .encode(&self.yuv)
            .map_err(|e| anyhow!("H.264 encoding failed: {e}"))?;
        let frame_type = bs.frame_type();
        let avcc = AvccFrame::from_bitstream(&bs);

        if frame_type == FrameType::Skip || avcc.data.is_empty() {
            // Nothing new to show: hold the previous frame for longer.
            if let Some(prev) = self.pending.as_mut() {
                prev.duration += self.frame_ticks;
            }
            self.next_start += self.frame_ticks as u64;
            return Ok(());
        }

        if self.writer.is_none() {
            let sps = avcc
                .sps
                .clone()
                .context("encoder did not emit an SPS on the first frame")?;
            let pps = avcc
                .pps
                .clone()
                .context("encoder did not emit a PPS on the first frame")?;
            self.open_writer(sps, pps)?;
        }
        self.flush_pending()?;
        self.pending = Some(Mp4Sample {
            start_time: self.next_start,
            duration: self.frame_ticks,
            rendering_offset: 0,
            is_sync: matches!(frame_type, FrameType::IDR | FrameType::I),
            bytes: Bytes::from(avcc.data),
        });
        self.next_start += self.frame_ticks as u64;
        Ok(())
    }
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
    fn encodes_playable_frames() {
        let path = std::env::temp_dir().join(format!("ptv-enc-test-{}.mp4", std::process::id()));
        let mut enc = Mp4Encoder::new(&path, 64, 48, 2.0).unwrap();
        for i in 0..4u8 {
            enc.write_frame(&RgbImage::from_pixel(64, 48, image::Rgb([i * 60, 0, 0])))
                .unwrap();
        }
        enc.finish().unwrap();

        let file = File::open(&path).unwrap();
        let size = file.metadata().unwrap().len();
        let reader = mp4::Mp4Reader::read_header(std::io::BufReader::new(file), size).unwrap();
        let track = reader.tracks().get(&TRACK_ID).unwrap();
        assert_eq!(track.sample_count(), 4);
        assert_eq!(track.width(), 64);
        assert_eq!(reader.duration().as_secs_f64(), 2.0);
        std::fs::remove_file(&path).unwrap();
    }
}
