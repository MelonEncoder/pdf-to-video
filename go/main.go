package main

import (
	"bufio"
	"bytes"
	"errors"
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/gif"
	"image/jpeg"
	"image/png"
	"io"
	"io/fs"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

type Style string

const (
	StyleFrames Style = "FRAMES"
	StyleUp     Style = "UP"
	StyleDown   Style = "DOWN"
	StyleLeft   Style = "LEFT"
	StyleRight  Style = "RIGHT"
)

type Config struct {
	RenderGifs bool
	ReverseSeq bool

	Width  int
	Height int

	FPS      float64
	SPP      float64
	Duration float64

	Style  Style
	Output string
}

func even(n int) int {
	if n%2 == 0 {
		return n
	}
	return n + 1
}

func parseStyle(s string) (Style, error) {
	u := strings.ToUpper(strings.TrimSpace(s))
	switch u {
	case string(StyleFrames), "FRAME":
		return StyleFrames, nil
	case string(StyleUp), "U":
		return StyleUp, nil
	case string(StyleDown), "D":
		return StyleDown, nil
	case string(StyleLeft), "L":
		return StyleLeft, nil
	case string(StyleRight), "R":
		return StyleRight, nil
	default:
		return "", fmt.Errorf("invalid style %q (expected FRAMES|UP|DOWN|LEFT|RIGHT)", s)
	}
}

func usage() {
	fmt.Fprintf(os.Stderr, `ptv-go: PDF/Images -> Video

USAGE:
  ptv-go [options] <input1> [input2 ...]
Inputs may be a PDF path or a directory. Directory items may include images and PDFs.
GIFs inside directories are skipped unless --gif is set.

OPTIONS:
  -r WxH       Output resolution, e.g. 1920x1080 or 0x720 or 1280x0 (0 = auto from first image)
  -f FPS       Output frames per second (default 1.0)
  -s SPP       Seconds per page/image for scroll animations (default 1.0). Used when -d is 0.
  -d SECONDS   Total duration for scroll animations (overrides -s)
  -a STYLE     Animation style: FRAMES, UP, DOWN, LEFT, RIGHT (default FRAMES)
  -o PATH      Output video path (default based on first input, .mp4)
  --gif        Render GIF frames (basic: ignores per-frame delays)
  --rev-seq    Reverse input order when reading directories (and PDFs inside dirs)
`)
}

func parseResolution(res string) (int, int, error) {
	parts := strings.Split(res, "x")
	if len(parts) != 2 {
		return 0, 0, fmt.Errorf("bad resolution %q; expected WxH", res)
	}
	w, err := strconv.Atoi(parts[0])
	if err != nil {
		return 0, 0, fmt.Errorf("bad width in %q: %w", res, err)
	}
	h, err := strconv.Atoi(parts[1])
	if err != nil {
		return 0, 0, fmt.Errorf("bad height in %q: %w", res, err)
	}
	if w < 0 || h < 0 {
		return 0, 0, fmt.Errorf("resolution cannot be negative")
	}
	return w, h, nil
}

func isPDF(path string) bool { return strings.HasSuffix(strings.ToLower(path), ".pdf") }
func isGIF(path string) bool { return strings.HasSuffix(strings.ToLower(path), ".gif") }

func isImageExt(path string) bool {
	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tif", ".tiff", ".webp":
		return true
	default:
		return false
	}
}

// natural sort (numbers in filenames)
var reNum = regexp.MustCompile(`\d+`)

func naturalLess(a, b string) bool {
	aa := reNum.FindAllStringIndex(a, -1)
	bb := reNum.FindAllStringIndex(b, -1)
	i := 0
	j := 0
	pa := 0
	pb := 0
	for i < len(aa) && j < len(bb) {
		// compare non-number prefix
		if a[pa:aa[i][0]] != b[pb:bb[j][0]] {
			return a[pa:aa[i][0]] < b[pb:bb[j][0]]
		}
		// compare numbers
		na, _ := strconv.Atoi(a[aa[i][0]:aa[i][1]])
		nb, _ := strconv.Atoi(b[bb[j][0]:bb[j][1]])
		if na != nb {
			return na < nb
		}
		pa = aa[i][1]
		pb = bb[j][1]
		i++
		j++
	}
	return a < b
}

