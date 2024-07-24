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
 * @GstEGLGLESImageData
 * 内存池创建GstBuffer的时候是用
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

GstCaps *gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext * ctx);

gboolean got_gl_error (const char *wtf);
gboolean got_egl_error (const char *wtf);

G_END_DECLS

#endif /* __GST_EGL_ADAPTATION_H__ */
