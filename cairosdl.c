/*
 * Copyright (c) 2009  M Joonas Pihlaja
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
#include <assert.h>
#include "cairosdl.h"

/* from cairo-unpremultiply.c */
void
_cairosdl_blit_and_unpremultiply (
    void       *target_buffer,
    size_t      target_stride,
    void const *source_buffer,
    size_t      source_stride,
    int         width,
    int         height);

/* from cairo-premultiply.c */
void
_cairosdl_blit_and_premultiply (
    void       *target_buffer,
    size_t      target_stride,
    void const *source_buffer,
    size_t      source_stride,
    int         width,
    int         height);

/*
 * Surface functions
 */

/* We're hanging the SDL_Surface as a user datum on the
 * cairo_surface_t representing it using this key.  */
static cairo_user_data_key_t const CAIROSDL_TARGET_KEY[1];

static void
sdl_surface_destroy_func (void *param)
{
    SDL_Surface *sdl_surface = (SDL_Surface *)param;
    if (sdl_surface != NULL)
        SDL_FreeSurface (sdl_surface);
}

cairo_surface_t *
cairosdl_surface_create (
    SDL_Surface *sdl_surface)
{
    cairo_surface_t *target;
    cairo_format_t format;
    int is_dirty;

    /* Cairo only supports a limited number of pixels formats.  Make
     * sure the surface format is compatible. */
    if (sdl_surface->format->BytesPerPixel != 4 ||
        sdl_surface->format->BitsPerPixel != 32)
        goto unsupported_format;

    if (sdl_surface->format->Rmask != CAIROSDL_RMASK ||
        sdl_surface->format->Gmask != CAIROSDL_GMASK ||
        sdl_surface->format->Bmask != CAIROSDL_BMASK)
        goto unsupported_format;

    switch (sdl_surface->format->Amask) {
    case CAIROSDL_AMASK:
        format = CAIRO_FORMAT_ARGB32;
        break;
    case 0:
        format = CAIRO_FORMAT_RGB24;
        break;
    default:
        goto unsupported_format;
    }

    /* Make the target point to either the SDL_Surface's data itself
     * or a shadow image surface if we need to unpremultiply pixels. */
    if (format == CAIRO_FORMAT_RGB24) {
        /* The caller is expected to have locked the surface (_if_ it
         * needs locking) so that sdl_surface->pixels is valid and
         * constant for the lifetime of the cairo_surface_t.  However,
         * we're told not to call any OS functions when a surface is
         * locked, so we really shouldn't call
         * cairo_image_surface_create () as it will malloc, so really
         * if the surface needs locking this shouldn't be used.
         *
         * However, it turns out malloc is actually safe on many (all?)
         * platforms so we'll just go ahead anyway. */
        target = cairo_image_surface_create_for_data (sdl_surface->pixels,
                                                      format,
                                                      sdl_surface->w,
                                                      sdl_surface->h,
                                                      sdl_surface->pitch);
        is_dirty = 0;
    }
    else {
        /* Need a shadow image surface. */
        target = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                             sdl_surface->w,
                                             sdl_surface->h);
        is_dirty = 1;
    }

    if (cairo_surface_status (target) == CAIRO_STATUS_SUCCESS) {
        sdl_surface->refcount++;
        cairo_surface_set_user_data (target,
                                     CAIROSDL_TARGET_KEY,
                                     sdl_surface,
                                     sdl_surface_destroy_func);

        if (is_dirty)
            cairosdl_surface_mark_dirty (target);
    }

    return target;

 unsupported_format:
    /* Nasty kludge to get a cairo surface in CAIRO_INVALID_FORMAT
     * state. */
    return cairo_image_surface_create (
        (cairo_format_t)-1, 0, 0);
}

SDL_Surface *
cairosdl_surface_get_target (
    cairo_surface_t *surface)
{
    return cairo_surface_get_user_data (surface, CAIROSDL_TARGET_KEY);
}

