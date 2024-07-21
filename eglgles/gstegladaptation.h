/*
 * GStreamer EGL/GLES Sink Adaptation
 * Copyright (C) 2012-2013 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *   @author: Thiago Santos <thiago.sousa.santos@collabora.com>
 * Copyright (c) 2015-2024, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef __GST_EGL_ADAPTATION_H__
#define __GST_EGL_ADAPTATION_H__

#include <config.h>

#include <gst/gst.h>
#include <gst/video/gstvideopool.h>

/* USE_EGL_RPI 表示使用树莓派 */
#if defined (USE_EGL_RPI) && defined(__GNUC__)
#ifndef __VCCOREVER__
#define __VCCOREVER__ 0x04000000
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#pragma GCC optimize ("gnu89-inline")
#endif

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

/* HAVE_IOS 表示对于苹果的IOS */
#ifdef HAVE_IOS
#include <OpenGLES/ES2/gl.h>
#else
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif

#include "gsteglimageallocator.h"

#if defined (USE_EGL_RPI) && defined(__GNUC__)
#pragma GCC reset_options
#pragma GCC diagnostic pop
#endif

G_BEGIN_DECLS

typedef struct _GstEglAdaptationContext GstEglAdaptationContext;
typedef struct _GstEglGlesImageFmt GstEglGlesImageFmt; /* 没有使用 */

#ifdef HAVE_IOS
typedef struct _GstEaglContext GstEaglContext;
#else
typedef struct _GstEglGlesRenderContext GstEglGlesRenderContext;  /* EGLConfig、EGLContext、EGLSurface（egl配置、上下文、表面） */
#endif

typedef struct _coord5
{
  float x;
  float y;
  float z;
  float a;                      /* texpos x */
  float b;                      /* texpos y */
} coord5;


/***
 * @GstEGLGLESImageData:
 * 只有有在创建空GstBuffer的时候才会调用
 * 而且创建的这个空GstBuffer只是为了回复别人查询，并不是真正的GstBuffer
*/
typedef struct
{
  GLuint texture;
  EGLDisplay display;
  EGLContext eglcontext;
} GstEGLGLESImageData;

/*
 * GstEglAdaptationContext:
 * @have_vbo: Set if the GLES VBO setup has been performed
 * @have_texture: Set if the GLES texture setup has been performed
 * @have_surface: Set if the EGL surface setup has been performed
 *  
 * The #GstEglAdaptationContext data structure.
 */
/**
 * GstEglAdaptationContext
 * @window: @window和@used_window是相等的，在配置Cap的时候，执行eglglessink->egl_context->used_window = eglglessink->egl_context->window;
 * @used_window: used_window = window
*/
struct _GstEglAdaptationContext
{
  GstElement *element;

#ifdef HAVE_IOS
  GstEaglContext *eaglctx;
  void * window, *used_window;
#else
  GstEGLDisplay *display, *set_display; /* egl显示display、创建EGLImageKHR相关函数指针 */
#endif
  
  GLuint fragshader[2]; /* fragshader[0]表示正常片段着色程序ID， fragshader[1]表示不能保留前一帧buffer相关的片段着色程序ID（一般不会被复制） */
  GLuint vertshader[2]; /* vertshader[0]表示正常顶点着色程序ID， vertshader[1]表示不能保留前一帧buffer相关的顶点着色程序ID（一般不会被复制） */
  GLuint glslprogram[2]; /* glslprogram[0]表示正常整个着色程序的ID， glslprogram[1]表示不能保留前一帧buffer相关的整个着色程序ID（一般不会被复制） */
  GLuint texture[4]; /* RGBA只使用texture[0]，RGB/Y, U/UV, V */


  /* shader vars */
  GLuint position_loc[2]; /* position_loc[0]表示顶点位置属性ID */
  GLuint texpos_loc[1]; /* texpos_loc[0]表示顶点纹理位置属性ID */
  /* tex_scale_loc[0][0]表示uniform vec2 tex_scale0  
   * tex_scale_loc[0][1]表示uniform vec2 tex_scale1
   * tex_scale_loc[0][2]表示uniform vec2 tex_scale2
   */
  GLuint tex_scale_loc[1][3]; /* [frame] RGB/Y, U/UV, V */
  /* tex_loc[0][0]表示纹理的ID（以前还没用过该变量） */
  GLuint tex_loc[1][3]; /* [frame] RGB/Y, U/UV, V */
  coord5 position_array[16];    /* 4 x Frame x-normal,y-normal, 4x Frame x-normal,y-flip, 4 x Border1, 4 x Border2 */
  unsigned short index_array[4];
  unsigned int position_buffer, index_buffer;
  gint n_textures; /* 一共有多少个纹理，一般视频格式都是RGBA，所以只创建一个纹理texture[0] */

  gint surface_width; /* 创建的surface表面宽度 */
  gint surface_height; /* 创建的surface表面高度 */
  gint pixel_aspect_ratio_n; /* 缩放因子的分子 */
  gint pixel_aspect_ratio_d; /* 缩放因子的分母 */

  gboolean have_vbo;
  gboolean have_texture; /* 是否成功创建纹理 glGenTextures */
  gboolean have_surface; /* 是否成功创建并赋值了surface */
  gboolean buffer_preserved; /* 根据系统特性，是否能保存交换buffer前的一帧buffer */

  EGLContext egl_context;
};

GST_DEBUG_CATEGORY_EXTERN (egladaption_debug);

void gst_egl_adaption_init (void);

GstEglAdaptationContext * gst_egl_adaptation_context_new (GstElement * element);
void gst_egl_adaptation_context_free (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_init (GstEglAdaptationContext * ctx);
void gst_egl_adaptation_deinit (GstEglAdaptationContext * ctx);

#ifndef HAVE_IOS
EGLContext gst_egl_adaptation_context_get_egl_context (GstEglAdaptationContext * ctx);
#endif

GstCaps *gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_choose_config (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_init_surface (GstEglAdaptationContext * ctx, GstVideoFormat format, gboolean tex_external_oes);
void gst_egl_adaptation_init_exts (GstEglAdaptationContext * ctx);
gboolean gst_egl_adaptation_update_surface_dimensions (GstEglAdaptationContext * ctx);

gboolean got_gl_error (const char *wtf);
gboolean got_egl_error (const char *wtf);

gboolean gst_egl_adaptation_context_make_current (GstEglAdaptationContext * ctx, gboolean bind);
void gst_egl_adaptation_cleanup (GstEglAdaptationContext * ctx);

void gst_egl_adaptation_bind_API (GstEglAdaptationContext * ctx);

gboolean gst_egl_adaptation_context_swap_buffers (GstEglAdaptationContext * ctx, gchar* winsys, gpointer * own_window_data, GstBuffer *buf, gboolean show_latency);

gboolean gst_egl_adaptation_reset_window (GstEglAdaptationContext * ctx, GstVideoFormat format, gboolean tex_external_oes);

#ifndef HAVE_IOS
/* TODO: The goal is to move this function to gstegl lib (or
 * splitted between gstegl lib and gstgl lib) in order to be used in
 * webkitVideoSink
 * So it has to be independent of GstEglAdaptationContext */
GstBuffer *
gst_egl_image_allocator_alloc_eglimage (GstAllocator * allocator,
    GstEGLDisplay * display, EGLContext eglcontext, GstVideoFormat format,
    gint width, gint height);
#endif

G_END_DECLS

#endif /* __GST_EGL_ADAPTATION_H__ */
