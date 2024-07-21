
#include "gstegladaptation.h"
#include "gsteglglessink.h"
#include <gst/video/video.h>
#include <string.h>

#define GST_CAT_DEFAULT egladaption_debug
GST_DEBUG_CATEGORY (egladaption_debug);


/* GLESv2 GLSL 着色语言
 *
 * OpenGL ES 标准不强制要求支持 YUV。这就是为什么大多数这些着色器都处理打包/平面 YUV 到 RGB 的转换。
 */

/* *INDENT-OFF* */
/* 顶点着色器程序 */
/**
 * @param position(in): 顶点位置
 * @param texpos(in): 纹理位置
 * @param opos(out): 把顶点位置输出到片段着色器程序中
*/
static const char *vert_COPY_prog = {
      "attribute vec3 position;"
      "attribute vec2 texpos;"
      "varying vec2 opos;"
      "void main(void)"
      "{"
      " opos = texpos;"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

/* 顶点着色器程序（不需要纹理） */
static const char *vert_COPY_prog_no_tex = {
      "attribute vec3 position;"
      "void main(void)"
      "{"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

/* Paint all black */
static const char *frag_BLACK_prog = {
  "precision mediump float;"
      "void main(void)"
      "{"
      " gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);"
      "}"
};

/* 以下全部都是不同视频格式转RGB视频格式的着色语言，会在编译着色程序前使用 */

/* 这个是没有使用OES扩展的，片段着色器程序 */
static const char *frag_COPY_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.rgb, 1.0);"
      "}"
};

/* 这个是使用OES扩展的，片段着色器程序 */
static const char *frag_COPY_externel_oes_prog = {
  "#extension GL_OES_EGL_image_external : require\n"
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform samplerExternalOES tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " gl_FragColor = texture2D(tex, opos / tex_scale0);"
      "}"
};


/* Channel reordering for XYZ <-> ZYX conversion */
static const char *frag_REORDER_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos / tex_scale0);"
      " gl_FragColor = vec4(t.%c, t.%c, t.%c, 1.0);"
      "}"
};

/* Packed YUV converters */

/** AYUV to RGB conversion */
static const char *frag_AYUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv  = texture2D(tex,opos / tex_scale0).gba;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/* Planar YUV converters */

/** YUV to RGB conversion */
static const char *frag_PLANAR_YUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,Utex,Vtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.y=texture2D(Utex,opos / tex_scale1).r;"
      "  yuv.z=texture2D(Vtex,opos / tex_scale2).r;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

/** NV12/NV21 to RGB conversion */
static const char *frag_NV12_NV21_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,UVtex;"
      "uniform vec2 tex_scale0;"
      "uniform vec2 tex_scale1;"
      "uniform vec2 tex_scale2;"
      "const vec3 offset = vec3(-0.0625, -0.5, -0.5);"
      "const vec3 rcoeff = vec3(1.164, 0.000, 1.596);"
      "const vec3 gcoeff = vec3(1.164,-0.391,-0.813);"
      "const vec3 bcoeff = vec3(1.164, 2.018, 0.000);"
      "void main(void) {"
      "  float r,g,b;"
      "  vec3 yuv;"
      "  yuv.x=texture2D(Ytex,opos / tex_scale0).r;"
      "  yuv.yz=texture2D(UVtex,opos / tex_scale1).%c%c;"
      "  yuv += offset;"
      "  r = dot(yuv, rcoeff);"
      "  g = dot(yuv, gcoeff);"
      "  b = dot(yuv, bcoeff);"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};
/* *INDENT-ON* */

void
gst_egl_adaption_init (void)
{
  GST_DEBUG_CATEGORY_INIT (egladaption_debug, "egladaption", 0,
      "EGL adaption layer");
}

