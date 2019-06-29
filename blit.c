#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>


/* Macro definition to parse X server events
 * The ~0x80 is needed to get the lower 7 bits
 * XCB supports exactly the events specified in the protocol (33 events).
 * This structure contains the type of event received (including a bit for whether it came from the server or another client),
 * as well as the data associated with the event
 * (e.g. position on the screen where the event was generated,
 * mouse button associated with the event,
 * region of the screen associated with a "redraw" event, etc).
 * The way to read the event's data depends on the event type.
 */
#define RECEIVE_EVENT(ev) (ev->response_type & ~0x80)

/*
 * Here is the definition of xcb_cw_t
    typedef enum {
        XCB_CW_BACK_PIXMAP       = 1L<<0,
        XCB_CW_BACK_PIXEL        = 1L<<1,
        XCB_CW_BORDER_PIXMAP     = 1L<<2,
        XCB_CW_BORDER_PIXEL      = 1L<<3,
        XCB_CW_BIT_GRAVITY       = 1L<<4,
        XCB_CW_WIN_GRAVITY       = 1L<<5,
        XCB_CW_BACKING_STORE     = 1L<<6,
        XCB_CW_BACKING_PLANES    = 1L<<7,
        XCB_CW_BACKING_PIXEL     = 1L<<8,
        XCB_CW_OVERRIDE_REDIRECT = 1L<<9,
        XCB_CW_SAVE_UNDER        = 1L<<10,
        XCB_CW_EVENT_MASK        = 1L<<11,
        XCB_CW_DONT_PROPAGATE    = 1L<<12,
        XCB_CW_COLORMAP          = 1L<<13,
        XCB_CW_CURSOR            = 1L<<14
    } xcb_cw_t;
  * Why does this matter?
  * This lets us define what events we want to handle
  * See here https://xcb.freedesktop.org/tutorial/events/
  */

/* TODO
 *
 * Figure out what resources need to be free'd and figure out a strategy for allocating things better
 * Figure out a better way of managing the event loop than nanosleep?
 * Figure out which events we need to actually be handling
 * Figure out how to resize dynamically (See handmade hero videos for tips)
 * Figure out good strategy for only copying changed pixels to window
 * Figure out what allocations can fail and what to do if they fail
 */

xcb_connection_t*
getDisplay() {
  /* Get a display to use */
  /* Currently just uses the default display */

  xcb_connection_t *display = xcb_connect (NULL, NULL);

  if (display == NULL) {
    fprintf(stderr, "Could not open the display! :(\n");
    exit(1);
  }

  return display;
}

xcb_screen_t*
getScreen(xcb_connection_t *display) {
  const xcb_setup_t *setup = xcb_get_setup(display);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  xcb_screen_t *screen = iter.data;

  return screen;
}

xcb_window_t
getWindow(xcb_connection_t *display,
          xcb_screen_t *screen) {
  /* Create the window */
  xcb_window_t window = xcb_generate_id(display);

  xcb_cw_t mask = XCB_CW_EVENT_MASK;

  /* Define all the events we want to handle with xcb */
  /* XCB_EVENT_MASK_EXPOSURE is the "exposure" event */
  /* I.e. it fires when our window shows up on the screen */

  uint32_t valwin[1] = { XCB_EVENT_MASK_EXPOSURE };

  xcb_create_window(display, /* Connection */
                    XCB_COPY_FROM_PARENT,  /* depth (same as root) */
                    window, /* window Id */
                    screen->root, /* parent window */
                    0, /* x */
                    0, /* y */
                    150,
                    150,/* width, height */
                    10, /* border_width  */
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class */
                    screen->root_visual, /* visual */
                    mask, /* value mask, used for events */
                    valwin); /* masks, used for events */

  return window;
}

xcb_alloc_color_reply_t*
getColor(xcb_connection_t *display,
         xcb_screen_t *screen,
         xcb_window_t window,
         unsigned short red,
         unsigned short green,
         unsigned short blue) {
  /* Return a new xcb color structure */
  /* Initialize it with RGB */

  xcb_colormap_t colormapId = xcb_generate_id(display);

  xcb_create_colormap(display,
                      XCB_COLORMAP_ALLOC_NONE,
                      colormapId,
                      window,
                      screen->root_visual);


  xcb_alloc_color_reply_t *reply = xcb_alloc_color_reply(display,
                                                         xcb_alloc_color(display,
                                                                         colormapId,
                                                                         red,
                                                                         green,
                                                                         blue),
                                                         NULL);

  return reply;
}

static struct timespec
genSleep(time_t sec,
         long nanosec) {
  struct timespec t;
  t.tv_sec = sec;
  t.tv_nsec = nanosec;
  return t;
}

static xcb_gcontext_t
getGC(xcb_connection_t *display,
      xcb_screen_t *screen) {

  xcb_drawable_t window = screen->root;

  xcb_gcontext_t foreground = xcb_generate_id(display);

  uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
  uint32_t values[2] = {screen->black_pixel, 0};

  xcb_create_gc(display,
                foreground,
                window,
                mask,
                values);

  return foreground;
}

int
main(void) {

  xcb_connection_t *display = getDisplay();

  xcb_screen_t *screen = getScreen(display);

  xcb_window_t window = getWindow(display, screen);

  xcb_map_window(display, window);

  xcb_alloc_color_reply_t *xcolor = getColor(display,
                                             screen,
                                             window,
                                             0xffff,
                                             0xffff,
                                             0xffff);

  xcb_flush(display);

  /* Used to handle the event loop */
  struct timespec req = genSleep(0, 20000000);
  struct timespec rem = genSleep(0, 0);

  xcb_generic_event_t *event;
  xcb_expose_event_t *expose;

  while (1) {
    event = xcb_poll_for_event(display);

    if (event != NULL) {
      switch RECEIVE_EVENT(event) {
        /* TODO encapsulate event handlers in functions */
        case XCB_EXPOSE: {
          expose = (xcb_expose_event_t *)event;
          printf("Window %u exposed. Region to be redrawn at location (%u,%u), with dimension (%u,%u)\n",
                 expose->window, expose->x,
                 expose->y,
                 expose->width,
                 expose->height);
          break;
        }

        default: {
          printf ("Unknown event: %u\n", event->response_type);
          break;
        }

        free(event);
      }
    }

    nanosleep(&req, &rem);
  }

  xcb_disconnect(display);
  return 0;
}
