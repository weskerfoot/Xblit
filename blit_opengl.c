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

xcb_window_t
getWindow(xcb_connection_t*,
          xcb_colormap_t,
          int,
          xcb_screen_t*,
          uint16_t,
          uint16_t);

xcb_colormap_t
getColorMap(xcb_connection_t*,
                 xcb_screen_t*,
                 int);

GLXContext
getGLXContext(Display*,
              GLXFBConfig);

/*
    Attribs filter the list of FBConfigs returned by glXChooseFBConfig().
    Visual attribs further described in glXGetFBConfigAttrib(3)
*/
static int visual_attribs[] = {
    GLX_X_RENDERABLE, True,
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GLX_DEPTH_SIZE, 24,
    GLX_STENCIL_SIZE, 8,
    GLX_DOUBLEBUFFER, True,
    //GLX_SAMPLE_BUFFERS  , 1,
    //GLX_SAMPLES         , 4,
    None
};

/* Macro definition to parse X server events
 * The ~0x80 is needed to get the lower 7 bits
 */
#define RECEIVE_EVENT(ev) (ev->response_type & ~0x80)

void draw(GLint offset) {
  /* This is needed because now the contents of `drawable` are undefined */

  printf("Drawing!\n");

  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, 500, 500, 0.0, 0.0, 1.0);

  glBegin(GL_POINTS);
  for (int i = 0; i < 100; i++) {
    glVertex2i(offset, i);
  }
  glEnd();

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
message_loop(Display *display,
             xcb_connection_t *xcb_display,
             xcb_window_t window,
             GLXDrawable drawable) {

    int running = 1;


    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);

    GLint offset = 0;

    /* Used to handle the event loop */
    struct timespec req = genSleep(0, 20000000);
    struct timespec rem = genSleep(0, 0);

    while (running) {
        /* Poll for events */

        xcb_generic_event_t *event = xcb_poll_for_event(xcb_display);

        if (event != NULL) {
          switch (RECEIVE_EVENT(event)) {
            case XCB_KEY_PRESS:
                /* Quit on key press */
                // running = 0;
                break;
            case XCB_EXPOSE:
                printf("Got expose event\n");
                break;
            default:
                break;
          }

          free(event);
        }
        draw(offset);
        offset++;

        /* This is where the magic happens */
        /* This call will NOT block.*/
        /* It will be sync'd with vertical refresh */
        glXSwapBuffers(display, drawable);
        nanosleep(&req, &rem);
    }

    return 0;
}

int
setup_message_loop(Display* display,
              xcb_connection_t *xcb_display,
              int default_screen,
              xcb_screen_t *screen) {

    int visualID = 0;
    uint16_t width = 500;
    uint16_t height = 500;

    /* Query framebuffer configurations that match visual_attribs */
    GLXFBConfig *fb_configs = 0;

    int num_fb_configs = 0;

    fb_configs = glXChooseFBConfig(display,
                                   default_screen,
                                   visual_attribs,
                                   &num_fb_configs);

    if (!fb_configs || num_fb_configs == 0) {
        fprintf(stderr, "glXGetFBConfigs failed\n");
        return -1;
    }

    printf("Found %d matching FB configs\n", num_fb_configs);

    /* Select first framebuffer config and query visualID */
    GLXFBConfig fb_config = fb_configs[0];

    /* This will write the visualID */
    glXGetFBConfigAttrib(display, fb_config, GLX_VISUAL_ID , &visualID);

    /* Create XIDs for colormap and window */
    /* This is how we can use the Xlib GLX functions */
    /* Since these XIDs are transferrable between xcb and xlib */
    xcb_colormap_t colormap = getColorMap(xcb_display, screen, visualID);

    xcb_window_t window = getWindow(xcb_display,
                                    colormap,
                                    visualID,
                                    screen,
                                    width,
                                    height);

    /* NOTE: window must be mapped before glXMakeContextCurrent */
    xcb_map_window(xcb_display, window);


    /* Create GLX Window */
    GLXContext context = getGLXContext(display, fb_config);

    /* This is going to be our backbuffer */
    GLXDrawable drawable = 0;

    GLXWindow glxwindow =
        glXCreateWindow(
            display,
            fb_config,
            window,
            0
            );

    if (!window) {
        xcb_destroy_window(xcb_display, window);
        glXDestroyContext(display, context);

        fprintf(stderr, "glXDestroyContext failed\n");
        return -1;
    }

    drawable = glxwindow;

    /* make OpenGL context current */
    /* This will allow us to write to it with the GLX functions */
    /* https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glXMakeContextCurrent.xml */
    if (!glXMakeContextCurrent(display,
                               drawable,
                               drawable,
                               context)) {

        xcb_destroy_window(xcb_display, window);
        glXDestroyContext(display, context);

        fprintf(stderr, "glXMakeContextCurrent failed\n");
        return -1;
    }

    /* run message loop */
    int retval = message_loop(display,
                              xcb_display,
                              window,
                              drawable);

    /* Cleanup */
    glXDestroyWindow(display, glxwindow);

    xcb_destroy_window(xcb_display, window);

    glXDestroyContext(display, context);

    return retval;
}