func listDirPaths(dir string) ([]string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}
	var paths []string
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		p := filepath.Join(dir, e.Name())
		low := strings.ToLower(p)
		if isPDF(low) || isImageExt(low) {
			paths = append(paths, p)
		}
	}
	sort.Slice(paths, func(i, j int) bool { return naturalLess(filepath.Base(paths[i]), filepath.Base(paths[j])) })
	return paths, nil
}

func decodeImage(path string) (image.Image, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	ext := strings.ToLower(filepath.Ext(path))
	switch ext {
	case ".png":
		return png.Decode(f)
	case ".jpg", ".jpeg":
		return jpeg.Decode(f)
	case ".gif":
		return gif.Decode(f)
	default:
		// try auto
		img, _, err := image.Decode(f)
		return img, err
	}
}

func decodeGIFFRames(path string) ([]image.Image, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	g, err := gif.DecodeAll(bytes.NewReader(b))
	if err != nil {
		return nil, err
	}
	out := make([]image.Image, 0, len(g.Image))
	for _, fr := range g.Image {
		out = append(out, fr)
	}
	return out, nil
}

func toRGBA(img image.Image) *image.RGBA {
	b := img.Bounds()
	dst := image.NewRGBA(image.Rect(0, 0, b.Dx(), b.Dy()))
	draw.Draw(dst, dst.Bounds(), img, b.Min, draw.Src)
	return dst
}

// bilinear resize (pure stdlib, reasonably fast for typical slide/PDF sizes)
func resize(img image.Image, w, h int) image.Image {
	if w <= 0 || h <= 0 {
		return img
	}
	src := toRGBA(img)
	sb := src.Bounds()
	sw, sh := sb.Dx(), sb.Dy()
	if sw == w && sh == h {
		return src
	}
	dst := image.NewRGBA(image.Rect(0, 0, w, h))

	// Fixed-point math for speed
	for y := 0; y < h; y++ {
		fy := float64(y) * float64(sh-1) / float64(max(1, h-1))
		y0 := int(math.Floor(fy))
		y1 := minInt(y0+1, sh-1)
		wy := fy - float64(y0)

		for x := 0; x < w; x++ {
			fx := float64(x) * float64(sw-1) / float64(max(1, w-1))
			x0 := int(math.Floor(fx))
			x1 := minInt(x0+1, sw-1)
			wx := fx - float64(x0)

			c00 := src.RGBAAt(x0, y0)
			c10 := src.RGBAAt(x1, y0)
			c01 := src.RGBAAt(x0, y1)
			c11 := src.RGBAAt(x1, y1)

			r := bilerp(float64(c00.R), float64(c10.R), float64(c01.R), float64(c11.R), wx, wy)
			g := bilerp(float64(c00.G), float64(c10.G), float64(c01.G), float64(c11.G), wx, wy)
			bv := bilerp(float64(c00.B), float64(c10.B), float64(c01.B), float64(c11.B), wx, wy)
			a := bilerp(float64(c00.A), float64(c10.A), float64(c01.A), float64(c11.A), wx, wy)

			dst.SetRGBA(x, y, color.RGBA{uint8(r + 0.5), uint8(g + 0.5), uint8(bv + 0.5), uint8(a + 0.5)})
		}
	}
	return dst
}

