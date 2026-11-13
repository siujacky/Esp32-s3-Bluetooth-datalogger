#pragma once

#include <stddef.h>
#include <stdint.h>

namespace jc3248 {

struct Point { double x, y; };
struct Calibration {
    // Landscape pixels: x[0] * rawX + x[1] * rawY + x[2], and likewise y.
    double x[3] = {};
    double y[3] = {};
};

// Corners are supplied in display TL, TR, BR, BL order. Raw coordinates may
// be rotated or mirrored. Failed operations leave their outputs untouched.
bool fitCalibration(const Point raw[4], const Point target[4],
                    Calibration &out, double &rms,unsigned width=480,unsigned height=320);
bool validCalibration(const Calibration &calibration);
// Publishes finite, unclamped landscape coordinates; callers clip to their UI.
bool mapCalibration(const Calibration &calibration, double rawX, double rawY,
                    Point &out);

// Fixed little-endian record, independent of compiler struct layout:
// "JCC1", u16 version=1, u16 target width/height, u16 reserved=0,
// six IEEE-754 binary64 coefficients (x then y), IEEE CRC32 of bytes 0..59.
constexpr size_t CALIBRATION_BYTES = 64;
// Defaults retain the original JC landscape record byte for byte.
bool encodeCalibration(const Calibration &calibration, uint8_t *out, size_t size,
                       unsigned width=480,unsigned height=320);
bool decodeCalibration(const uint8_t *data, size_t size, Calibration &out,
                       unsigned width=480,unsigned height=320);

} // namespace jc3248
