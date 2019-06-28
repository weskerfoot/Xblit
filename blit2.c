#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

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

int
main(void) {

  xcb_connection_t *display = getDisplay();

  xcb_screen_t *screen = getScreen(display);

  xcb_window_t window = getWindow(display, screen);

  xcb_map_window(display, window);

  xcb_flush(display);

  struct timespec req;
  struct timespec rem;

  req.tv_sec = 0;
  req.tv_nsec = 20000000;

  /* Our event! */
  xcb_generic_event_t *event;
  xcb_expose_event_t *expose;

  while (1) {
    event = xcb_poll_for_event(display);

    if (event != NULL) {
      printf("Got an event\n");

      /* The ~0x80 is needed to get the lower 7 bits
       * Per the docs:
       * A structure is used to pass events received from the X server.
       * XCB supports exactly the events specified in the protocol (33 events).
       * This structure contains the type of event received (including a bit for whether it came from the server or another client),
       * as well as the data associated with the event
       * (e.g. position on the screen where the event was generated,
       * mouse button associated with the event,
       * region of the screen associated with a "redraw" event, etc).
       * The way to read the event's data depends on the event type.
       */
      switch (event->response_type & ~0x80) {
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
          /* Unknown event type, ignore it */
          printf ("Unknown event: %PRIu8\n", event->response_type);
          break;
        }

        /* Events have to be explicitly free'd */
        free(event);
      }
    }

    nanosleep(&req, &rem);
  }

  xcb_disconnect(display);
  return 0;
}
