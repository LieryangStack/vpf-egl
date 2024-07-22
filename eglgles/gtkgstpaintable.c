#include "gtkgstpaintable.h"
#include "gsteglglessink.h"

struct _GtkGstPaintableClass {
  GObjectClass parent_class;
};

struct _GtkGstPaintable {
  GObject parent_instance;
  GstEglGlesSink *sink;
  GdkGLContext *context;
};

static void
gtk_gst_paintable_paintable_snapshot (GdkPaintable *paintable,
                                      GdkSnapshot  *snapshot,
                                      double        width,
                                      double        height) {
  /* 将之前的绘图操作保存在堆栈中，避免新的绘图操作影响之前的绘图内容 */
  gtk_snapshot_save (snapshot);
  gtk_snapshot_scale (snapshot, width, height);

  static gboolean flag = FALSE;

  GdkGLTextureBuilder *builder = gdk_gl_texture_builder_new ();
  gdk_gl_texture_builder_set_context (builder, GTK_GST_PAINTABLE(paintable)->context);
  gdk_gl_texture_builder_set_id (builder, GTK_GST_PAINTABLE(paintable)->sink->egl_context->texture[0]);
  gdk_gl_texture_builder_set_width (builder, 1);
  gdk_gl_texture_builder_set_height (builder, 1);


  GdkTexture *texture = gdk_gl_texture_builder_build (builder,NULL, NULL);

  gtk_snapshot_append_texture (snapshot, texture, &GRAPHENE_RECT_INIT(0, 0, 1, 1));

  g_object_unref (texture);
  g_object_unref (builder);

  /* 用于将之前保存的状态从堆栈中恢复 */
  gtk_snapshot_restore (snapshot);
}

static void
gtk_gst_paintable_paintable_init (GdkPaintableInterface *iface)
{
  iface->snapshot = gtk_gst_paintable_paintable_snapshot;
}

static void
gtk_gst_paintable_class_init (GtkGstPaintableClass *class) {

}

static void
gtk_gst_paintable_init (GtkGstPaintable *object) {

}

G_DEFINE_TYPE_WITH_CODE (GtkGstPaintable, gtk_gst_paintable, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (GDK_TYPE_PAINTABLE,
                                                gtk_gst_paintable_paintable_init));

GdkPaintable *
gtk_gst_paintable_new (void) {
  return g_object_new (GTK_TYPE_GST_PAINTABLE, NULL);
}

GdkGLContext *
gtk_gst_paintable_get_context (GtkGstPaintable *paintable) {
  return paintable->context;
}

void
gtk_gst_paintable_set_context (GtkGstPaintable *paintable, GdkGLContext* context) {
  paintable->context = context;
}

void
gtk_gst_paintable_set_sink (GtkGstPaintable *paintable, GstElement* sink) {
  
  paintable->sink = GST_EGLGLESSINK(sink);
}