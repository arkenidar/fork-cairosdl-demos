/*
 * Cairo SDL clock. Shows how to use Cairo with SDL.
 * Made by Writser Cleveringa, based upon code from Eric Windisch.
 * Minor code clean up by Chris Nystrom (5/21/06) and converted to cairo-sdl
 * by Chris Wilson and converted to cairosdl by M Joonas Pihlaja.
 */

#include <stdio.h>
#include <time.h>
#include <math.h>
#include "cairosdl.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Draws a clock on a normalized Cairo context */
static void
draw (cairo_t *cr)
{
    time_t t;
    struct tm *tm;
    double seconds, minutes, hours;

    /* In newer versions of Visual Studio localtime(..) is deprecated. */
    /* Use localtime_s instead. See MSDN. */
    t = time (NULL);
    tm = localtime (&t);

    /* compute the angles for the indicators of our clock */
    seconds = tm->tm_sec * M_PI / 30;
    minutes = tm->tm_min * M_PI / 30;
    hours = tm->tm_hour * M_PI / 6;

    /* Fill the background with white. */
    cairo_set_source_rgb (cr, 1, 1, 1);
    cairo_paint (cr);

    /* who doesn't want all those nice line settings :) */
    cairo_set_line_cap (cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_width (cr, 0.1);

    /* translate to the center of the rendering context and draw a black */
    /* clock outline */
    cairo_set_source_rgb (cr, 0, 0, 0);
    cairo_translate (cr, 0.5, 0.5);
    cairo_arc (cr, 0, 0, 0.4, 0, M_PI * 2);
    cairo_stroke (cr);

    /* draw a white dot on the current second. */
    cairo_set_source_rgba (cr, 1, 1, 1, 0.6);
    cairo_arc (cr,
	       sin (seconds) * 0.4, -cos (seconds) * 0.4,
	       0.05, 0, M_PI * 2);
    cairo_fill (cr);

    /* draw the minutes indicator */
    cairo_set_source_rgba (cr, 0.2, 0.2, 1, 0.6);
    cairo_move_to (cr, 0, 0);
    cairo_line_to (cr, sin (minutes) * 0.4, -cos (minutes) * 0.4);
    cairo_stroke (cr);

    /* draw the hours indicator      */
    cairo_move_to (cr, 0, 0);
    cairo_line_to (cr, sin (hours) * 0.2, -cos (hours) * 0.2);
    cairo_stroke (cr);
}

/* Shows how to draw with Cairo on SDL surfaces */
static void
draw_screen (SDL_Surface *screen)
{
    cairo_t *cr;
    cairo_status_t status;

    /* Create a cairo drawing context, normalize it and draw a clock. */
    SDL_LockSurface (screen); {
        cr = cairosdl_create (screen);

        cairo_scale (cr, screen->w, screen->h);
        draw (cr);

        status = cairo_status (cr);
        cairosdl_destroy (cr);
    }
    SDL_UnlockSurface (screen);
    SDL_Flip (screen);

    /* Nasty nasty error handling. */
    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf (stderr, "Unable to create or draw with a cairo context "
                 "for the screen: %s\n",
                 cairo_status_to_string (status));
        exit (1);
    }
}

static SDL_Surface *
init_screen (int width, int height, int bpp)
{
    SDL_Surface *screen;

    /* Initialize SDL */
    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
	fprintf (stderr, "Unable to initialize SDL: %s\n",
		 SDL_GetError ());
	exit (1);
    }

    /* Open a screen with the specified properties */
    screen = SDL_SetVideoMode (width, height, bpp,
			       SDL_HWSURFACE |
			       SDL_RESIZABLE);
    if (screen == NULL) {
	fprintf (stderr, "Unable to set %ix%i video: %s\n",
		 width, height, SDL_GetError ());
	exit (1);
    }

    SDL_WM_SetCaption ("Cairo clock - Press Q to quit", "ICON");

    return screen;
}

/* This function pushes a custom event onto the SDL event queue.
 * Whenever the main loop receives it, the window will be redrawn.
 * We can't redraw the window here, since this function could be called
 * from another thread.
 */
static Uint32
timer_cb (Uint32 interval, void *param)
{
    SDL_Event event;

    event.type = SDL_USEREVENT;
    SDL_PushEvent (&event);

    (void)param;
    return interval;
}

int
main (int argc, char **argv)
{
    SDL_Surface *screen;
    SDL_Event event;

    (void)argc;
    (void)argv;

    /* Initialize SDL, open a screen */
    screen = init_screen (640, 480, 32);

    /* Create a timer which will redraw the screen every 100 ms. */
    SDL_AddTimer (100, timer_cb, NULL);

    while (SDL_WaitEvent (&event)) {
	switch (event.type) {
	case SDL_KEYDOWN:
	    if (event.key.keysym.sym == SDLK_q) {
		goto done;
	    }
	    break;

	case SDL_QUIT:
	    goto done;

	case SDL_VIDEORESIZE:
	    screen = SDL_SetVideoMode (event.resize.w,
				       event.resize.h, 32,
				       SDL_HWSURFACE |
				       SDL_RESIZABLE);
	    /* fall-through */
	case SDL_USEREVENT:
	    draw_screen (screen);
	    break;

	default:
	    break;
	}
    }

done:
    SDL_FreeSurface (screen);
    SDL_Quit ();
    return 0;
}
