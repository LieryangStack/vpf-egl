#include "config.h"

#define EGL_EGLEXT_PROTOTYPES

#include "gsteglimageallocator.h"
#include <string.h>


struct _GstEGLDisplay {

  EGLDisplay display; /* egl显示 */
  volatile gint refcount; /* 该结构体引用计数 */
  GDestroyNotify destroy_notify; /* 销毁该结构体前执行的回调函数 */

  PFNEGLCREATEIMAGEKHRPROC eglCreateImage; /* 创建EGLImageKHR */
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImage; /* 销毁EGLImageKHR */
};



/********************************GstEGLImageMemory结构体及其相关函数定义************************************ */
typedef struct {

  GstMemory parent;

  GstEGLDisplay *display;
  EGLImageKHR image;
  GstVideoGLTextureType type; /* 纹理的类型 */
  GstVideoGLTextureOrientation orientation; /* gl纹理的方向 */

  gpointer user_data;
  GDestroyNotify user_data_destroy;
} GstEGLImageMemory;


#define GST_EGL_IMAGE_MEMORY(mem) ((GstEGLImageMemory*)(mem))

gboolean
gst_egl_image_memory_is_mappable (void)
{
  return FALSE;
}

gboolean
gst_is_egl_image_memory (GstMemory * mem)
{
  g_return_val_if_fail (mem != NULL, FALSE);
  g_return_val_if_fail (mem->allocator != NULL, FALSE);

  return g_strcmp0 (mem->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0;
}

EGLImageKHR
gst_egl_image_memory_get_image (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), EGL_NO_IMAGE_KHR);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->image;
}

GstEGLDisplay *
gst_egl_image_memory_get_display (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), NULL);

  if (mem->parent)
    mem = mem->parent;

  return gst_egl_display_ref (GST_EGL_IMAGE_MEMORY (mem)->display);
}

GstVideoGLTextureType
gst_egl_image_memory_get_type (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem), -1);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->type;
}

GstVideoGLTextureOrientation
gst_egl_image_memory_get_orientation (GstMemory * mem)
{
  g_return_val_if_fail (gst_is_egl_image_memory (mem),
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL);

  if (mem->parent)
    mem = mem->parent;

  return GST_EGL_IMAGE_MEMORY (mem)->orientation;
}

void
gst_egl_image_memory_set_orientation (GstMemory * mem,
    GstVideoGLTextureOrientation orientation)
{
  g_return_if_fail (gst_is_egl_image_memory (mem));

  if (mem->parent)
    mem = mem->parent;

  GST_EGL_IMAGE_MEMORY (mem)->orientation = orientation;
}

/********************************************GstEGLImageMemory END***************************************** */



/*************************************GstEGLImageAllocator相关********************************************* */
static GstMemory *
gst_egl_image_allocator_alloc_vfunc (GstAllocator * allocator, 
                                     gsize size,
                                     GstAllocationParams * params) {

  g_warning
      ("Use gst_egl_image_allocator_alloc() to allocate from this allocator");

  return NULL;
}


static void
gst_egl_image_allocator_free_vfunc (GstAllocator * allocator, GstMemory * mem)
{
  GstEGLImageMemory *emem = (GstEGLImageMemory *) mem;
  EGLDisplay display;

  g_return_if_fail (gst_is_egl_image_memory (mem));

  /* 如果有父对象，说明内存被共享了，不应该调用释放函数 */
  if (!mem->parent) {
    display = gst_egl_display_get (emem->display);
    if (emem->display->eglDestroyImage)
      emem->display->eglDestroyImage (display, emem->image);

    if (emem->user_data_destroy)
      emem->user_data_destroy (emem->user_data);

    gst_egl_display_unref (emem->display);
  }

  /* 如果被共享了，是可以释放的这部分内存的，因为共享的时候使用 g_slice_new 创建了一个新的只读 GstEGLImageMemory */
  g_slice_free (GstEGLImageMemory, emem);
}

static gpointer
gst_egl_image_mem_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  return NULL;
}

static void
gst_egl_image_mem_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_egl_image_mem_copy (GstMemory * mem, gssize offset, gssize size)
{
  return NULL;
}

static gboolean
gst_egl_image_mem_is_span (GstMemory * mem1, GstMemory * mem2, gsize * offset)
{
  return FALSE;
}

