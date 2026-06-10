#pragma once

#include <cstdint>

#include "image_io.h"  // RgbaImage

// How a source image is fit into a fixed target size.
enum class ResizeMethod {
    Stretch,            // bilinear stretch to fill target (distorts aspect)
    CenterTransparent,  // fit inside target (scale down if larger) + center, alpha=0 padding
};

// 9-anchor placement grid for overlay compositing.
enum class Anchor9 {
    TopLeft, TopCenter, TopRight,
    MidLeft, Center, MidRight,
    BottomLeft, BottomCenter, BottomRight,
};

// Produce a target-sized RGBA8 image from src.
//  Stretch:           bilinear resize to (tw,th).
//  CenterTransparent: if src fits within (tw,th) keep it 1:1 and center; if any
//                     side exceeds the target, scale down preserving aspect to
//                     fit inside, then center. Padding is transparent (alpha=0).
// Returns an empty image (width==0) for degenerate inputs.
RgbaImage transformToTarget(const uint8_t* srcPixels, int srcW, int srcH,
                            int targetW, int targetH, ResizeMethod method);

// Src-over alpha composite of `overlay` onto a copy of `base`.
//  overlay is first scaled to scalePct% of its size, then placed at `anchor`
//  inset by marginPx, blended at global opacity[0..1] multiplied into its alpha.
//  Overlay pixels outside base bounds are clipped. Returns the composited copy.
RgbaImage compositeOverlay(const RgbaImage& base, const RgbaImage& overlay,
                           Anchor9 anchor, int marginPx, int scalePct, float opacity);

// Return a copy of src with a solid (RGBA) border.
//  expandCanvas == false: draw a `thicknessPx` frame inside src's own bounds.
//  expandCanvas == true:  grow the canvas by thicknessPx on every side, fill the
//                         new margin with the color and center the original.
RgbaImage addBorder(const RgbaImage& src, int thicknessPx,
                    uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool expandCanvas);
