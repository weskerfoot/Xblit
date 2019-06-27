#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xcms.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Display*
getDisplay() {
  /* Get a display to use */
  /* Currently just uses the default display */
  Display *display = XOpenDisplay(NULL);

  if (display == NULL) {
    fprintf(stderr, "Could not open the display! :(\n");
    exit(1);
  }

  return display;
}

XColor
getColor(unsigned short red,
         unsigned short green,
         unsigned short blue) {
  /* Return a new XColor structure */
  /* Initialize it with RGB */
  XColor xcolor;

  xcolor.red = red;
  xcolor.green = green;
  xcolor.blue = blue;

  xcolor.flags = DoRed | DoGreen | DoBlue;

  return xcolor;
}

int
main(void) {
  Window window;
  XEvent event;

  const char *msg = "Hello, world!\n";

  XColor xcolor = getColor(0xffff, 0xffff, 0xffff);

  Display *display = getDisplay();

  int screen = DefaultScreen(display);

  GC gc = XDefaultGC(display, screen);

  XAllocColor(display,
              XDefaultColormap(display, screen),
              &xcolor);

  window = XCreateSimpleWindow(display,
                               RootWindow(display, screen),
                               10,
                               10,
                               100,
                               100,
                               1,
                               WhitePixel(display, screen),
                               xcolor.pixel);

  printf("%p\n", window);

  XSelectInput(display,
               window,
               ExposureMask | KeyPressMask);

  XMapWindow(display, window);

  int depth = DefaultDepth(display, DefaultScreen(display));

  Pixmap pixmap;
  pixmap = XCreatePixmap(display,
                         window,
                         100,
                         100,
                         depth);

  int factor = 0;

  struct timespec req;
  struct timespec rem;

  req.tv_sec = 0;
  req.tv_nsec = 20000000;

  while (1) {
    /* Event loop that handles events from the X server's event queue */
    /* Will actually block if there are no events */

    XNextEvent(display, &event);
    if (event.type == Expose) {

      XColor boxcolor = getColor(32000, 0, 32000);

      XAllocColor(display,
                  XDefaultColormap(display, screen),
                  &boxcolor);

      XSetForeground(display, gc, boxcolor.pixel);

    }

    if (event.type == KeyPress) {
      break;
    }

    for(int x = 0; x < 100; x++) {
      for(int y = 0; y < 100; y++) {
        XDrawPoint(display, pixmap, gc, x, y);
      }
    }

    factor++;

    XCopyArea(display,
              pixmap,
              window,
              gc,
              0,
              0,
              100,
              100,
              factor,
              factor);

    nanosleep(&req, &rem);

  }

  XCloseDisplay(display);

  return 0;
}
