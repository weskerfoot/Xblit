#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/Xlib-xcb.h> /* for XGetXCBConnection, link with libX11-xcb */
#include <X11/Xlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb.h>

xcb_alloc_color_reply_t*
getColor(xcb_connection_t*,
         xcb_colormap_t,
         unsigned short,
         unsigned short,
         unsigned short);

/* Macro definition to parse X server events
 * The ~0x80 is needed to get the lower 7 bits
 */
#define RECEIVE_EVENT(ev) (ev->response_type & ~0x80)

/*
 * Here is the definition of xcb_cw_t
  * This lets us define what events we want to handle
  * See here https://xcb.freedesktop.org/tutorial/events/
  */

/* TODO
 *
 * Figure out what resources need to be free'd and figure out a strategy for allocating things better
 * Figure out a better way of managing the event loop than nanosleep? (clock_nanosleep maybe)
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
  /* Gets a screen from the display connection */
  const xcb_setup_t *setup = xcb_get_setup(display);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  xcb_screen_t *screen = iter.data;

  return screen;
}

xcb_window_t
getWindow(xcb_connection_t *display,
          xcb_screen_t *screen,
          uint16_t width,
          uint16_t height) {
  /* Create the window */
  xcb_window_t window = xcb_generate_id(display);

  xcb_cw_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;

  /* Define all the events we want to handle with xcb */
  /* XCB_EVENT_MASK_EXPOSURE is the "exposure" event */
  /* I.e. it fires when our window shows up on the screen */

  uint32_t valwin[2] = {screen->white_pixel, XCB_EVENT_MASK_EXPOSURE};

  xcb_create_window(display,
                    XCB_COPY_FROM_PARENT,  /* depth (same as root) */
                    window,
                    screen->root, /* parent window */
                    0, /* x */
                    0, /* y */
                    width,/* width */
                    height,/* height */
                    10, /* border_width  */
                    XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class */
                    screen->root_visual, /* visual */
                    mask, /* value mask, used for events */
                    valwin); /* masks, used for events */

  return window;
}

static xcb_colormap_t
allocateColorMap(xcb_connection_t *display,
                 xcb_window_t window,
                 xcb_screen_t *screen) {
  xcb_colormap_t colormapId = xcb_generate_id(display);

  xcb_create_colormap(display,
                      XCB_COLORMAP_ALLOC_NONE,
                      colormapId,
                      window,
                      screen->root_visual);
  return colormapId;
}

xcb_alloc_color_reply_t*
getColor(xcb_connection_t *display,
         xcb_colormap_t colormap,
         unsigned short red,
         unsigned short green,
         unsigned short blue) {
  /* Return a new xcb color structure */
  /* Initialize it with RGB */

  xcb_alloc_color_reply_t *reply = xcb_alloc_color_reply(display,
                                                         xcb_alloc_color(display,
                                                                         colormap,
                                                                         red,
                                                                         green,
                                                                         blue),
                                                         NULL);

  /* TODO, make sure colors get free'd after they're used?
   * Might not be necessary to free them if we only ever allocate one of each color
   * Linux will make sure resources are cleaned up after the program exits
   * We just have to make sure we don't leak colors
   */

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
      xcb_screen_t *screen,
      xcb_colormap_t colormap,
      unsigned short r,
      unsigned short g,
      unsigned short b) {

  xcb_drawable_t window = screen->root;

  xcb_gcontext_t foreground = xcb_generate_id(display);

  xcb_alloc_color_reply_t *xcolor = getColor(display,
                                             colormap,
                                             r,
                                             g,
                                             b);

  uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
  uint32_t values[2] = {xcolor->pixel, 0};

  xcb_create_gc(display,
                foreground,
                window,
                mask,
                values);

  return foreground;
}

static xcb_pixmap_t
allocatePixmap(xcb_connection_t *display,
               xcb_screen_t *screen,
               xcb_window_t window,
               uint32_t width,
               uint32_t height) {
  xcb_pixmap_t pixmapId = xcb_generate_id(display);

  xcb_create_pixmap(display,
                    screen->root_depth,
                    pixmapId,
                    window,
                    width,
                    height);

  return pixmapId;
}

xcb_point_t*
genPoints(uint16_t width,
          uint16_t height) {
  xcb_point_t *points = malloc( sizeof(xcb_point_t) * height * width);

  xcb_point_t point;

  int i = 0;

  printf("width = %d, height = %d\n", width, height);

  for (uint16_t x = 0; x < width; x++) {
    for(uint16_t y = 0; y < height; y++) {
      point.x = x;
      point.y = y;
      points[i] = point;
      i++;
    }
  }

  assert(i == (width*height));

  return points;
}

int
main(void) {
  uint16_t window_width = 500;
  uint16_t window_height = 500;

  /* Open up the display */
  xcb_connection_t *display = getDisplay();

  /* Get a handle to the screen */
  xcb_screen_t *screen = getScreen(display);

  /* Create a window */
  xcb_window_t window = getWindow(display,
                                  screen,
                                  window_width,
                                  window_height);

  /* Map the window to the display */
  xcb_map_window(display, window);

  /* Allocate a colormap, for creating colors */
  xcb_colormap_t colormap = allocateColorMap(display, window, screen);

  /* Flush all commands */
  xcb_flush(display);

  /* Used to handle the event loop */
  struct timespec req = genSleep(0, 20000000);
  struct timespec rem = genSleep(0, 0);

  xcb_generic_event_t *event;
  xcb_expose_event_t *expose;

  xcb_gcontext_t gc = getGC(display,
                            screen,
                            colormap,
                            0,
                            0xffff,
                            0);
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

          window_width = expose->width;
          window_height = expose->height;

          /*
           * One important note should be made:
           * it is possible to create pixmaps with different depths on the same screen.
           * When we perform copy operations (a pixmap onto a window, etc),
           * we should make sure that both source and target have the same depth.
           * If they have a different depth, the operation will fail.
           * The exception to this is if we copy a specific bit plane of the source pixmap using xcb_copy_plane().
           * In such an event, we can copy a specific plane to the target window
           * (in actuality, setting a specific bit in the color of each pixel copied).
           * This can be used to generate strange graphic effects in a window, but that is beyond the scope of this tutorial.
           */


          /* Allocate a pixmap we will be blitting to the window */
          xcb_pixmap_t pixmap = allocatePixmap(display,
                                               screen,
                                               window,
                                               window_width,
                                               window_height);

          xcb_point_t *points = genPoints(window_width, window_height);

          xcb_poly_point (display,               /* The connection to the X server */
                          XCB_COORD_MODE_ORIGIN, /* Coordinate mode, usually set to XCB_COORD_MODE_ORIGIN */
                          pixmap,        /* The drawable on which we want to draw the point(s) */
                          gc,              /* The Graphic Context we use to draw the point(s) */
                          window_width*window_height,      /* The number of points */
                          points);         /* An array of points */

          xcb_copy_area(display,
                        pixmap,
                        window,
                        gc,
                        0, /* top left x coord */
                        0, /* top left y coord */
                        0, /* top left x coord of dest*/
                        0, /* top left y coord of dest*/
                        window_width, /* pixel width of source */
                        window_height /* pixel height of source */
                        );

          xcb_flush(display);
          free(points);
          xcb_free_pixmap(display, pixmap);
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