func bilerp(c00, c10, c01, c11, wx, wy float64) float64 {
	top := c00 + (c10-c00)*wx
	bot := c01 + (c11-c01)*wx
	return top + (bot-top)*wy
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

// For FRAMES: fit inside viewport (may upscale/downscale) preserving aspect ratio.
func scaleToFit(img image.Image, vw, vh int) image.Image {
	b := img.Bounds()
	iw, ih := b.Dx(), b.Dy()
	if iw == 0 || ih == 0 || vw == 0 || vh == 0 {
		return img
	}
	sw := float64(vw) / float64(iw)
	sh := float64(vh) / float64(ih)
	scale := math.Min(sw, sh)
	nw := int(math.Round(float64(iw) * scale))
	nh := int(math.Round(float64(ih) * scale))
	nw = max(1, nw)
	nh = max(1, nh)
	return resize(img, nw, nh)
}

// For UP/DOWN: scale to viewport width, preserve aspect ratio.
func scaleToWidth(img image.Image, vw int) image.Image {
	b := img.Bounds()
	iw, ih := b.Dx(), b.Dy()
	if iw == 0 || ih == 0 || vw == 0 {
		return img
	}
	scale := float64(vw) / float64(iw)
	nh := int(math.Round(float64(ih) * scale))
	nh = max(1, nh)
	return resize(img, vw, nh)
}

// For LEFT/RIGHT: scale to viewport height, preserve aspect ratio.
func scaleToHeight(img image.Image, vh int) image.Image {
	b := img.Bounds()
	iw, ih := b.Dx(), b.Dy()
	if iw == 0 || ih == 0 || vh == 0 {
		return img
	}
	scale := float64(vh) / float64(ih)
	nw := int(math.Round(float64(iw) * scale))
	nw = max(1, nw)
	return resize(img, nw, vh)
}

func padToViewport(img image.Image, vw, vh int) *image.RGBA {
	dst := image.NewRGBA(image.Rect(0, 0, vw, vh))
	draw.Draw(dst, dst.Bounds(), &image.Uniform{color.Black}, image.Point{}, draw.Src)
	b := img.Bounds()
	iw, ih := b.Dx(), b.Dy()
	x := 0
	y := 0
	if vw-iw >= 2 {
		x = (vw - iw) / 2
	}
	if vh-ih >= 2 {
		y = (vh - ih) / 2
	}
	draw.Draw(dst, image.Rect(x, y, x+iw, y+ih), img, b.Min, draw.Over)
	return dst
}

func writeJPG(path string, img image.Image) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()

	// Buffered IO helps.
	bw := bufio.NewWriterSize(f, 1<<20)
	defer bw.Flush()

	// Quality 85–92 is a good default for “slides/video frames”.
	opt := &jpeg.Options{Quality: 90}
	return jpeg.Encode(bw, img, opt)
}

func runCmd(name string, args ...string) error {
	cmd := exec.Command(name, args...)
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	return cmd.Run()
}

