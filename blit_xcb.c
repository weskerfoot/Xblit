/*
 * Note that this is a terrible way to implement a renderer, and it ends up being unwieldy with regard to colors
 * See http://www.rahul.net/kenton/colormap.html#DoubleBuf for a potentially better way
 */

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

typedef struct {
  xcb_point_t *points;
  uint16_t width;
  uint16_t height;
  uint16_t x_origin;
  uint16_t y_origin;
} points_t;

xcb_alloc_color_reply_t*
getColorFromCmap(xcb_connection_t*,
                 xcb_colormap_t,
                 color_t);


/* Macro definition to parse X server events
 * The ~0x80 is needed to get the lower 7 bits
 */
#define RECEIVE_EVENT(ev) (ev->response_type & ~0x80)

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

points_t
genPoints(uint16_t width,
          uint16_t height,
          uint16_t x_offset,
          uint16_t y_offset) {
  /* Fills the entire screen with pixels */
  xcb_point_t *points = malloc( sizeof(xcb_point_t) * height * width);

  xcb_point_t point;

  points_t points_ret;

  int i = 0;

  for (uint16_t x = 0; x < width; x++) {
    for(uint16_t y = 0; y < height; y++) {
      point.x = x + x_offset;
      point.y = y + y_offset;
      points[i] = point;
      i++;
    }
  }

  assert(i == (width*height));

  points_ret.x_origin = x_offset;
  points_ret.y_origin = y_offset;
  points_ret.points = points;
  points_ret.height = height;
  points_ret.width = width;

  return points_ret;
}

void
displayBuffer(xcb_pixmap_t pixmap_buffer,
              xcb_connection_t *display,
              xcb_window_t window,
              xcb_gcontext_t gc,
              points_t points) {
  /* Note that x = 0, y = 0, is the top left of the screen */
  xcb_copy_area(display,
                pixmap_buffer,
                window,
                gc,
                0, /* top left x coord */
                0, /* top left y coord */
                points.x_origin, /* top left x coord of dest*/
                points.y_origin, /* top left y coord of dest*/
                points.width, /* pixel width of source */
                points.height /* pixel height of source */
                );

  xcb_flush(display);
}

void
writePixmap(xcb_pixmap_t pixmap_buffer,
            color_t color,
            xcb_colormap_t colormap,
            points_t points,
            xcb_gcontext_t gc,
            xcb_connection_t *display,
            xcb_window_t window) {

  printf("Drawing pixmap\nr = %u, g = %u, b = %u\n", color.r, color.g, color.b);
  updateGCColor(display,
                gc,
                colormap,
                color);

  xcb_poly_point(display,
                 XCB_COORD_MODE_ORIGIN, /* Coordinate mode, usually set to XCB_COORD_MODE_ORIGIN */
                 pixmap_buffer,
                 gc,
                 points.width*points.height,
                 points.points);

  displayBuffer(pixmap_buffer,
                display,
                window,
                gc,
                points);

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

  int side = 0;
  points_t points;
  uint16_t x_offset = 0;
  uint16_t y_offset = 0;

  while (1) {
    event = xcb_poll_for_event(display);

    if (event != NULL) {
      switch RECEIVE_EVENT(event) {
        case XCB_EXPOSE: {
          expose = (xcb_expose_event_t *)event;

          window_width = expose->width;
          window_height = expose->height;

          printf("Window %u exposed. Region to be redrawn at location (%u,%u), with dimension (%u,%u)\n",
                 expose->window, expose->x,
                 expose->y,
                 expose->width,
                 expose->height);

          points = genPoints(window_width/2, window_height/2, x_offset, y_offset++);

          writePixmap(pixmap_buffer,
                      draw_color,
                      colormap,
                      points,
                      gc,
                      display,
                      window);

          was_exposed = 1;
          free(points.points);

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
      points = genPoints(window_width/2, window_height/2, x_offset, y_offset++);

      writePixmap(pixmap_buffer,
                  draw_color,
                  colormap,
                  points,
                  gc,
                  display,
                  window);

      free(points.points);
    }

    draw_color.r += 100;
    draw_color.g -= 100;

    nanosleep(&req, &rem);
  }

  xcb_free_pixmap(display, pixmap_buffer);
  xcb_disconnect(display);
  return 0;

}
