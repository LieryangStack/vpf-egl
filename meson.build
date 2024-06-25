project('vpf_egl_gles_sink', 'c',
        version: '1.0',
        default_options:[
          'buildtype=debugoptimized',
          'warning_level=1',
          'c_std=gnu99'],
        meson_version: '>= 0.60.0')

# 创建 configuration_data ()
conf_data = configuration_data()

#define  ""

#define  ""

# set_quoted 会把字符串两边加双引号，value只能是字符串类型
conf_data.set_quoted('GST_LICENSE', 'LGPL')
conf_data.set_quoted('PACKAGE', 'gst-template-plugin')
conf_data.set_quoted('PACKAGE_VERSION', '1.22.6')
conf_data.set_quoted('GST_PACKAGE_NAME', 'GStreamer Nvidia Bad Plug-ins source release')
conf_data.set_quoted('GST_PACKAGE_ORIGIN', 'Unknown package origin')
conf_data.set_quoted('GST_API_VERSION', '1.0')
conf_data.set_quoted('VERSION', '1.2.3')

host = host_machine.cpu_family()

if host == 'x86_64'
  conf_data.set('IS_DESKTOP', 1, description: '如果是PC端运行，该值就会被定义，该值跟使用GPU CUDA有关')
endif


# 如果是 IOS 系统，就需要定义 HAVE_IOS
# 如果是 树莓派 RPI, 定义 USE_EGL_RPI
# 如果是 使用Wayland视窗系统， 定义 USE_EGL_WAYLAND
conf_data.set('USE_EGL_X11', 1, description: '是否使用的是egl x11窗口')

configure_file(output: 'config.h',
               configuration: conf_data)

# 头文件路径，头文件路径会根据传入参数返回 1. 基于源代码根目录的 2. 基于构建根目录的
incs = include_directories('.',
                               'ext/eglgles', 
                               'gst-libs', 
                               'gst-libs/gst/egl',
                               '/opt/nvidia/deepstream/deepstream/sources/includes')

# 需要编译的源文件
c_sources = files (
  'ext/eglgles/gstegladaptation.c',
	'ext/eglgles/gstegladaptation_egl.c',
	'ext/eglgles/gsteglglessink.c',
	'ext/eglgles/gstegljitter.c',
	'ext/eglgles/video_platform_wrapper.c',
	'gst-libs/gst/egl/egl.c')

ds_library_path = '/opt/nvidia/deepstream/deepstream/lib'

# modules: ['cudart', 'cuda'] 就是 libcudart.so libcuda.so
cuda_dep = dependency('cuda', modules: ['cudart', 'cuda'], required: true)
glib_dep = dependency('glib-2.0', required: true)
gstreamer_dep = dependency('gstreamer-1.0', required: true)
gstreamer_base_dep = dependency('gstreamer-base-1.0', required: true)
gstreamer_video_dep = dependency('gstreamer-video-1.0', required: true)
x11_dep = dependency('x11', required: true)


cc = meson.get_compiler('c')

egl_dep = cc.find_library ('EGL')
gles_dep = cc.find_library ('GLESv2')
libm_dep = cc.find_library('m') # 数学库
nvbufsurface_dep = cc.find_library('nvbufsurface', dirs: ds_library_path)

deps = [cuda_dep, glib_dep, gstreamer_dep, gstreamer_base_dep, gstreamer_video_dep, x11_dep,
        egl_dep, gles_dep, libm_dep, nvbufsurface_dep]

shared_library ('vpfeglglessink',
  sources: c_sources,
  include_directories: incs,
  dependencies: deps,
  c_args: ['-Wl,--no-undefined'])