GLXContext
getGLXContext(Display *display,
              GLXFBConfig fb_config) {
  /* Create GLX Window */
  GLXContext context;

  /* Create OpenGL context */
  /* Display* dpy
   * GLXFBConfig config
   * int render_type
   * GLXContext share_list
   * Bool direct (indicates we want direct rendering)
   */
  context = glXCreateNewContext(display,
                                fb_config,
                                GLX_RGBA_TYPE,
                                0,
                                True);
  if (!context) {
    fprintf(stderr, "glXCreateNewContext failed\n");
    exit(1);
  }

  return context;

}

xcb_colormap_t
getColorMap(xcb_connection_t *xcb_display,
                 xcb_screen_t *screen,
                 int visualID) {
  /* Create XID's for colormap and window */
  xcb_colormap_t colormap = xcb_generate_id(xcb_display);

  /* Create colormap */
  xcb_create_colormap(
      xcb_display,
      XCB_COLORMAP_ALLOC_NONE,
      colormap,
      screen->root,
      visualID
      );

  return colormap;
}

xcb_window_t
getWindow(xcb_connection_t *xcb_display,
          xcb_colormap_t colormap,
          int visualID,
          xcb_screen_t *screen,
          uint16_t width,
          uint16_t height) {
    xcb_window_t window = xcb_generate_id(xcb_display);

    /* Create window */
    uint32_t eventmask = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;
    uint32_t valuelist[] = { eventmask, colormap, 0 };
    uint32_t valuemask = XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;

    xcb_create_window(
        xcb_display,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        0, 0, /* x y */
        width, height, /* width height */
        0, /* border width */
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        visualID,
        valuemask,
        valuelist
        );

    return window;
}

Display*
getDisplay() {
    Display *display;

    if (display == NULL) {
      fprintf(stderr, "Could not open the display! :(\n");
      exit(1);
    }

    /* Open Xlib Display */
    return XOpenDisplay(0);
}

xcb_connection_t*
getXCBDisplay(Display *x_display) {
  /* Get the XCB connection from the display */
  /* See https://xcb.freedesktop.org/MixingCalls/ */

  xcb_connection_t *xcb_display = XGetXCBConnection(x_display);

  if (!xcb_display) {
      XCloseDisplay(x_display);
      fprintf(stderr, "Can't get xcb connection from display\n");
      exit(1);
  }
  return xcb_display;
}

xcb_screen_t*
getScreen(xcb_connection_t *xcb_display,
          int default_screen) {

    /* Find XCB screen */
    xcb_screen_t *screen = 0;

    /* Create an iterator of all available screens */
    xcb_screen_iterator_t screen_iter;
    screen_iter = xcb_setup_roots_iterator(xcb_get_setup(xcb_display));

    int screen_num = default_screen;
    printf("%d\n", screen_num);

    /* Handle multiple screens */
    /* We want screen number 0 */
    while (screen_iter.rem && screen_num > 0) {
      printf("%d\n", screen_num);

      screen_num--;
      xcb_screen_next(&screen_iter);
    }

    return screen_iter.data;
}

int
main(void) {
    Display *display = getDisplay();

    /* Get the XCB connection from the display */
    xcb_connection_t *xcb_display = getXCBDisplay(display);

    /* Acquire event queue ownership */
    /* See https://xcb.freedesktop.org/MixingCalls/ for why this is needed */
    XSetEventQueueOwner(display, XCBOwnsEventQueue);

    int default_screen = DefaultScreen(display);

    xcb_screen_t *screen = getScreen(xcb_display, default_screen);

    /* Initialize window and OpenGL context, run main loop and deinitialize */
    int retval = setup_message_loop(display, xcb_display, default_screen, screen);

    /* Cleanup */
    XCloseDisplay(display);

    return retval;
}
