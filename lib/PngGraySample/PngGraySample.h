#pragma once

#include <cstdint>

// Reads a single grayscale sample from a PNGdec scanline at column `x` and
// returns it scaled to a full 0..255 intensity.
//
// PNG grayscale images are encoded at 1, 2, 4, 8, or 16 bits per sample (PNG
// spec, IHDR bit depth for color type 0). PNGdec delivers each scanline
// packed at that native bit depth:
//   - 1/2/4-bit: samples are packed MSB-first, multiple samples per byte.
//   - 8-bit: one byte per sample.
//   - 16-bit: two bytes per sample, big-endian per spec; we use the high
//     byte as the 8-bit intensity, matching how the rest of the rendering
//     pipeline treats channel data.
// Any other bpp is not a valid PNG grayscale bit depth and should not occur
// from PNGdec; rather than trust that invariant and read out of bounds or
// misinterpret the buffer, fall back to a neutral mid-gray.
inline uint8_t pngUnpackGraySample(const uint8_t* pixels, const int x, const int bpp) {
  switch (bpp) {
    case 8:
      return pixels[x];
    case 16:
      return pixels[x * 2];
    case 1:
    case 2:
    case 4: {
      const int ppb = 8 / bpp;                          // samples per byte
      const int mask = (1 << bpp) - 1;                  // max sample value
      const uint8_t byte = pixels[x / ppb];
      const int shift = (ppb - 1 - (x % ppb)) * bpp;     // MSB-first within the byte
      const int sample = (byte >> shift) & mask;
      return static_cast<uint8_t>(sample * 255 / mask);  // scale sample to 0..255
    }
    default:
      return 128;  // unexpected bit depth; avoid reading out of bounds or misinterpreting data
  }
}
