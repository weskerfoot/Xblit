#include <assert.h>
#include <cairo-xcb.h>
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <xcb/xcb.h>

void
print_cairo_format(cairo_format_t format) {
  switch (format) {
    case CAIRO_FORMAT_INVALID:
      printf("Invalid\n");
      break;
    case CAIRO_FORMAT_ARGB32:
      printf("ARGB32\n");
      break;
    case CAIRO_FORMAT_RGB24:
      printf("ARGB32\n");
      break;
    case CAIRO_FORMAT_A8:
      printf("A8\n");
      break;
    case CAIRO_FORMAT_A1:
      printf("A1\n");
      break;
    case CAIRO_FORMAT_RGB16_565:
      printf("RGB16_565\n");
      break;
    case CAIRO_FORMAT_RGB30:
      printf("RGB30\n");
      break;
    default:
      break;
  }
}

static xcb_visualtype_t*
findVisual(xcb_connection_t *display,
            xcb_visualid_t visual) {

  /* Taken from here https://cairographics.org/cookbook/xcbsurface.c/ */
  /* This function basically searches for a xcb_visualtype_t given a visual ID */

  xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(xcb_get_setup(display));

  for(; screen_iter.rem; xcb_screen_next(&screen_iter)) {
    /* Iterate over the screens available */

    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen_iter.data);

    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
      /* Iterate over the depths allowed on this screen */
      xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
      /* depth_iter.data = the number of visuals available */

      for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
        /* Iterate over all of the visuals available */
        if (visual == visual_iter.data->visual_id) {
          printf("%u bits per rgb value\n", visual_iter.data->bits_per_rgb_value);
          return visual_iter.data;
        }
      }
    }
  }
  return NULL;
}

xcb_screen_t*
allocScreen(xcb_connection_t *display) {
  /* Gets a screen from the display connection */
  const xcb_setup_t *setup = xcb_get_setup(display);
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(setup);
  xcb_screen_t *screen = iter.data;

  return screen;
}

void
swapBuffers(cairo_t *front_cr,
            cairo_surface_t *backbuffer_surface,
            unsigned char v,
            int height) {

  int stride = cairo_image_surface_get_stride(backbuffer_surface);

  unsigned char *data = cairo_image_surface_get_data(backbuffer_surface);

  memset(data, v, stride*height);

  cairo_surface_flush(backbuffer_surface);

  cairo_set_source_surface(front_cr,
                           backbuffer_surface,
                           0,
                           0);

  cairo_paint(front_cr);
  cairo_surface_flush(backbuffer_surface);
}

cairo_surface_t*
allocFrontBuf(xcb_connection_t *display,
              xcb_drawable_t drawable,
              xcb_screen_t *screen,
              int width,
              int height) {

  cairo_surface_t *surface = cairo_xcb_surface_create(display,
                                                      drawable,
                                                      findVisual(display, screen->root_visual),
                                                      width,
                                                      height);

  printf("Stride = %d\n", cairo_image_surface_get_stride(surface));

  return surface;
}

cairo_surface_t*
allocBackBuf(int width,
             int height) {

  cairo_format_t format = CAIRO_FORMAT_RGB24;

  cairo_surface_t *surface = cairo_image_surface_create(format,
                                                        width,
                                                        height);

  int stride = cairo_image_surface_get_stride(surface);

  printf("Stride for image = %d\n", stride);

  /* might not be needed */
  cairo_surface_flush(surface);

  return surface;
}

xcb_connection_t*
allocDisplay() {
  /* Get a display to use */
  /* Currently just uses the default display */

  xcb_connection_t *display = xcb_connect(NULL, NULL);

  if (display == NULL) {
    fprintf(stderr, "Could not open the display! :(\n");
    exit(1);
  }

  return display;
}

xcb_window_t
allocWindow(xcb_connection_t *display,
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

static struct timespec
genSleep(time_t sec,
         long nanosec) {
  struct timespec t;
  t.tv_sec = sec;
  t.tv_nsec = nanosec;
  return t;
}

int
main (void) {

  /* Used to handle the event loop */
  struct timespec req = genSleep(0, 20000000);
  struct timespec rem = genSleep(0, 0);

  /* Open up the display */
  xcb_connection_t *display = allocDisplay();

  /* Get a handle to the screen */
  xcb_screen_t *screen = allocScreen(display);

  int window_height = screen->height_in_pixels;
  int window_width = screen->width_in_pixels;

  /* Create a window */
  xcb_window_t window =
    allocWindow(display,
                screen,
                window_width,
                window_height);

  /* Map the window to the display */
  xcb_map_window(display, window);

  /* Allocate front buffer (X drawable) */
  cairo_surface_t *frontbuffer_surface =
    allocFrontBuf(display,
                  window,
                  screen,
                  window_width,
                  window_height);

  cairo_t *front_cr = cairo_create(frontbuffer_surface);

  /* Allocate backbuffer (raw pixel buffer) */
  cairo_surface_t *backbuffer_surface =
    allocBackBuf(window_width, window_height);

  cairo_t *back_cr = cairo_create(backbuffer_surface);

  unsigned char v = 0;

  while (1) {
    swapBuffers(front_cr,
                backbuffer_surface,
                v,
                window_height);
    xcb_flush(display);
    nanosleep(&req, &rem);
    v++;
  }

  pause();

  cairo_destroy(back_cr);
  cairo_surface_destroy(backbuffer_surface);

  cairo_destroy(front_cr);
  cairo_surface_destroy(frontbuffer_surface);

  return 0;
}