func renderPDFToPNGs(pdfPath, outDir string) ([]string, error) {
	// pdftoppm creates outDir/base-1.jpg ...
	base := "page"
	prefix := filepath.Join(outDir, base)
	// Use a higher raster DPI for quality; we will still scale in Go.
	// If this is too slow for huge PDFs, drop to 150.
	args := []string{"-jpeg", "-r", "200", pdfPath, prefix}
	if err := runCmd("pdftoppm", args...); err != nil {
		return nil, err
	}
	// Collect generated pngs
	glob := prefix + "-*.jpg"
	matches, err := filepath.Glob(glob)
	if err != nil {
		return nil, err
	}
	sort.Slice(matches, func(i, j int) bool { return naturalLess(matches[i], matches[j]) })
	return matches, nil
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func buildImageList(inputs []string, conf *Config, tmpDir string) ([]image.Image, error) {
	var imgs []image.Image
	for _, in := range inputs {
		if isPDF(in) {
			pngs, err := renderPDFToPNGs(in, tmpDir)
			if err != nil {
				return nil, fmt.Errorf("render pdf %q: %w", in, err)
			}
			for _, p := range pngs {
				im, err := decodeImage(p)
				if err != nil {
					return nil, fmt.Errorf("decode %q: %w", p, err)
				}
				imgs = append(imgs, im)
			}
			continue
		}
		// directory
		fi, err := os.Stat(in)
		if err != nil {
			return nil, err
		}
		if !fi.IsDir() {
			return nil, fmt.Errorf("input %q is not a pdf or directory", in)
		}
		paths, err := listDirPaths(in)
		if err != nil {
			return nil, err
		}
		// iterate in forward or reverse
		if conf.ReverseSeq {
			for i := len(paths) - 1; i >= 0; i-- {
				p := paths[i]
				if isPDF(p) {
					pngs, err := renderPDFToPNGs(p, tmpDir)
					if err != nil {
						return nil, fmt.Errorf("render pdf %q: %w", p, err)
					}
					for _, pp := range pngs {
						im, err := decodeImage(pp)
						if err != nil {
							return nil, err
						}
						imgs = append(imgs, im)
					}
					continue
				}
				if isGIF(p) {
					if conf.RenderGifs {
						frames, err := decodeGIFFRames(p)
						if err != nil {
							return nil, err
						}
						imgs = append(imgs, frames...)
					} else {
						fmt.Fprintf(os.Stderr, "Note: %q skipped. Use --gif to render gif files (basic support).\n", p)
					}
					continue
				}
				im, err := decodeImage(p)
				if err != nil {
					return nil, err
				}
				imgs = append(imgs, im)
			}
		} else {
			for _, p := range paths {
				if isPDF(p) {
					pngs, err := renderPDFToPNGs(p, tmpDir)
					if err != nil {
						return nil, fmt.Errorf("render pdf %q: %w", p, err)
					}
					for _, pp := range pngs {
						im, err := decodeImage(pp)
						if err != nil {
							return nil, err
						}
						imgs = append(imgs, im)
					}
					continue
				}
				if isGIF(p) {
					if conf.RenderGifs {
						frames, err := decodeGIFFRames(p)
						if err != nil {
							return nil, err
						}
						imgs = append(imgs, frames...)
					} else {
						fmt.Fprintf(os.Stderr, "Note: %q skipped. Use --gif to render gif files (basic support).\n", p)
					}
					continue
				}
				im, err := decodeImage(p)
				if err != nil {
					return nil, err
				}
				imgs = append(imgs, im)
			}
		}
	}
	return imgs, nil
}

func applyScaling(imgs []image.Image, conf *Config) ([]image.Image, error) {
	if len(imgs) == 0 {
		return nil, errors.New("no images loaded")
	}
	// Auto resolution from first image (matching C++ behavior: only fill missing dims)
	first := imgs[0].Bounds()
	if conf.Width == 0 {
		conf.Width = first.Dx()
	}
	if conf.Height == 0 {
		conf.Height = first.Dy()
	}
	conf.Width = even(conf.Width)
	conf.Height = even(conf.Height)

	out := make([]image.Image, 0, len(imgs))
	for _, im := range imgs {
		var scaled image.Image
		switch conf.Style {
		case StyleFrames:
			scaled = scaleToFit(im, conf.Width, conf.Height)
		case StyleUp, StyleDown:
			scaled = scaleToWidth(im, conf.Width)
		case StyleLeft, StyleRight:
			scaled = scaleToHeight(im, conf.Height)
		default:
			return nil, fmt.Errorf("unknown style %q", conf.Style)
		}
		out = append(out, scaled)
	}
	return out, nil
}

func writeFramesSequence(imgs []image.Image, conf *Config, framesDir string) (int, error) {
	for i, im := range imgs {
		vp := padToViewport(im, conf.Width, conf.Height)
		fn := filepath.Join(framesDir, fmt.Sprintf("frame%06d.jpg", i+1))
		if err := writeJPG(fn, vp); err != nil {
			return 0, err
		}
	}
	return len(imgs), nil
}

func writeFramesScroll(imgs []image.Image, conf *Config, framesDir string) (int, error) {
	if len(imgs) == 0 {
		return 0, errors.New("no images")
	}

	vw, vh := conf.Width, conf.Height

	// Build stitched canvas
	switch conf.Style {
	case StyleUp, StyleDown:
		totalH := vh
		for _, im := range imgs {
			totalH += im.Bounds().Dy()
		}
		canvas := image.NewRGBA(image.Rect(0, 0, vw, totalH))
		draw.Draw(canvas, canvas.Bounds(), &image.Uniform{color.Black}, image.Point{}, draw.Src)

		y := vh
		for _, im := range imgs {
			b := im.Bounds()
			draw.Draw(canvas, image.Rect(0, y, b.Dx(), y+b.Dy()), im, b.Min, draw.Over)
			y += b.Dy()
		}

		distance := totalH - vh
		if distance < 0 {
			distance = 0
		}

		var totalFrames int
		if conf.Duration > 0 {
			totalFrames = int(math.Round(conf.FPS * conf.Duration))
		} else {
			totalFrames = int(math.Round(conf.FPS * conf.SPP * float64(len(imgs))))
		}
		if totalFrames < 1 {
			totalFrames = 1
		}

		pxPerFrame := float64(distance) / float64(totalFrames)
		if pxPerFrame <= 0 {
			pxPerFrame = 1.0
		}
		fmt.Printf("Pixels per frame: %.4f\n", pxPerFrame)

		frame := image.NewRGBA(image.Rect(0, 0, vw, vh))
		for fi := 0; fi < totalFrames; fi++ {
			var y0 int
			switch conf.Style {
			case StyleUp:
				y0 = int(math.Floor(float64(fi) * pxPerFrame))
			case StyleDown:
				y0 = distance - int(math.Floor(float64(fi)*pxPerFrame))
			}
			if y0 < 0 {
				y0 = 0
			}
			if y0 > distance {
				y0 = distance
			}
			draw.Draw(frame, frame.Bounds(), canvas, image.Point{0, y0}, draw.Src)
			fn := filepath.Join(framesDir, fmt.Sprintf("frame%06d.jpg", fi+1))
			if err := writeJPG(fn, frame); err != nil {
				return 0, err
			}
		}
		return totalFrames, nil

	case StyleLeft, StyleRight:
		totalW := vw
		for _, im := range imgs {
			totalW += im.Bounds().Dx()
		}
		canvas := image.NewRGBA(image.Rect(0, 0, totalW, vh))
		draw.Draw(canvas, canvas.Bounds(), &image.Uniform{color.Black}, image.Point{}, draw.Src)

		x := vw
		for _, im := range imgs {
			b := im.Bounds()
			draw.Draw(canvas, image.Rect(x, 0, x+b.Dx(), b.Dy()), im, b.Min, draw.Over)
			x += b.Dx()
		}

		distance := totalW - vw
		if distance < 0 {
			distance = 0
		}

		var totalFrames int
		if conf.Duration > 0 {
			totalFrames = int(math.Round(conf.FPS * conf.Duration))
		} else {
			totalFrames = int(math.Round(conf.FPS * conf.SPP * float64(len(imgs))))
		}
		if totalFrames < 1 {
			totalFrames = 1
		}

		pxPerFrame := float64(distance) / float64(totalFrames)
		if pxPerFrame <= 0 {
			pxPerFrame = 1.0
		}
		fmt.Printf("Pixels per frame: %.4f\n", pxPerFrame)

		frame := image.NewRGBA(image.Rect(0, 0, vw, vh))
		for fi := 0; fi < totalFrames; fi++ {
			var x0 int
			switch conf.Style {
			case StyleLeft:
				x0 = int(math.Floor(float64(fi) * pxPerFrame))
			case StyleRight:
				x0 = distance - int(math.Floor(float64(fi)*pxPerFrame))
			}
			if x0 < 0 {
				x0 = 0
			}
			if x0 > distance {
				x0 = distance
			}
			draw.Draw(frame, frame.Bounds(), canvas, image.Point{x0, 0}, draw.Src)
			fn := filepath.Join(framesDir, fmt.Sprintf("frame%06d.jpg", fi+1))
			if err := writeJPG(fn, frame); err != nil {
				return 0, err
			}
		}
		return totalFrames, nil
	default:
		return 0, fmt.Errorf("scroll not supported for style %q", conf.Style)
	}
}

func encodeVideoFFmpeg(framesDir string, fps float64, outPath string) error {
	// Ensure output directory exists
	if dir := filepath.Dir(outPath); dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}

	inPattern := filepath.Join(framesDir, "frame%06d.jpg")
	args := []string{
		"-y",
		"-framerate", fmt.Sprintf("%.6g", fps),
		"-i", inPattern,
		"-c:v", "libx264",
		"-pix_fmt", "yuv420p",
		"-movflags", "+faststart",
		outPath,
	}
	return runCmd("ffmpeg", args...)
}