static GstMemory *
gst_egl_image_mem_share (GstMemory * mem, gssize offset, gssize size) {

  GstMemory *sub;
  GstMemory *parent;

  if (offset != 0)
    return NULL;

  if (size != -1 && size != (gssize)mem->size)
    return NULL;

  /* 查看@mem是否有父对象 */
  if ((parent = mem->parent) == NULL)
    parent = (GstMemory *) mem;

  if (size == -1)
    size = mem->size - offset;

  sub = (GstMemory *) g_slice_new (GstEGLImageMemory);

  /* 共享内存应该是只读 */
  gst_memory_init (GST_MEMORY_CAST (sub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, mem->allocator, parent,
      mem->maxsize, mem->align, mem->offset + offset, size);

  return sub;
}


/* 声明一个EGLImage内存分配器对象 */
G_DECLARE_FINAL_TYPE (GstEGLImageAllocator, gst_egl_image_allocator, GST, EGL_IMAGE_ALLOCATOR, GstAllocator);

struct _GstEGLImageAllocator {
  GstAllocator parent;
};

/* 定义一个EGLImage内存分配器对象 */
G_DEFINE_TYPE (GstEGLImageAllocator, gst_egl_image_allocator, GST_TYPE_ALLOCATOR);


static void
gst_egl_image_allocator_class_init (GstEGLImageAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_egl_image_allocator_alloc_vfunc; /* 空函数 */
  allocator_class->free = gst_egl_image_allocator_free_vfunc;
}


static void
gst_egl_image_allocator_init (GstEGLImageAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_EGL_IMAGE_MEMORY_TYPE;
  alloc->mem_map = gst_egl_image_mem_map; /* 不能映射 */
  alloc->mem_unmap = gst_egl_image_mem_unmap;
  alloc->mem_share = gst_egl_image_mem_share;
  alloc->mem_copy = gst_egl_image_mem_copy; /* 不能拷贝 */
  alloc->mem_is_span = gst_egl_image_mem_is_span;

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

/*************************************GstEGLImageAllocator END********************************************* */


/**********************创建 GstEGLImageMemory 和 GstEGLImageAllocator 函数********************************** */
GstMemory *
gst_egl_image_allocator_alloc (GstAllocator * allocator,
                               GstEGLDisplay * display, 
                               GstVideoGLTextureType type, 
                               gint width,
                               gint height, 
                               gsize * size) {
  return NULL;
}

/**
 * @brief： 真正创建GstEGLImageMemory对象的函数
 */
GstMemory *
gst_egl_image_allocator_wrap (GstAllocator * allocator,
                              GstEGLDisplay * display, 
                              EGLImageKHR image, 
                              GstVideoGLTextureType type,
                              GstMemoryFlags flags, 
                              gsize size, 
                              gpointer user_data,
                              GDestroyNotify user_data_destroy) {
  
  GstEGLImageMemory *mem;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (image != EGL_NO_IMAGE_KHR, NULL);

  if (!allocator) {
    allocator = gst_egl_image_allocator_new ();
  }

  mem = g_slice_new (GstEGLImageMemory);
  gst_memory_init (GST_MEMORY_CAST (mem), flags,
      allocator, NULL, size, 0, 0, size);

  mem->display = gst_egl_display_ref (display);
  mem->image = image;
  mem->type = type;
  mem->orientation = GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;

  mem->user_data = user_data;
  mem->user_data_destroy = user_data_destroy;

  return GST_MEMORY_CAST (mem);
}


/**
 * @brief: 创建GstEGLImageAllocator内存分配器对象函数，但是不支持内存 map、copy
 */
GstAllocator *
gst_egl_image_allocator_new (void) {

  GstAllocator *allocator;

  allocator = g_object_new (gst_egl_image_allocator_get_type (), NULL);
  return GST_ALLOCATOR (g_object_ref (allocator));
}

/*********************创建 GstEGLImageMemory 和 GstEGLImageAllocator END******************************* */



/**************************************GstEGLDisplay相关********************************************** */
GstContext *
gst_context_new_egl_display (GstEGLDisplay * display, gboolean persistent)
{
  GstContext *context;
  GstStructure *s;

  context = gst_context_new (GST_EGL_DISPLAY_CONTEXT_TYPE, persistent);
  s = gst_context_writable_structure (context);
  gst_structure_set (s, "display", GST_TYPE_EGL_DISPLAY, display, NULL);

  return context;
}

gboolean
gst_context_get_egl_display (GstContext * context, GstEGLDisplay ** display)
{
  const GstStructure *s;

  g_return_val_if_fail (GST_IS_CONTEXT (context), FALSE);
  g_return_val_if_fail (strcmp (gst_context_get_context_type (context),
          GST_EGL_DISPLAY_CONTEXT_TYPE) == 0, FALSE);

  s = gst_context_get_structure (context);
  return gst_structure_get (s, "display", GST_TYPE_EGL_DISPLAY, display, NULL);
}

GstEGLDisplay *
gst_egl_display_new (EGLDisplay display, GDestroyNotify destroy_notify)
{
  GstEGLDisplay *gdisplay;

  gdisplay = g_slice_new (GstEGLDisplay);
  gdisplay->display = display;
  gdisplay->refcount = 1;
  gdisplay->destroy_notify = destroy_notify;

  gdisplay->eglCreateImage =
      (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress ("eglCreateImageKHR");
  gdisplay->eglDestroyImage =
      (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress ("eglDestroyImageKHR");

  return gdisplay;
}

GstEGLDisplay *
gst_egl_display_ref (GstEGLDisplay * display)
{
  g_return_val_if_fail (display != NULL, NULL);

  g_atomic_int_inc (&display->refcount);

  return display;
}

void
gst_egl_display_unref (GstEGLDisplay * display)
{
  g_return_if_fail (display != NULL);

  if (g_atomic_int_dec_and_test (&display->refcount)) {
    if (display->destroy_notify)
      display->destroy_notify (display->display);
    g_slice_free (GstEGLDisplay, display);
  }
}

EGLDisplay
gst_egl_display_get (GstEGLDisplay * display)
{
  g_return_val_if_fail (display != NULL, EGL_NO_DISPLAY);

  return display->display;
}


EGLImageKHR
gst_egl_display_image_create (GstEGLDisplay * display,
                              EGLContext ctx,
                              EGLenum target, 
                              EGLClientBuffer buffer, 
                              const EGLint * attrib_list) {
                              
  if (!display->eglCreateImage)
    return EGL_NO_IMAGE_KHR;

  return display->eglCreateImage (gst_egl_display_get (display), ctx, target,
      buffer, attrib_list);
}

G_DEFINE_BOXED_TYPE (GstEGLDisplay, gst_egl_display,
    (GBoxedCopyFunc) gst_egl_display_ref,
    (GBoxedFreeFunc) gst_egl_display_unref);
