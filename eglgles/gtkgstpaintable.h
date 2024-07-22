#ifndef __GTK_GST_PAINTABLE_H__
#define __GTK_GST_PAINTABLE_H__


#include <gtk/gtk.h>
#include <gst/gst.h>

#define GTK_TYPE_GST_PAINTABLE (gtk_gst_paintable_get_type ())

G_DECLARE_FINAL_TYPE (GtkGstPaintable, gtk_gst_paintable, GTK, GST_PAINTABLE, GObject)

GdkPaintable *
gtk_gst_paintable_new (void);

GdkGLContext *
gtk_gst_paintable_get_context (GtkGstPaintable *paintable);

void
gtk_gst_paintable_set_context (GtkGstPaintable *paintable, GdkGLContext* context);

void
gtk_gst_paintable_set_sink (GtkGstPaintable *paintable, GstElement* sink);

#endif

