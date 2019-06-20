#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(void) {
  Display *display;
  Window window;
  XEvent event;

  const char *msg = "Hello, world!\n";

  int screen;

  // Opens the current display
  display = XOpenDisplay(NULL);

  if (display == NULL) {
    fprintf(stderr, "Could not open the display! :(\n");
    exit(1);
  }

  printf("%p\n", display);

  screen = DefaultScreen(display);

  window = XCreateSimpleWindow(display,
                               RootWindow(display, screen),
                               10,
                               10,
                               100,
                               100,
                               1,
                               BlackPixel(display, screen),
                               WhitePixel(display, screen));

  printf("%p\n", window);

  XSelectInput(display,
               window,
               ExposureMask | KeyPressMask);

  XMapWindow(display, window);

  while (1) {
    /* Event loop that handles events from the X server's event queue */
    /* Will actually block if there are no events */

    XNextEvent(display, &event);
    if (event.type == Expose) {

      XFillRectangle(display,
                     window,
                     DefaultGC(display, screen),
                     20,
                     20,
                     10,
                     10);

      XDrawString(display,
                  window,
                  DefaultGC(display, screen),
                  10,
                  50,
                  msg,
                  strlen(msg));
    }

    if (event.type == KeyPress) {
      break;
    }

  }

  XCloseDisplay(display);

  return 0;
}
