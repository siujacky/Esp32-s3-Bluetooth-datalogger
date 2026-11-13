#pragma once
#include <stddef.h>
#include <stdint.h>
namespace jc3248 {
constexpr unsigned WIDTH=320,HEIGHT=480;
// Convert a calibrated logical point between GFX rotations on the same panel.
// Invalid geometry/coordinates leave the output untouched.
bool transformTouch(uint16_t x,uint16_t y,unsigned fromRotation,unsigned toRotation,
                    unsigned width,unsigned height,uint16_t &outX,uint16_t &outY);
// Decode the eight-byte AXS15231B single-contact response. On malformed
// data leave all outputs untouched; zero contacts publishes a release.
bool decodeTouch(const uint8_t *data,size_t size,unsigned rotation,uint16_t &x,uint16_t &y,bool &down,
                 unsigned width=WIDTH,unsigned height=HEIGHT);
}
