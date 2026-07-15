#ifndef ANIMATION_DATA_HPP
#define ANIMATION_DATA_HPP

#include <cstdint>

#include "Pallet.hpp"

typedef struct {
    uint8_t offset;
    uint8_t color_count;
    uint8_t * colors;
} AnimationDrawInstruction;

typedef struct {
    uint8_t instruction_count;
    AnimationDrawInstruction * instructions;
} AnimationLineData;

typedef struct {
    uint32_t size;
    uint16_t height;
    uint16_t width;
    int16_t offset_y;
    int16_t offset_x;
    uint16_t mystery_bytes;
    bool is_shadow;
    AnimationLineData * lines;
} AnimationFrameData;

typedef struct {
    uint16_t width;
    uint16_t height;
    // Position of the anchor point inside the sprite box, from the ani
    // ini's box (-x0, -y0). Frame offsets are relative to this anchor.
    int16_t origin_x;
    int16_t origin_y;
    uint8_t has_background;
    uint32_t frame_time_in_ms;
    Pallet * pallet;
    uint32_t frame_count;
    AnimationFrameData * frames;
} AnimationData;

#endif  // ANIMATION_DATA_HPP