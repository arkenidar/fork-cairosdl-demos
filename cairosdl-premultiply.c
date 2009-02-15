/*
 * Copyright (c) 2009  M Joonas Pihlaja
 * Copyright (c) 2009  Paul-Virak Khuong
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
/* A pixel premultiplier and an unpremultiplier using reciprocal
 * multiplication.  It specialises constant runs and solid runs of
 * pixels with low overhead loops and uses only a 1KB table of
 * reciprocals.
 *
 * The unpremultiplier wa snarfed from
 * http://cgit.freedesktop.org/~joonas/unpremultiply
 *
 * See there for other faster or nonportable variations on
 * unpremultiply.  This one is a good all rounder that doesn't take
 * that much data space. */
#include <stddef.h>
#include "cairosdl.h"

/* Pixel format config for a 32 bit pixel with 8 bit components.  Only
 * the location of alpha matters.  Cairo uses ASHIFT 24. */
#define ASHIFT CAIROSDL_ASHIFT
#define RSHIFT ((24 + ASHIFT) % 32)
#define GSHIFT ((16 + ASHIFT) % 32)
#define BSHIFT (( 8 + ASHIFT) % 32)

#define AMASK (255U << ASHIFT)
#define RMASK (255U << RSHIFT)
#define GMASK (255U << GSHIFT)
#define BMASK (255U << BSHIFT)

/* Set to 1 if the input can have superluminant pixels.  Cairo doesn't
 * produce them. */
#define DO_CLAMP_INPUT 0

/* Shift x left by y bits.  Supports negative y for right shifts. */
#define SHIFT(x, y) ((y) < 0 ? (x) >> (-(y)) : (x) << (y))

#define ceil_div(a,b) ((a) + (b)-1) / (b)

/* The reciprocal_table[i] entries are defined by
 *
 * 	0		when i = 0
 *	255 / i		when i > 0
 *
 * represented in fixed point format with RECIPROCAL_BITS of
 * precision and errors rounded up. */
#define RECIPROCAL_BITS 16
static unsigned const reciprocal_table[256] = {
# define R(i)  ((i) ? ceil_div(255*(1<<RECIPROCAL_BITS), (i)) : 0)
# define R1(i) R(i),  R(i+1),   R(i+2),   R(i+3)
# define R2(i) R1(i), R1(i+4),  R1(i+8),  R1(i+12)
# define R3(i) R2(i), R2(i+16), R2(i+32), R2(i+48)
               R3(0), R3(64),   R3(128),  R3(192)
};

/* Transfer num_pixels premultiplied pixels from src[] to dst[] and
 * unpremultiply them. */
static void
unpremultiply_row(
    unsigned       * dst,
    unsigned const * src,
    size_t           num_pixels)
{
    size_t i = 0;
    while (i < num_pixels) {
	/* We want to identify long runs of constant input pixels and
	 * cache the unpremultiplied.  */
        unsigned const_in, const_out;

        /* Diff is the or of all bitwise differences from const_in
	 * during the probe period.  If it is zero after the probe
	 * period then every input pixel was identical in the
	 * probe. */
        unsigned diff = 0;

        /* Accumulator for all alphas of the probe period pixels,
	 * biased to make the sum zero if the */
        unsigned accu = -2*255;

        {
	    unsigned rgba, a, r, g, b, recip;
            rgba = const_in = src[i];
            a = (rgba >> ASHIFT) & 255;
            accu += a;
            r = (rgba >> RSHIFT) & 255;
            g = (rgba >> GSHIFT) & 255;
            b = (rgba >> BSHIFT) & 255;
            recip = reciprocal_table[a];
#if DO_CLAMP_INPUT
	    r = r < a ? r : a;
	    g = g < a ? g : a;
	    b = b < a ? b : a;
#endif
            r = SHIFT(r * recip, RSHIFT - RECIPROCAL_BITS);
            g = SHIFT(g * recip, GSHIFT - RECIPROCAL_BITS);
            b = SHIFT(b * recip, BSHIFT - RECIPROCAL_BITS);
            dst[i] = const_out =
		(r & RMASK) | (g & GMASK) | (b & BMASK) | (rgba & AMASK);
        }

	if (i + 1 == num_pixels)
	    return;

	{
	    unsigned rgba, a, r, g, b, recip;
            rgba = src[i+1];
            a = (rgba >> ASHIFT) & 255;
            accu += a;
            r = (rgba >> RSHIFT) & 255;
            g = (rgba >> GSHIFT) & 255;
            b = (rgba >> BSHIFT) & 255;
            recip = reciprocal_table[a];
#if DO_CLAMP_INPUT
	    r = r < a ? r : a;
	    g = g < a ? g : a;
	    b = b < a ? b : a;
#endif
            diff = rgba ^ const_in;
            r = SHIFT(r * recip, RSHIFT - RECIPROCAL_BITS);
            g = SHIFT(g * recip, GSHIFT - RECIPROCAL_BITS);
            b = SHIFT(b * recip, BSHIFT - RECIPROCAL_BITS);
            dst[i+1] =
		(r & RMASK) | (g & GMASK) | (b & BMASK) | (rgba & AMASK);
        }

        i += 2;

	/* Fall into special cases if we have special
	 * circumstances. */
        if (0 != (accu & diff))
	    continue;

        if (0 == accu) {	/* a run of solid pixels. */
            unsigned in;
            while (AMASK == ((in = src[i]) & AMASK)) {
                dst[i++] = in;
                if (i == num_pixels) return;
            }
        } else if (0 == diff) {	/* a run of constant pixels. */
            while (src[i] == const_in) {
                dst[i++] = const_out;
                if (i == num_pixels) return;
            }
        }
    }
}