/**
 * @brief: 创建一个"video/x-raw"的GstCap
*/
static GstCaps *
_gst_video_format_new_template_caps (GstVideoFormat format)
{
  return gst_caps_new_simple ("video/x-raw",
      "format", G_TYPE_STRING, gst_video_format_to_string (format),
      "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
}

GstCaps *
gst_egl_adaptation_fill_supported_fbuffer_configs (GstEglAdaptationContext *
    ctx)
{
  GstCaps *caps = NULL, *copy1, *copy2;
  guint i, n;

  GST_DEBUG_OBJECT (ctx->element,
      "Building initial list of wanted eglattribs per format");

  caps = gst_caps_new_empty ();

  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y42B));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB16));

  copy1 = gst_caps_copy (caps);
  copy2 = gst_caps_copy (caps);

  #ifndef HAVE_IOS
  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) { /* video/x-raw(memory:EGLImage) */
    GstCapsFeatures *features =
        gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_EGL_IMAGE, NULL);
    gst_caps_set_features (caps, i, features);
  }
  #endif

  n = gst_caps_get_size (copy1);
  for (i = 0; i < n; i++) { /* video/x-raw(meta:GstVideoGLTextureUploadMeta) */
    GstCapsFeatures *features =
        gst_caps_features_new
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META, NULL);
    gst_caps_set_features (copy1, i, features);
  }

  gst_caps_append (caps, copy1);
  gst_caps_append (caps, copy2);

  n = gst_caps_get_size (caps);
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB));
  gst_caps_append (caps,
      _gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
  for (i = n; i < n+6; i++) {  /* video/x-raw(memory:NVMM) */
    GstCapsFeatures *features =
        gst_caps_features_new ("memory:NVMM", NULL);
    gst_caps_set_features (caps, i, features);
  }

  /* 是否应该添加检查  是否支持 RGBA8888 config 属性？？ */
  // } else {
  //   GST_INFO_OBJECT (ctx->element,
  //       "EGL display doesn't support RGBA8888 config");
  // }

  return caps;
}

/**
 * @brief: 清理顶点数据，删除着色器程序，删除EGLSurface和上下文
*/
void
gst_egl_adaptation_cleanup (GstEglAdaptationContext * ctx)
{
  gint i;

  if (ctx->have_vbo) {
    glDeleteBuffers (1, &ctx->position_buffer);
    glDeleteBuffers (1, &ctx->index_buffer);
    ctx->have_vbo = FALSE;
  }

  if (ctx->have_texture) {
    glDeleteTextures (ctx->n_textures, ctx->texture);
    ctx->have_texture = FALSE;
    ctx->n_textures = 0;
  }

  for (i = 0; i < 2; i++) {
    if (ctx->glslprogram[i]) {
      glUseProgram (0);
      glDetachShader (ctx->glslprogram[i], ctx->fragshader[i]);
      glDetachShader (ctx->glslprogram[i], ctx->vertshader[i]);
      glDeleteProgram (ctx->glslprogram[i]);
      glDeleteShader (ctx->fragshader[i]);
      glDeleteShader (ctx->vertshader[i]);
      ctx->glslprogram[i] = 0;
      ctx->fragshader[i] = 0;
      ctx->vertshader[i] = 0;
    }
  }

  gst_egl_adaptation_context_make_current (ctx, FALSE);

  gst_egl_adaptation_destroy_surface (ctx);
  gst_egl_adaptation_destroy_context (ctx);
}

gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned 0x%04x", wtf, error);
    g_print ("GL ERROR: %s returned 0x%04x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

/**
 * @brief: 编译着色器程序
 * @param prog(out): 着色程序对象标识ID
 * @param vert(out): 顶点着色器程序ID
 * @param frag(out): 片段着色器程序ID
 * @param vert_text(in): 顶点着色程序源代码
 * @param frag_text(in): 片段着色程序源代码
*/
static gboolean
create_shader_program (GstEglAdaptationContext * ctx, GLuint * prog,
    GLuint * vert, GLuint * frag, const gchar * vert_text,
    const gchar * frag_text)
{
  GLint test;
  GLchar *info_log;

  /* Build shader program for video texture rendering */
  *vert = glCreateShader (GL_VERTEX_SHADER);
  GST_DEBUG_OBJECT (ctx->element, "Sending %s to handle %d", vert_text, *vert);
  glShaderSource (*vert, 1, &vert_text, NULL);
  if (got_gl_error ("glShaderSource vertex"))
    goto HANDLE_ERROR;

  glCompileShader (*vert);
  if (got_gl_error ("glCompileShader vertex"))
    goto HANDLE_ERROR;

  glGetShaderiv (*vert, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (ctx->element, "Successfully compiled vertex shader");
  else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't compile vertex shader");
    glGetShaderiv (*vert, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*vert, test, NULL, info_log);
    GST_INFO_OBJECT (ctx->element, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *frag = glCreateShader (GL_FRAGMENT_SHADER);
  GST_DEBUG_OBJECT (ctx->element, "Sending %s to handle %d", frag_text, *frag);
  glShaderSource (*frag, 1, &frag_text, NULL);
  if (got_gl_error ("glShaderSource fragment"))
    goto HANDLE_ERROR;

  glCompileShader (*frag);
  if (got_gl_error ("glCompileShader fragment"))
    goto HANDLE_ERROR;

  glGetShaderiv (*frag, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (ctx->element, "Successfully compiled fragment shader");
  else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't compile fragment shader");
    glGetShaderiv (*frag, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (*frag, test, NULL, info_log);
    GST_INFO_OBJECT (ctx->element, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  *prog = glCreateProgram ();
  if (got_gl_error ("glCreateProgram"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *vert);
  if (got_gl_error ("glAttachShader vertices"))
    goto HANDLE_ERROR;
  glAttachShader (*prog, *frag);
  if (got_gl_error ("glAttachShader fragments"))
    goto HANDLE_ERROR;
  glLinkProgram (*prog);
  glGetProgramiv (*prog, GL_LINK_STATUS, &test);
  if (test != GL_FALSE) {
    GST_DEBUG_OBJECT (ctx->element, "GLES: Successfully linked program");
  } else {
    GST_ERROR_OBJECT (ctx->element, "Couldn't link program");
    goto HANDLE_ERROR;
  }

  return TRUE;

HANDLE_ERROR:
  {
    if (*frag && *prog)
      glDetachShader (*prog, *frag);
    if (*vert && *prog)
      glDetachShader (*prog, *vert);
    if (*prog)
      glDeleteProgram (*prog);
    if (*frag)
      glDeleteShader (*frag);
    if (*vert)
      glDeleteShader (*vert);
    *prog = 0;
    *frag = 0;
    *vert = 0;

    return FALSE;
  }
}

/**
 * @brief: 1. 创建 EGLSurface
 *         2. 当前线程绑定 EGLContext 
 *         3. 编译着色器程序并获取相关顶点属性和uniform标识ID
 *         4. 生成纹理
 * @param format: 视频帧的格式
 * @param tex_external_oes: 如果使用的Jetson（GPU和CPU共享内存设备），这个就赋值TRUE（我认为CUDA也可以使用部分扩展，因为未读完整个代码，该部分未详细解释）
*/
gboolean
gst_egl_adaptation_init_surface (GstEglAdaptationContext * ctx,
    GstVideoFormat format, gboolean tex_external_oes)
{
  GLboolean ret;
  const gchar *texnames[3] = { NULL, };
  gchar *frag_prog = NULL;
  gboolean free_frag_prog = FALSE;
  gint i;
  GLint target;

  GST_DEBUG_OBJECT (ctx->element, "Enter EGL surface setup");

  /* 创建 EGLSurface */
  if (!gst_egl_adaptation_create_surface (ctx)) {
    GST_ERROR_OBJECT (ctx->element, "Can't create surface");
    goto HANDLE_ERROR_LOCKED;
  }

  /* 当前线程绑定egl上下文 */
  if (!gst_egl_adaptation_context_make_current (ctx, TRUE))
    goto HANDLE_ERROR_LOCKED;

  /* 根据查询信息，是否支持保存交换buffer之前的buffer（上一帧） */
  gst_egl_adaptation_query_buffer_preserved (ctx);

  /* 查询egl支持的扩展信息 */
  gst_egl_adaptation_init_exts (ctx);

  /* 保存surface维度信息 */
  gst_egl_adaptation_update_surface_dimensions (ctx);

  /* 显示器像素缩放因子 */
  gst_egl_adaptation_query_par (ctx);

  /* 成功创建了EGLSurface */
  ctx->have_surface = TRUE;

  /* 查看着色编译器是否可用 */
  glGetBooleanv (GL_SHADER_COMPILER, &ret);
  if (ret == GL_FALSE) {
    GST_ERROR_OBJECT (ctx->element, "Shader compiler support is unavailable!");
    goto HANDLE_ERROR;
  }

  /* Build shader program for video texture rendering */
  g_print ("format = %d\n", format);
  switch (format) {
          
    case GST_VIDEO_FORMAT_AYUV:
      frag_prog = (gchar *) frag_AYUV_prog;
      free_frag_prog = FALSE;  /* FALSE表面 frag_prog 是字符串常量，不需要内存释放，如果 g_strdup_printf，则需要释放*/
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      frag_prog = (gchar *) frag_PLANAR_YUV_prog;
      free_frag_prog = FALSE;
      ctx->n_textures = 3;
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_NV12:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a'); /* free_frag_prog 赋值TRUE，需要 g_free (frag_prog) */
      free_frag_prog = TRUE;
      ctx->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      frag_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
      free_frag_prog = TRUE;
      ctx->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
      free_frag_prog = TRUE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
      free_frag_prog = TRUE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      frag_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
      free_frag_prog = TRUE;
      ctx->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA: /* 一般是RGBA，所以一般只创建一个纹理 */
    case GST_VIDEO_FORMAT_RGB16:
      frag_prog = (gchar *) frag_COPY_prog;
      free_frag_prog = FALSE;
      ctx->n_textures = 1;
      g_print ("format = %d\n", format);
      texnames[0] = "tex";
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  /* 如果使用扩展，就执行。（Jetson肯定执行） */
  if (tex_external_oes) {
    frag_prog = (gchar *) frag_COPY_externel_oes_prog;
    free_frag_prog = FALSE;
    ctx->n_textures = 1;
    texnames[0] = "tex";
  }
  
  /* 编译着色器程序 */
  if (!create_shader_program (ctx,
          &ctx->glslprogram[0],
          &ctx->vertshader[0],
          &ctx->fragshader[0], vert_COPY_prog, frag_prog)) { /* 着色程序编译失败执行 */
    if (free_frag_prog)
      g_free (frag_prog);
    frag_prog = NULL;
    goto HANDLE_ERROR;
  }

  if (free_frag_prog)  /* 如果 frag_prog 是通过 g_strdup_printf, 需要释放 */
    g_free (frag_prog);
  frag_prog = NULL;

  /* 获取着色程序中相关变量的ID */
  ctx->position_loc[0] = glGetAttribLocation (ctx->glslprogram[0], "position");
  ctx->texpos_loc[0] = glGetAttribLocation (ctx->glslprogram[0], "texpos");
  ctx->tex_scale_loc[0][0] =
      glGetUniformLocation (ctx->glslprogram[0], "tex_scale0");
  ctx->tex_scale_loc[0][1] =
      glGetUniformLocation (ctx->glslprogram[0], "tex_scale1");
  ctx->tex_scale_loc[0][2] =
      glGetUniformLocation (ctx->glslprogram[0], "tex_scale2");

  for (i = 0; i < ctx->n_textures; i++) {
    ctx->tex_loc[0][i] =
        glGetUniformLocation (ctx->glslprogram[0], texnames[i]);
  }

  /* 交换Buffer前一帧buffer不能保留才会执行 */
  if (!ctx->buffer_preserved) {
    /* Build shader program for black borders */
    if (!create_shader_program (ctx,
            &ctx->glslprogram[1],
            &ctx->vertshader[1],
            &ctx->fragshader[1], vert_COPY_prog_no_tex, frag_BLACK_prog))
      goto HANDLE_ERROR;

    ctx->position_loc[1] =
        glGetAttribLocation (ctx->glslprogram[1], "position");
  }

  g_print ("tex_external_oes = %d\n", tex_external_oes);

  /* 生成纹理 */
  if (!ctx->have_texture) {
    GST_INFO_OBJECT (ctx->element, "Performing initial texture setup");
    if (tex_external_oes) {
      target = GL_TEXTURE_EXTERNAL_OES;
    } else {
      target = GL_TEXTURE_2D;
    }
    glGenTextures (ctx->n_textures, ctx->texture);

    if (got_gl_error ("glGenTextures"))
      goto HANDLE_ERROR_LOCKED;

    for (i = 0; i < ctx->n_textures; i++) {
      g_print ("ctx->texture[i] = %d\n", ctx->texture[i]);
      glActiveTexture (GL_TEXTURE0);
      glBindTexture (target, ctx->texture[i]);
      if (got_gl_error ("glBindTexture"))
        goto HANDLE_ERROR;

      /* Set 2D resizing params */
      glTexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      /* If these are not set the texture image unit will return
       * (R, G, B, A) = black on glTexImage2D for non-POT width/height
       * frames. For a deeper explanation take a look at the OpenGL ES
       * documentation for glTexParameter */
      glTexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (got_gl_error ("glTexParameteri"))
        goto HANDLE_ERROR_LOCKED;
    }
    
    g_print ("god god god\n");
    ctx->have_texture = TRUE;
  }

  glUseProgram (0);

  return TRUE;

  /* Errors */
HANDLE_ERROR_LOCKED:
HANDLE_ERROR:
  GST_ERROR_OBJECT (ctx->element, "Couldn't setup EGL surface");
  return FALSE;
}


/**
 * 选择egl配置组时候所需的属性信息
*/
static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};


/**
 * @brief: 该函数只有在创建GstBuffer的时候才会调用，而且创建的这个空GstBuffer只是为了回复别人查询，并不是真正的GstBuffer
*/
static void
gst_egl_gles_image_data_free (GstEGLGLESImageData * data)
{
  if (!eglMakeCurrent (data->display,
      EGL_NO_SURFACE, EGL_NO_SURFACE, data->eglcontext)) {
      got_egl_error ("eglMakeCurrent");
      g_slice_free (GstEGLGLESImageData, data);
      return;
  }
  glDeleteTextures (1, &data->texture);
  g_slice_free (GstEGLGLESImageData, data);
}


/**
 * @brief: 获取Buffer，但是该该Buffer中的GstMemory的图片是EGLImageKHR，
 *         这个EGLImageKHR是空的，因为里面函数新生成一个纹理（该纹理并没有被更新）
 * @calledby: 只会在查询元素的时候 eglglessink-allocate-eglimage 时候，才会调用，调用几率很小
*/
GstBuffer *
gst_egl_image_allocator_alloc_eglimage (GstAllocator * allocator,
    GstEGLDisplay * display, EGLContext eglcontext, GstVideoFormat format,
    gint width, gint height)
{
  GstEGLGLESImageData *data = NULL;
  GstBuffer *buffer;
  GstVideoInfo info;
  guint i;
  gint stride[GST_VIDEO_MAX_PLANES]; /* 一行占用的多少字节内存，比如RGB888就是 3 * width */
  gsize offset[GST_VIDEO_MAX_PLANES];
  GstMemory *mem[3] = { NULL, NULL, NULL }; /* 新建GstBuffer，然后把该mem添加到该Buffer中，然后返回 */
  guint n_mem;
  GstMemoryFlags flags = 0;

  memset (stride, 0, sizeof (stride));
  memset (offset, 0, sizeof (offset));

  /* 表示该内存块不能映射到用户可访问的指针地址空间（因为可能返回的，通过当前上下文创建的EGLImageKHR） */
  if (!gst_egl_image_memory_is_mappable ())
    flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  /* See https://bugzilla.gnome.org/show_bug.cgi?id=695203 */
  flags |= GST_MEMORY_FLAG_NO_SHARE;


  gst_video_info_set_format (&info, format, width, height);

  switch (format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:{
      gsize size;  /* 一帧占用的多少字节内存 */
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {  /* gst_egl_image_allocator_alloc这返回的是NULL */
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else { /* 所以会执行该部分 */
        data = g_slice_new0 (GstEGLGLESImageData);
        data->display = gst_egl_display_get (display);
        data->eglcontext = eglcontext;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 3);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            gst_egl_display_image_create (display,
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (uintptr_t) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      EGLImageKHR image;
      gsize size;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGB, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);
        data->display = gst_egl_display_get (display);
        data->eglcontext = eglcontext;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 2);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
            NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        image =
            gst_egl_display_image_create (display,
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (uintptr_t) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGB,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);
        n_mem = 1;
      }
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      EGLImageKHR image;
      gsize size[2];

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA,
          GST_VIDEO_INFO_COMP_WIDTH (&info, 1),
          GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);

      if (mem[0] && mem[1]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 2;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        mem[0] = mem[1] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1) * 2);
        offset[1] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[0] = offset[1];
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);

        for (i = 0; i < 2; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);
          data->display = gst_egl_display_get (display);
          data->eglcontext = eglcontext;

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          if (i == 0)
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
                GL_UNSIGNED_BYTE, NULL);
          else
            glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                GST_VIDEO_INFO_COMP_WIDTH (&info, i),
                GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE_ALPHA,
                GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              gst_egl_display_image_create (display,
              eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (uintptr_t) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, display,
              image,
              (i ==
                  0 ? GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE :
                  GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE_ALPHA),
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 2;
      }
      break;
    }
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      EGLImageKHR image;
      gsize size[3];

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              0), GST_VIDEO_INFO_COMP_HEIGHT (&info, 0), &size[0]);
      mem[1] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              1), GST_VIDEO_INFO_COMP_HEIGHT (&info, 1), &size[1]);
      mem[2] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE, GST_VIDEO_INFO_COMP_WIDTH (&info,
              2), GST_VIDEO_INFO_COMP_HEIGHT (&info, 2), &size[2]);

      if (mem[0] && mem[1] && mem[2]) {
        stride[0] = size[0] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[1] = size[0];
        stride[1] = size[1] / GST_VIDEO_INFO_HEIGHT (&info);
        offset[2] = size[1];
        stride[2] = size[2] / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 3;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[1], GST_MEMORY_FLAG_NO_SHARE);
        GST_MINI_OBJECT_FLAG_SET (mem[2], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        if (mem[0])
          gst_memory_unref (mem[0]);
        if (mem[1])
          gst_memory_unref (mem[1]);
        if (mem[2])
          gst_memory_unref (mem[2]);
        mem[0] = mem[1] = mem[2] = NULL;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 0));
        stride[1] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 1));
        stride[2] = GST_ROUND_UP_4 (GST_VIDEO_INFO_COMP_WIDTH (&info, 2));
        size[0] = stride[0] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 0);
        size[1] = stride[1] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 1);
        size[2] = stride[2] * GST_VIDEO_INFO_COMP_HEIGHT (&info, 2);
        offset[0] = 0;
        offset[1] = size[0];
        offset[2] = offset[1] + size[1];

        for (i = 0; i < 3; i++) {
          data = g_slice_new0 (GstEGLGLESImageData);
          data->display = gst_egl_display_get (display);
          data->eglcontext = eglcontext;

          glGenTextures (1, &data->texture);
          if (got_gl_error ("glGenTextures"))
            goto mem_error;

          glBindTexture (GL_TEXTURE_2D, data->texture);
          if (got_gl_error ("glBindTexture"))
            goto mem_error;

          /* Set 2D resizing params */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

          /* If these are not set the texture image unit will return
           * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
           * * frames. For a deeper explanation take a look at the OpenGL ES
           * * documentation for glTexParameter */
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
          glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
          if (got_gl_error ("glTexParameteri"))
            goto mem_error;

          glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
              GST_VIDEO_INFO_COMP_WIDTH (&info, i),
              GST_VIDEO_INFO_COMP_HEIGHT (&info, i), 0, GL_LUMINANCE,
              GL_UNSIGNED_BYTE, NULL);

          if (got_gl_error ("glTexImage2D"))
            goto mem_error;

          image =
              gst_egl_display_image_create (display,
              eglcontext, EGL_GL_TEXTURE_2D_KHR,
              (EGLClientBuffer) (uintptr_t) data->texture, NULL);
          if (got_egl_error ("eglCreateImageKHR"))
            goto mem_error;

          mem[i] =
              gst_egl_image_allocator_wrap (allocator, display,
              image, GST_VIDEO_GL_TEXTURE_TYPE_LUMINANCE,
              flags, size[i], data,
              (GDestroyNotify) gst_egl_gles_image_data_free);
        }

        n_mem = 3;
      }
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:{
      gsize size;
      EGLImageKHR image;

      mem[0] =
          gst_egl_image_allocator_alloc (allocator, display,
          GST_VIDEO_GL_TEXTURE_TYPE_RGBA, GST_VIDEO_INFO_WIDTH (&info),
          GST_VIDEO_INFO_HEIGHT (&info), &size);
      if (mem[0]) {
        stride[0] = size / GST_VIDEO_INFO_HEIGHT (&info);
        n_mem = 1;
        GST_MINI_OBJECT_FLAG_SET (mem[0], GST_MEMORY_FLAG_NO_SHARE);
      } else {
        data = g_slice_new0 (GstEGLGLESImageData);
        data->display = gst_egl_display_get (display);
        data->eglcontext = eglcontext;

        stride[0] = GST_ROUND_UP_4 (GST_VIDEO_INFO_WIDTH (&info) * 4);
        size = stride[0] * GST_VIDEO_INFO_HEIGHT (&info);

        glGenTextures (1, &data->texture);
        if (got_gl_error ("glGenTextures"))
          goto mem_error;

        glBindTexture (GL_TEXTURE_2D, data->texture);
        if (got_gl_error ("glBindTexture"))
          goto mem_error;

        /* Set 2D resizing params */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* If these are not set the texture image unit will return
         * * (R, G, B, A) = black on glTexImage2D for non-POT width/height
         * * frames. For a deeper explanation take a look at the OpenGL ES
         * * documentation for glTexParameter */
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (got_gl_error ("glTexParameteri"))
          goto mem_error;

        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
            GST_VIDEO_INFO_WIDTH (&info),
            GST_VIDEO_INFO_HEIGHT (&info), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        if (got_gl_error ("glTexImage2D"))
          goto mem_error;

        /* 通过 egl当前的上下文创建了一个 EGLImageKHR， */
        image =
            gst_egl_display_image_create (display,
            eglcontext, EGL_GL_TEXTURE_2D_KHR,
            (EGLClientBuffer) (uintptr_t) data->texture, NULL);
        if (got_egl_error ("eglCreateImageKHR"))
          goto mem_error;

        mem[0] =
            gst_egl_image_allocator_wrap (allocator, display,
            image, GST_VIDEO_GL_TEXTURE_TYPE_RGBA,
            flags, size, data, (GDestroyNotify) gst_egl_gles_image_data_free);

        n_mem = 1;
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  buffer = gst_buffer_new ();
  gst_buffer_add_video_meta_full (buffer, 0, format, width, height,
      GST_VIDEO_INFO_N_PLANES (&info), offset, stride);

  for (i = 0; i < n_mem; i++)
    gst_buffer_append_memory (buffer, mem[i]);

  return buffer;

mem_error:
  {
    GST_ERROR_OBJECT (GST_CAT_DEFAULT, "Failed to create EGLImage");

    if (data)
      gst_egl_gles_image_data_free (data);

    if (mem[0])
      gst_memory_unref (mem[0]);
    if (mem[1])
      gst_memory_unref (mem[1]);
    if (mem[2])
      gst_memory_unref (mem[2]);

    return NULL;
  }
}

