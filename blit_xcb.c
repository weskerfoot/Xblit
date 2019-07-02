#include <X11/Xlib-xcb.h> /* for XGetXCBConnection, link with libX11-xcb */
#include <X11/Xlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

typedef struct {
  unsigned short r;
  unsigned short g;
  unsigned short b;
} color_t;

xcb_alloc_color_reply_t*
getColorFromCmap(xcb_connection_t*,
                 xcb_colormap_t,
                 color_t);


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
getColorFromCmap(xcb_connection_t *display,
                 xcb_colormap_t colormap,
                 color_t color) {
  /* Allocate a color in the color map */
  /* Initialize it with RGB */

  xcb_alloc_color_reply_t *reply = xcb_alloc_color_reply(display,
                                                         xcb_alloc_color(display,
                                                                         colormap,
                                                                         color.r,
                                                                         color.g,
                                                                         color.b),
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
      color_t color) {

  xcb_drawable_t window = screen->root;

  xcb_gcontext_t foreground = xcb_generate_id(display);

  xcb_alloc_color_reply_t *xcolor = getColorFromCmap(display,
                                                     colormap,
                                                     color);

  uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
  uint32_t values[2] = {xcolor->pixel, 0};

  xcb_create_gc(display,
                foreground,
                window,
                mask,
                values);

  return foreground;
}

static xcb_void_cookie_t
updateGCColor(xcb_connection_t *display,
              xcb_gcontext_t gc,
              xcb_colormap_t colormap,
              color_t color) {
  /* https://www.x.org/releases/X11R7.6/doc/libxcb/tutorial/index.html#changegc */

  xcb_alloc_color_reply_t *xcolor = getColorFromCmap(display,
                                                     colormap,
                                                     color);

  uint32_t mask = XCB_GC_FOREGROUND | XCB_GC_GRAPHICS_EXPOSURES;
  uint32_t values[2] = {xcolor->pixel, 0};

  return xcb_change_gc(display,
                       gc,
                       mask,
                       values);
}

xcb_point_t*
genPoints(uint16_t width,
          uint16_t height) {
  /* Fills the entire screen with pixels */
  xcb_point_t *points = malloc( sizeof(xcb_point_t) * height * width);

  xcb_point_t point;

  int i = 0;

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

void
displayBuffer(xcb_pixmap_t pixmap_buffer,
              xcb_connection_t *display,
              xcb_window_t window,
              xcb_gcontext_t gc,
              xcb_expose_event_t *event) {
  /* Note that x = 0, y = 0, is the top left of the screen */
  xcb_copy_area(display,
                pixmap_buffer,
                window,
                gc,
                0, /* top left x coord */
                0, /* top left y coord */
                event->x, /* top left x coord of dest*/
                event->y, /* top left y coord of dest*/
                event->width, /* pixel width of source */
                event->height /* pixel height of source */
                );

  xcb_flush(display);
}

void
writePixmap(xcb_pixmap_t pixmap_buffer,
            color_t color,
            xcb_colormap_t colormap,
            xcb_point_t *points,
            xcb_gcontext_t gc,
            xcb_connection_t *display,
            xcb_window_t window,
            xcb_expose_event_t *event) {

  printf("Drawing pixmap\nr = %u, g = %u, b = %u\n", color.r, color.g, color.b);
  updateGCColor(display,
                gc,
                colormap,
                color);

  xcb_poly_point(display,
                 XCB_COORD_MODE_ORIGIN, /* Coordinate mode, usually set to XCB_COORD_MODE_ORIGIN */
                 pixmap_buffer,
                 gc,
                 event->width*event->height,
                 points);

  displayBuffer(pixmap_buffer,
                display,
                window,
                gc,
                event);

}

xcb_pixmap_t
getPixmap(xcb_connection_t *display,
          xcb_screen_t *screen,
          xcb_window_t window,
          uint16_t window_width,
          uint16_t window_height) {
    /* Allocate a pixmap we will be blitting to the window */
    xcb_pixmap_t pixmapId = xcb_generate_id(display);

    xcb_create_pixmap(display,
                      screen->root_depth,
                      pixmapId,
                      window,
                      window_width,
                      window_height);

    return pixmapId;
}

static color_t
color(unsigned short r,
      unsigned short g,
      unsigned short b) {
  /* Initialize an RGB color struct */
  color_t color;
  color.r = r;
  color.g = g;
  color.b = b;
  return color;
}

int
main(void) {
  /* Open up the display */
  xcb_connection_t *display = getDisplay();

  /* Get a handle to the screen */
  xcb_screen_t *screen = getScreen(display);

  uint16_t window_height = screen->height_in_pixels;
  uint16_t window_width = screen->width_in_pixels;

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

  color_t draw_color = color(0, 0, 0);

  xcb_generic_event_t *event;
  xcb_expose_event_t *expose;

  xcb_gcontext_t gc = getGC(display,
                            screen,
                            colormap,
                            draw_color);

  /* The pixmap that acts as our backbuffer */
  xcb_pixmap_t pixmap_buffer = getPixmap(display,
                                         screen,
                                         window,
                                         window_width,
                                         window_height);

  int was_exposed = 0;

  while (1) {
    event = xcb_poll_for_event(display);

    if (event != NULL) {
      switch RECEIVE_EVENT(event) {

        /* TODO encapsulate event handlers in functions */

        case XCB_EXPOSE: {
          expose = (xcb_expose_event_t *)event;
          window_width = expose->width;
          window_height = expose->height;
          printf("Window %u exposed. Region to be redrawn at location (%u,%u), with dimension (%u,%u)\n",
                 expose->window, expose->x,
                 expose->y,
                 expose->width,
                 expose->height);

          xcb_point_t *points = genPoints(window_width, window_height);
          writePixmap(pixmap_buffer,
                      draw_color,
                      colormap,
                      points,
                      gc,
                      display,
                      window,
                      expose);
          was_exposed = 1;
          free(points);

          break;
        }

        default: {
          printf ("Unknown event: %u\n", event->response_type);
          break;
        }

        free(event);
      }
    }
    if (was_exposed) {
      xcb_point_t *points = genPoints(window_width, window_height);

      writePixmap(pixmap_buffer,
                  draw_color,
                  colormap,
                  points,
                  gc,
                  display,
                  window,
                  expose);

      free(points);
    }

    draw_color.r += 100;
    draw_color.g -= 100;


    /* General strategy for writing to buffer
     * Function should take point(s), color, and write it
     */

    nanosleep(&req, &rem);
  }

  xcb_free_pixmap(display, pixmap_buffer);
  xcb_disconnect(display);
  return 0;

}