/* Transfer num_pixels unpremultiplied pixels from src[] to dst[] and
 * premultiply them. */
static void
premultiply_row(
    unsigned       * dst,
    unsigned const * src,
    size_t           num_pixels)
{
    size_t i = 0;
    while (i < num_pixels) {
	/* We want to identify long runs of constant input pixels and
	 * cache the unpremultiplied.  */
        unsigned const_in, const_out;

        /* Diff is the or of all bitwise differences from const_in
	 * during the probe period.  If it is zero after the probe
	 * period then every input pixel was identical in the
	 * probe. */
        unsigned diff = 0;

        /* Accumulator for all alphas of the probe period pixels,
	 * biased to make the sum zero if the */
        unsigned accu = -2*255;

        {
	    unsigned rgba, a, r, g, b;
            rgba = const_in = src[i];
            a = (rgba >> ASHIFT) & 255;
            accu += a;
            r = (rgba >> RSHIFT) & 255;
            g = (rgba >> GSHIFT) & 255;
            b = (rgba >> BSHIFT) & 255;

            r = SHIFT(r*a*257 + 32768, RSHIFT - 16);
            g = SHIFT(g*a*257 + 32768, GSHIFT - 16);
            b = SHIFT(b*a*257 + 32768, BSHIFT - 16);
            dst[i] = const_out =
		(r & RMASK) | (g & GMASK) | (b & BMASK) | (rgba & AMASK);
        }

	if (i + 1 == num_pixels)
	    return;

	{
	    unsigned rgba, a, r, g, b;
            rgba = src[i+1];
            a = (rgba >> ASHIFT) & 255;
            accu += a;
            r = (rgba >> RSHIFT) & 255;
            g = (rgba >> GSHIFT) & 255;
            b = (rgba >> BSHIFT) & 255;
            diff = rgba ^ const_in;

            r = SHIFT(r*a*257 + 32768, RSHIFT - 16);
            g = SHIFT(g*a*257 + 32768, GSHIFT - 16);
            b = SHIFT(b*a*257 + 32768, BSHIFT - 16);
            dst[i+1] =
		(r & RMASK) | (g & GMASK) | (b & BMASK) | (rgba & AMASK);
        }

        i += 2;

	/* Fall into special cases if we have special
	 * circumstances. */
        if (0 != (accu & diff))
	    continue;

        if (0 == accu) {	/* a run of solid pixels. */
            unsigned in;
            while (AMASK == ((in = src[i]) & AMASK)) {
                dst[i++] = in;
                if (i == num_pixels) return;
            }
        } else if (0 == diff) {	/* a run of constant pixels. */
            while (src[i] == const_in) {
                dst[i++] = const_out;
                if (i == num_pixels) return;
            }
        }
    }
}

void
_cairosdl_blit_and_unpremultiply (
    void       *target_buffer,
    size_t      target_stride,
    void const *source_buffer,
    size_t      source_stride,
    int         width,
    int         height)
{
    unsigned char *target_bytes =
        (unsigned char *)target_buffer;
    unsigned char const *source_bytes =
        (unsigned char const *)source_buffer;
    if (width <= 0)
        return;

    while (height-- > 0) {
        unpremultiply_row ((unsigned *)target_bytes,
                           (unsigned const *)source_bytes,
                           width);

        target_bytes += target_stride;
        source_bytes += source_stride;
    }
}

void
_cairosdl_blit_and_premultiply (
    void       *target_buffer,
    size_t      target_stride,
    void const *source_buffer,
    size_t      source_stride,
    int         width,
    int         height)
{
    unsigned char *target_bytes =
        (unsigned char *)target_buffer;
    unsigned char const *source_bytes =
        (unsigned char const *)source_buffer;
    if (width <= 0)
        return;

    while (height-- > 0) {
        premultiply_row ((unsigned *)target_bytes,
                         (unsigned const *)source_bytes,
                         width);

        target_bytes += target_stride;
        source_bytes += source_stride;
    }
}
