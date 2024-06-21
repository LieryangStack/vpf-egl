#ifndef __GST_VIDEO_PLATFORM_WRAPPER__
#define __GST_VIDEO_PLATFORM_WRAPPER__

#include <gst/gst.h>
#include <EGL/egl.h>

#define USE_EGL_X11 1
#define IS_DESKTOP 1

#ifdef USE_EGL_X11
#include <X11/Xlib.h>
typedef struct
{
  Display *display;
} X11WindowData;

EGLNativeWindowType platform_create_native_window_x11 (gint x, gint y, gint width, gint height, gpointer * window_data);
gboolean platform_destroy_native_window_x11 (EGLNativeDisplayType display,
    EGLNativeWindowType w, gpointer * window_data);
#endif

#ifdef USE_EGL_WAYLAND
#include <wayland-client.h>
#include <wayland-client-core.h>
#include "wayland-client-protocol.h"
#include "wayland-egl.h"
#include "presentation-time-client-protocol.h"
#include "ivi-application-client-protocol.h"
typedef struct
{
  struct wl_egl_window *egl_window;
  struct wl_shell_surface *shell_surface;
  struct ivi_surface *ivi_surface;
  struct wl_surface *surface;
} WaylandWindowData;

typedef struct
{
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_shell *shell;
  struct wp_presentation *presentation;
  struct wp_presentation_feedback *presentation_feedback;
  struct wl_registry *registry;
  struct ivi_application *ivi_application;
  guint32 clock_id;
} WaylandDisplay;

EGLNativeWindowType platform_create_native_window_wayland (gint x, gint y, gint width, gint height, guint ivisurf_id, gpointer * window_data);
EGLNativeDisplayType platform_initialize_display_wayland (void);
gboolean platform_destroy_native_window_wayland (EGLNativeDisplayType display,
    EGLNativeWindowType w, gpointer * window_data);
gboolean platform_destroy_display_wayland (void);
gboolean register_frame_callback_wayland(gpointer * window_data, GstBuffer * buf);
gboolean register_presentation_feedback(gpointer * window_data, GstBuffer * buf);
#endif

gboolean platform_wrapper_init (void);

#if !defined(USE_EGL_X11) && !defined(USE_EGL_WAYLAND)
EGLNativeWindowType platform_create_native_window (gint x, gint y, gint width, gint height, gpointer * window_data);
gboolean platform_destroy_native_window (EGLNativeDisplayType display,
    EGLNativeWindowType w, gpointer * window_data);
#endif

#endif
