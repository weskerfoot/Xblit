#include <stdio.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h> /* for XGetXCBConnection, link with libX11-xcb */
#include <xcb/xcb.h>
#include <GL/glx.h>
#include <GL/gl.h>

xcb_window_t
getWindow(xcb_connection_t*,
          xcb_colormap_t,
          int,
          xcb_screen_t*);

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

void draw() {
    glClearColor(0.2, 0.4, 0.9, 1.0);
    glClear(GL_COLOR_BUFFER_BIT);
}

int
main_loop(Display *display,
          xcb_connection_t *xcb_display,
          xcb_window_t window,
          GLXDrawable drawable) {

    int running = 1;

    while (running) {
        /* Wait for event */
        xcb_generic_event_t *event = xcb_wait_for_event(xcb_display);
        if (!event) {
            fprintf(stderr, "i/o error in xcb_wait_for_event");
            return -1;
        }

        switch (RECEIVE_EVENT(event)) {
            case XCB_KEY_PRESS:
                /* Quit on key press */
                running = 0;
                break;
            case XCB_EXPOSE:
                /* Handle expose event, draw and swap buffers */
                draw();

                /* This is where the magic happens */
                /* This call will NOT block.*/
                /* It will be sync'd with vertical refresh */
                glXSwapBuffers(display, drawable);
                break;
            default:
                break;
        }

        free(event);
    }

    return 0;
}

int
setup_and_run(Display* display,
              xcb_connection_t *xcb_display,
              int default_screen,
              xcb_screen_t *screen) {

    int visualID = 0;

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

    printf("Found %d matching FB configs", num_fb_configs);

    /* Select first framebuffer config and query visualID */
    GLXFBConfig fb_config = fb_configs[0];

    /* This will write the visualID */
    glXGetFBConfigAttrib(display, fb_config, GLX_VISUAL_ID , &visualID);

    /* Create XIDs for colormap and window */
    /* This is how we can use the Xlib GLX functions */
    /* Since these XIDs are transferrable between xcb and xlib */
    xcb_colormap_t colormap = getColorMap(xcb_display, screen, visualID);
    xcb_window_t window = getWindow(xcb_display, colormap, visualID, screen);

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
    if (!glXMakeContextCurrent(display,
                               drawable,
                               drawable,
                               context)) {

        xcb_destroy_window(xcb_display, window);
        glXDestroyContext(display, context);

        fprintf(stderr, "glXMakeContextCurrent failed\n");
        return -1;
    }

    /* run main loop */
    int retval = main_loop(display, xcb_display, window, drawable);

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
          xcb_screen_t *screen) {
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
        0, 0,
        150, 150,
        0,
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

int main(int argc, char* argv[])
{
    Display *display = getDisplay();

    /* Get the XCB connection from the display */
    xcb_connection_t *xcb_display = getXCBDisplay(display);

    /* Acquire event queue ownership */
    /* See https://xcb.freedesktop.org/MixingCalls/ for why this is needed */
    XSetEventQueueOwner(display, XCBOwnsEventQueue);

    int default_screen = DefaultScreen(display);

    xcb_screen_t *screen = getScreen(xcb_display, default_screen);

    /* Initialize window and OpenGL context, run main loop and deinitialize */
    int retval = setup_and_run(display, xcb_display, default_screen, screen);

    /* Cleanup */
    XCloseDisplay(display);

    return retval;
}