static cairo_status_t
_cairosdl_surface_obtain_SDL_buffer(
    cairo_surface_t *surface,
    unsigned char  **OUT_buffer,
    size_t          *OUT_stride,
    size_t          *OUT_width,
    size_t          *OUT_height)
{
    SDL_Surface *sdl_surface = cairosdl_surface_get_target (surface);
    if (sdl_surface == NULL)
        return CAIRO_STATUS_NULL_POINTER;

    if (OUT_buffer)
        *OUT_buffer = sdl_surface->pixels;
    if (OUT_stride)
        *OUT_stride = sdl_surface->pitch;
    if (OUT_width)
        *OUT_width = sdl_surface->w;
    if (OUT_height)
        *OUT_height = sdl_surface->h;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
_cairosdl_surface_obtain_shadow_buffer(
    cairo_surface_t *surface,
    unsigned char  **OUT_buffer,
    size_t          *OUT_stride,
    size_t          *OUT_width,
    size_t          *OUT_height)
{
    cairo_format_t format;

    if (cairo_surface_get_type (surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return CAIRO_STATUS_SURFACE_TYPE_MISMATCH;

    format = cairo_image_surface_get_format (surface);
    if (format != CAIRO_FORMAT_ARGB32)
        return CAIRO_STATUS_INVALID_FORMAT;

    if (OUT_buffer != NULL)
        *OUT_buffer = cairo_image_surface_get_data (surface);
    if (OUT_stride != NULL)
        *OUT_stride = cairo_image_surface_get_stride (surface);
    if (OUT_width != NULL)
        *OUT_width = cairo_image_surface_get_width (surface);
    if (OUT_height != NULL)
        *OUT_height = cairo_image_surface_get_height (surface);
    return CAIRO_STATUS_SUCCESS;
}

void
cairosdl_surface_flush_rects (
    cairo_surface_t *surface,
    int              num_rects,
    SDL_Rect const  *rects)
{
    unsigned char *source_bytes;
    size_t source_stride;
    size_t source_width;
    size_t source_height;

    unsigned char *target_bytes;
    size_t target_stride;
    size_t target_width;
    size_t target_height;

    int width, height;
    cairo_status_t status;

    if (num_rects <= 0)
        return;

    cairo_surface_flush (surface);

    status = _cairosdl_surface_obtain_SDL_buffer (surface,
                                                  &target_bytes,
                                                  &target_stride,
                                                  &target_width,
                                                  &target_height);
    if (status != CAIRO_STATUS_SUCCESS)
        return;                 /* no buffer -> nothing to do */

    status = _cairosdl_surface_obtain_shadow_buffer (surface,
                                                     &source_bytes,
                                                     &source_stride,
                                                     &source_width,
                                                     &source_height);
    if (status != CAIRO_STATUS_SUCCESS)
        return;                 /* no buffer -> nothing to do */

    width = source_width < target_width ? source_width : target_width;
    height = source_height < target_height ? source_height : target_height;
    assert(width >= 0 && height >= 0);

    while (num_rects-- > 0) {
        Sint32 x = rects->x;
        Sint32 y = rects->y;
        Sint32 w = rects->w;
        Sint32 h = rects->h;
        rects++;

        if (x <= 0) { w += x; x = 0; }
        if (y <= 0) { h += y; y = 0; }
        if (x + w >= width) w = width - x;
        if (y + h >= height) h = height - y;

        _cairosdl_blit_and_unpremultiply (
            target_bytes + target_stride*y + x, target_stride,
            source_bytes + source_stride*y + x, source_stride,
            w, h);
    }
}

void
cairosdl_surface_mark_dirty_rects (
    cairo_surface_t *surface,
    int              num_rects,
    SDL_Rect const  *rects)
{
    unsigned char *source_bytes = NULL;
    size_t source_stride = 0;
    size_t source_width = 32767;
    size_t source_height = 32767;

    unsigned char *target_bytes = NULL;
    size_t target_stride = 0;
    size_t target_width = 32767;
    size_t target_height = 32767;

    int width, height;
    cairo_status_t status;
    int have_buffers = 1;

    if (num_rects <= 0)
        return;

    status = _cairosdl_surface_obtain_SDL_buffer (surface,
                                                  &source_bytes,
                                                  &source_stride,
                                                  &source_width,
                                                  &source_height);
    if (status != CAIRO_STATUS_SUCCESS)
        have_buffers = 0;

    status = _cairosdl_surface_obtain_shadow_buffer (surface,
                                                     &target_bytes,
                                                     &target_stride,
                                                     &target_width,
                                                     &target_height);
    if (status != CAIRO_STATUS_SUCCESS)
        have_buffers = 0;

    width = source_width < target_width ? source_width : target_width;
    height = source_height < target_height ? source_height : target_height;
    assert(width >= 0 && height >= 0);

    while (num_rects-- > 0) {
        Sint32 x = rects->x;
        Sint32 y = rects->y;
        Sint32 w = rects->w;
        Sint32 h = rects->h;
        rects++;

        if (x <= 0) { w += x; x = 0; }
        if (y <= 0) { h += y; y = 0; }
        if (x >= width || y >= height) continue;
        if (x + w >= width) w = width - x;
        if (y + h >= height) h = height - y;
        if (w <= 0 || h <= 0) continue;

        if (have_buffers) {
            _cairosdl_blit_and_premultiply (
                target_bytes + target_stride*y + x, target_stride,
                source_bytes + source_stride*y + x, source_stride,
                w, h);
        }

        cairo_surface_mark_dirty_rectangle (surface, x, y, w, h);
    }
}

static SDL_Rect
make_rect(int x, int y, int w, int h)
{
    static SDL_Rect const empty_rect = {0,0,0,0};
    SDL_Rect r;

    /* Clamp the rect to [0,32768). */
    if (w <= 0 || h <= 0)
        return empty_rect;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if ((unsigned)w > 32767) w = 32767;
    if ((unsigned)h > 32767) h = 32767;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

void
cairosdl_surface_flush_rect (
    cairo_surface_t *surface,
    int              x,
    int              y,
    int              w,
    int              h)
{
    SDL_Rect rect = make_rect(x, y, w, h);
    cairosdl_surface_flush_rects (surface, 1, &rect);
}

void
cairosdl_surface_mark_dirty_rect (
    cairo_surface_t *surface,
    int              x,
    int              y,
    int              w,
    int              h)
{
    SDL_Rect rect = make_rect(x, y, w, h);
    cairosdl_surface_mark_dirty_rects (surface, 1, &rect);
}

void
cairosdl_surface_flush (cairo_surface_t *surface)
{
    cairosdl_surface_flush_rect (surface, 0, 0, 32767, 32767);
}

void
cairosdl_surface_mark_dirty (cairo_surface_t *surface)
{
    cairosdl_surface_mark_dirty_rect (surface, 0, 0, 32767, 32767);
}

/*
 * Context functions for convenience.
 */

SDL_Surface *
cairosdl_get_target (cairo_t *cr)
{
    return cairosdl_surface_get_target (cairo_get_target (cr));
}

cairo_t *
cairosdl_create (SDL_Surface *sdl_surface)
{
    cairo_surface_t *surface = cairosdl_surface_create (sdl_surface);
    cairo_t *cr = cairo_create (surface);
    cairo_surface_destroy (surface);
    return cr;
}

void
cairosdl_destroy (cairo_t *cr)
{
    cairosdl_surface_flush (cairo_get_target (cr));
    cairo_destroy (cr);
}