func main() {
	flag.Usage = usage

	var resStr string
	var fps float64
	var spp float64
	var dur float64
	var styleStr string
	var out string
	var gifFlag bool
	var revFlag bool

	flag.StringVar(&resStr, "r", "1280x720", "resolution WxH (0 allowed for auto)")
	flag.Float64Var(&fps, "f", 1.0, "fps")
	flag.Float64Var(&spp, "s", 1.0, "seconds per page for scroll")
	flag.Float64Var(&dur, "d", 0.0, "total duration for scroll")
	flag.StringVar(&styleStr, "a", "FRAMES", "style (FRAMES|UP|DOWN|LEFT|RIGHT)")
	flag.StringVar(&out, "o", "", "output path (.mp4)")
	flag.BoolVar(&gifFlag, "gif", false, "render gif frames")
	flag.BoolVar(&revFlag, "rev-seq", false, "reverse sequence (directories and pdfs inside dirs)")
	flag.Parse()

	inputs := flag.Args()
	if len(inputs) == 0 {
		usage()
		os.Exit(2)
	}

	w, h, err := parseResolution(resStr)
	if err != nil {
		fmt.Fprintln(os.Stderr, "<!> "+err.Error())
		os.Exit(2)
	}
	style, err := parseStyle(styleStr)
	if err != nil {
		fmt.Fprintln(os.Stderr, "<!> "+err.Error())
		os.Exit(2)
	}

	conf := &Config{
		RenderGifs: gifFlag,
		ReverseSeq: revFlag,
		Width:      w,
		Height:     h,
		FPS:        fps,
		SPP:        spp,
		Duration:   dur,
		Style:      style,
		Output:     out,
	}

	// Derive output if missing
	if conf.Output == "" {
		first := inputs[0]
		if isPDF(first) {
			conf.Output = strings.TrimSuffix(first, filepath.Ext(first)) + ".mp4"
		} else {
			conf.Output = strings.TrimRight(first, "/")
			conf.Output = conf.Output + ".mp4"
		}
	}

	// Mimic C++: duration only used for scroll styles.
	if conf.Duration != 0 && conf.Style == StyleFrames {
		fmt.Fprintln(os.Stderr, "Note: -d ignored for FRAMES style.")
		conf.Duration = 0
	}

	start := time.Now()

	// temp dirs
	workDir, err := os.MkdirTemp("", "ptv-go-*")
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
	defer os.RemoveAll(workDir)

	renderDir := filepath.Join(workDir, "render")
	framesDir := filepath.Join(workDir, "frames")
	_ = os.MkdirAll(renderDir, 0o755)
	_ = os.MkdirAll(framesDir, 0o755)

	fmt.Println("Loading Images...")
	imgs, err := buildImageList(inputs, conf, renderDir)
	if err != nil {
		fmt.Fprintln(os.Stderr, "<!> "+err.Error())
		os.Exit(1)
	}

	imgs, err = applyScaling(imgs, conf)
	if err != nil {
		fmt.Fprintln(os.Stderr, "<!> "+err.Error())
		os.Exit(1)
	}

	fmt.Printf("Output: %s\n", conf.Output)
	fmt.Printf("Resolution: %dx%d\n", conf.Width, conf.Height)
	fmt.Printf("FPS: %.6g\n", conf.FPS)
	if conf.Style != StyleFrames {
		if conf.Duration > 0 {
			fmt.Printf("Duration: %.6gs\n", conf.Duration)
		} else {
			fmt.Printf("SPP: %.6g\n", conf.SPP)
		}
	}
	fmt.Printf("Animated: %s\n", conf.Style)

	// Generate frames
	var frameCount int
	switch conf.Style {
	case StyleFrames:
		frameCount, err = writeFramesSequence(imgs, conf, framesDir)
	default:
		frameCount, err = writeFramesScroll(imgs, conf, framesDir)
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "<!> "+err.Error())
		os.Exit(1)
	}
	fmt.Printf("Frames: %d\n", frameCount)

	// Encode video
	fmt.Println("Encoding video...")
	if err := encodeVideoFFmpeg(framesDir, conf.FPS, conf.Output); err != nil {
		fmt.Fprintln(os.Stderr, "<!> ffmpeg failed:", err)
		os.Exit(1)
	}

	d := time.Since(start)
	fmt.Printf("Time: %dm %ds\n", int(d.Minutes()), int(d.Seconds())%60)
}

// Ensure the compiler keeps these imports if users extend formats.
var _ fs.FileInfo
var _ io.Reader
