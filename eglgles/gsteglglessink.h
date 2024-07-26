#ifndef __GST_VPFEGLGLESSINK_H__
#define __GST_VPFEGLGLESSINK_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/base/gstdataqueue.h>

#include <cuda.h>
#include <cudaGL.h>
#include <cuda_runtime.h>

#include "gtkgstpaintable.h"
#include "gstegladaptation.h"
#include "gstegljitter.h"

G_BEGIN_DECLS

#define GST_TYPE_VPF_EGLGLESSINK gst_vpf_eglglessink_get_type()
G_DECLARE_FINAL_TYPE (GstVpfEglGlesSink, gst_vpf_eglglessink, GST, VPF_EGLGLESSINK, GstVideoSink);

struct _GstVpfEglGlesSink 
{
  GstVideoSink videosink;       /* Element hook */

  GstVideoInfo configured_info; /* 存储视频信息（比如格式、宽、高） */

  GstCaps *sinkcaps; /* 该sink支持的pad */
  GstCaps *current_caps, *configured_caps; /* 当前sink使用（被配置的pad） */

  GstEglAdaptationContext *egl_context;
  guint profile; /* 视频帧抖动信息（平均帧率） */

  gboolean egl_started; /* egl成功在线程中初始化 */
  gboolean is_closing; /* egl是否关闭flag */
  gboolean using_cuda; /* GPU CUDA */
  gboolean using_nvbufsurf; /* Jetson */

  GThread *thread;
  gboolean thread_running;
  GstDataQueue *queue; /* 需要处理数据的队列 */
  GCond render_exit_cond; 
  GCond render_cond;
  GMutex render_lock;
  GstFlowReturn last_flow;
  GstMiniObject *dequeued_object;  /* 当前那个元素出队（从@queue），也就是该元素被处理了 */

  EGLNativeDisplayType display; /* X11得到的那个显示， typedef Display *EGLNativeDisplayType; */

  GstEglJitterTool *pDeliveryJitter;

  GstBuffer *last_uploaded_buffer; /* 最近一次更新的buffer（上传纹理成功后会更新） */
  CUcontext cuContext; /* CDUA 上下文 */
  CUgraphicsResource cuResource[3]; /* CUDA资源 */
  unsigned int gpu_id;

  gboolean nvbuf_api_version_new;

  GdkPaintable *paintable;
};


extern GdkTexture *dmabuf_texture;

G_END_DECLS
#endif /* __GST_EGLGLESSINK_H__ */
