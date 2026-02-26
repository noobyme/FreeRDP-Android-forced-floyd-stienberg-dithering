/*
 * android_dither.h
 *
 * 1-bit Floyd-Steinberg dithering for the FreeRDP Android client.
 * Drop alongside android_freerdp.c and #include it at the top of that file.
 *
 * Pixel format: PIXEL_FORMAT_RGBX32 — byte order in memory is
 *   byte 0 = Red, byte 1 = Green, byte 2 = Blue, byte 3 = (unused / padding)
 * This matches the format passed to gdi_init() in android_post_connect().
 *
 * Luminance:  Rec.601   0.299·R + 0.587·G + 0.114·B
 * Threshold:  191 / 255  (~75 %) — pixel must be quite bright to become white
 * Weights:    Floyd-Steinberg
 *               right        7/16
 *               lower-left   3/16
 *               lower        5/16
 *               lower-right  1/16
 */

#pragma once

#include <stdlib.h>
#include <string.h>
#include <winpr/wtypes.h>  /* BYTE */

/**
 * android_dither_rect() - apply 1-bit Floyd-Steinberg dithering in-place.
 *
 * @surface  pointer to pixel (0,0) of the full framebuffer  (gdi->primary_buffer)
 * @stride   bytes per scanline of the full framebuffer      (gdi->stride)
 * @x        left edge of the dirty rectangle (pixels)
 * @y        top  edge of the dirty rectangle (pixels)
 * @w        width  of the dirty rectangle (pixels)
 * @h        height of the dirty rectangle (pixels)
 *
 * Only pixels inside [x, x+w) x [y, y+h) are touched.
 */
static inline void android_dither_rect(BYTE* surface, int stride,
                                       int x, int y, int w, int h)
{
    if (!surface || w <= 0 || h <= 0)
        return;

    /*
     * Two rolling error scanlines, each (w + 2) floats wide.
     * The extra slots on each side mean neighbour writes at
     * col = -1 and col = w are always in bounds.
     * calloc() zero-initialises, seeding row 0 with no carried error.
     */
    const int errLen = w + 2;
    float* errCur = (float*)calloc((size_t)errLen, sizeof(float));
    float* errNxt = (float*)calloc((size_t)errLen, sizeof(float));

    if (!errCur || !errNxt)
    {
        free(errCur);
        free(errNxt);
        return;
    }

    for (int row = 0; row < h; ++row)
    {
        memset(errNxt, 0, (size_t)errLen * sizeof(float));

        BYTE* line = surface + (y + row) * stride + x * 4;

        for (int col = 0; col < w; ++col)
        {
            /* RGBX: px[0]=R  px[1]=G  px[2]=B  px[3]=unused */
            BYTE* px = line + col * 4;

            float luma     = 0.299f * (float)px[0]
                           + 0.587f * (float)px[1]
                           + 0.114f * (float)px[2];
            float adjusted = luma + errCur[col + 1];

            /* Quantise: threshold at 75% of 255 = 191 */
            BYTE  quantized = (adjusted >= 191.0f) ? 255 : 0;
            float error     = adjusted - (float)quantized;

            /* Write grey back; leave the padding byte untouched */
            px[0] = px[1] = px[2] = quantized;

            /* Distribute quantisation error (Floyd-Steinberg) */
            errCur[col + 2] += error * (7.0f / 16.0f); /* right        */
            errNxt[col    ] += error * (3.0f / 16.0f); /* lower-left   */
            errNxt[col + 1] += error * (5.0f / 16.0f); /* lower        */
            errNxt[col + 2] += error * (1.0f / 16.0f); /* lower-right  */
        }

        float* tmp = errCur;
        errCur = errNxt;
        errNxt = tmp;
    }

    free(errCur);
    free(errNxt);
}
