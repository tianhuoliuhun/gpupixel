/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include "core/gpupixel_context.h"
#include "utils/dispatch_queue.h"
#include "utils/logging.h"
#include "utils/util.h"
#include <vector>
#if defined(GPUPIXEL_WASM)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace gpupixel {

GPUPixelContext* GPUPixelContext::instance_ = 0;
std::mutex GPUPixelContext::mutex_;

// ===== 本地扩展（fork）：共享 EGLContext 的注入槽位 =====
#if defined(GPUPIXEL_ANDROID)
EGLDisplay GPUPixelContext::shared_display_ = EGL_NO_DISPLAY;
EGLConfig GPUPixelContext::shared_config_ = nullptr;
EGLContext GPUPixelContext::shared_context_ = EGL_NO_CONTEXT;

void GPUPixelContext::SetSharedEglContext(EGLDisplay display,
                                          EGLConfig config,
                                          EGLContext share_context) {
  shared_display_ = display;
  shared_config_ = config;
  shared_context_ = share_context;
  LOG_INFO("GPUPixelContext: external EGL context registered for sharing");
}

bool GPUPixelContext::CaptureCurrentEglContextAsShare() {
  EGLDisplay display = eglGetCurrentDisplay();
  EGLContext context = eglGetCurrentContext();
  if (display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
    LOG_WARN("CaptureCurrentEglContextAsShare: no current EGL context on this thread");
    return false;
  }

  // 按 EGL_CONFIG_ID 反查当前 context 对应的 EGLConfig
  EGLint config_id = 0;
  if (!eglQueryContext(display, context, EGL_CONFIG_ID, &config_id)) {
    LOG_WARN("CaptureCurrentEglContextAsShare: eglQueryContext failed");
    return false;
  }
  EGLint num_configs = 0;
  eglGetConfigs(display, nullptr, 0, &num_configs);
  if (num_configs <= 0) {
    LOG_WARN("CaptureCurrentEglContextAsShare: no EGL configs");
    return false;
  }
  std::vector<EGLConfig> configs(static_cast<size_t>(num_configs));
  EGLint returned = 0;
  eglGetConfigs(display, configs.data(), num_configs, &returned);

  EGLConfig matched = nullptr;
  for (EGLint i = 0; i < returned; ++i) {
    EGLint id = 0;
    eglGetConfigAttrib(display, configs[i], EGL_CONFIG_ID, &id);
    if (id == config_id) {
      matched = configs[i];
      break;
    }
  }
  if (matched == nullptr) {
    LOG_WARN("CaptureCurrentEglContextAsShare: config id {} not found", config_id);
    return false;
  }

  SetSharedEglContext(display, matched, context);
  LOG_INFO("GPUPixelContext: captured current context as share source (configId={})",
           config_id);
  return true;
}

bool GPUPixelContext::HasSharedEglContext() {
  return shared_context_ != EGL_NO_CONTEXT && shared_display_ != EGL_NO_DISPLAY &&
         shared_config_ != nullptr;
}
#endif  // GPUPIXEL_ANDROID

GPUPixelContext::GPUPixelContext() : current_shader_program_(0) {
  LOG_DEBUG("Creating GPUPixelContext");
#if !defined(GPUPIXEL_WASM)
  task_queue_ = std::make_shared<DispatchQueue>();
#endif
  framebuffer_factory_ = new FramebufferFactory();
  Init();
}

GPUPixelContext::~GPUPixelContext() {
  LOG_DEBUG("Destroying GPUPixelContext");
  ReleaseContext();
  delete framebuffer_factory_;
  task_queue_->stop();
}

GPUPixelContext* GPUPixelContext::GetInstance() {
  if (!instance_) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!instance_) {
      instance_ = new (std::nothrow) GPUPixelContext;
    }
  }
  return instance_;
};

void GPUPixelContext::Destroy() {
  if (instance_) {
    delete instance_;
    instance_ = 0;
  }
}

void GPUPixelContext::Init() {
  SyncRunWithContext([=] {
    LOG_INFO("Initializing GPUPixelContext");
    this->CreateContext();
  });
}

FramebufferFactory* GPUPixelContext::GetFramebufferFactory() const {
  return framebuffer_factory_;
}

void GPUPixelContext::SetActiveGlProgram(GPUPixelGLProgram* shaderProgram) {
  if (current_shader_program_ != shaderProgram) {
    current_shader_program_ = shaderProgram;
    shaderProgram->UseProgram();
  }
}

void GPUPixelContext::Clean() {
  LOG_DEBUG("Cleaning GPUPixelContext resources");
  framebuffer_factory_->Clean();
}

void GPUPixelContext::CreateContext() {
#if defined(GPUPIXEL_IOS)
  LOG_DEBUG("Creating iOS OpenGL ES 2.0 context");
  egl_context_ = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
  if (!egl_context_) {
    LOG_ERROR("Failed to create iOS OpenGL ES 2.0 context");
    return;
  }
  [EAGLContext setCurrentContext:egl_context_];
  LOG_INFO("iOS OpenGL ES 2.0 context created successfully");
#elif defined(GPUPIXEL_MAC)
  LOG_DEBUG("Creating macOS OpenGL context");
  NSOpenGLPixelFormatAttribute pixelFormatAttributes[] = {
      NSOpenGLPFADoubleBuffer,
      NSOpenGLPFAOpenGLProfile,
      NSOpenGLProfileVersionLegacy,
      NSOpenGLPFAAccelerated,
      0,
      NSOpenGLPFAColorSize,
      24,
      NSOpenGLPFAAlphaSize,
      8,
      NSOpenGLPFADepthSize,
      24,
      0};

  pixel_format_ =
      [[NSOpenGLPixelFormat alloc] initWithAttributes:pixelFormatAttributes];
  if (!pixel_format_) {
    LOG_ERROR("Failed to create NSOpenGLPixelFormat");
    return;
  }

  image_processing_context_ =
      [[NSOpenGLContext alloc] initWithFormat:pixel_format_ shareContext:nil];
  if (!image_processing_context_) {
    LOG_ERROR("Failed to create NSOpenGLContext");
    return;
  }

  GLint interval = 0;
  [image_processing_context_ makeCurrentContext];
  [image_processing_context_ setValues:&interval
                          forParameter:NSOpenGLContextParameterSwapInterval];
  LOG_INFO("macOS OpenGL context created successfully");
#elif defined(GPUPIXEL_ANDROID)
  LOG_DEBUG("Creating Android EGL context");

  // ===== 本地扩展（fork）：优先尝试与外部分享 context =====
  // 共享后 texture / FBO / sync 与外部渲染器互通，可彻底免去每帧回读→上传的
  // 跨界搬运。失败（外部 config 不支持 pbuffer、驱动拒绝共享等）则自动回退，
  // 保证与上游行为完全一致。
  if (HasSharedEglContext()) {
    if (InitContextWith(shared_display_, shared_config_, shared_context_)) {
      context_is_shared_ = true;
      LOG_INFO("Android EGL context created WITH SHARING (external context)");
      return;
    }
    LOG_WARN("Shared EGL context init failed -> fallback to private context");
  }

  // Initialize EGL
  egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (egl_display_ == EGL_NO_DISPLAY) {
    LOG_ERROR("Failed to get EGL display");
    return;
  }

  EGLint major, minor;
  if (!eglInitialize(egl_display_, &major, &minor)) {
    LOG_ERROR("Failed to initialize EGL");
    return;
  }
  LOG_DEBUG("EGL initialized: version major:{} minor:{}", major, minor);

  // Configure EGL - support both PBUFFER (off-screen) and WINDOW (window) Surface
  // This allows it to be used for both off-screen rendering (filter processing) and window rendering (display to screen)
  const EGLint configAttribs[] = {
      EGL_RED_SIZE, 8,           // 8 bits for red channel
      EGL_GREEN_SIZE, 8,         // 8 bits for green channel
      EGL_BLUE_SIZE, 8,          // 8 bits for blue channel
      EGL_ALPHA_SIZE, 8,         // 8 bits for alpha channel
      EGL_DEPTH_SIZE, 16,        // 16 bits for depth buffer
      EGL_STENCIL_SIZE, 0,       // 0 bits for stencil buffer (not used)
      EGL_SURFACE_TYPE,
      EGL_PBUFFER_BIT | EGL_WINDOW_BIT,  // Key: support both off-screen and window Surface
      EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES2_BIT,        // Use OpenGL ES 2.0
      EGL_NONE};

  EGLint numConfigs;
  if (!eglChooseConfig(egl_display_, configAttribs, &egl_config_, 1,
                       &numConfigs)) {
    LOG_ERROR("Failed to choose EGL config");
    return;
  }

  // Create EGL context
  const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

  egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT,
                                  contextAttribs);
  if (egl_context_ == EGL_NO_CONTEXT) {
    LOG_ERROR("Failed to create EGL context");
    return;
  }

  // Create offscreen rendering surface
  const EGLint pbufferAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

  egl_surface_ =
      eglCreatePbufferSurface(egl_display_, egl_config_, pbufferAttribs);
  if (egl_surface_ == EGL_NO_SURFACE) {
    LOG_ERROR("Failed to create EGL surface");
    return;
  }

  // Set current context
  if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
    LOG_ERROR("Failed to make EGL context current");
    return;
  }
  LOG_INFO("Android EGL context created successfully");
#elif defined(GPUPIXEL_WIN) || defined(GPUPIXEL_LINUX)
  LOG_DEBUG("Creating Windows/Linux OpenGL context");
  int ret = glfwInit();

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

  if (ret) {
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  } else {
    LOG_ERROR("Failed to initialize GLFW");
    return;
  }
  gl_context_ = glfwCreateWindow(1, 1, "gpupixel opengl context", NULL, NULL);
  if (!gl_context_) {
    LOG_ERROR("Failed to create GLFW window");
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(gl_context_);

  if (!gladLoadGL()) {
    LOG_ERROR("Failed to initialize GLAD");
    return;
  }
  LOG_INFO("Windows/Linux OpenGL context created successfully");
#elif defined(GPUPIXEL_WASM)
  LOG_DEBUG("Creating WebGL context");
  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.majorVersion = 2;  // Use WebGL 2.0
  attrs.minorVersion = 0;
  wasm_context_ = emscripten_webgl_create_context("#gpupixel_canvas", &attrs);
  if (wasm_context_ <= 0) {
    LOG_ERROR("Failed to create WebGL context: {}", wasm_context_);
    return;
  }
  emscripten_webgl_make_context_current(wasm_context_);
  LOG_INFO("WebGL context created successfully");
#endif
}

void GPUPixelContext::UseAsCurrent() {
#if defined(GPUPIXEL_IOS)
  if ([EAGLContext currentContext] != egl_context_) {
    LOG_TRACE("Setting current EAGLContext");
    [EAGLContext setCurrentContext:egl_context_];
  }
#elif defined(GPUPIXEL_MAC)
  if ([NSOpenGLContext currentContext] != image_processing_context_) {
    LOG_TRACE("Setting current NSOpenGLContext");
    [image_processing_context_ makeCurrentContext];
  }
#elif defined(GPUPIXEL_ANDROID)
  if (eglGetCurrentContext() != egl_context_) {
    LOG_TRACE("Setting current EGL context");
    eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
  }
#elif defined(GPUPIXEL_WIN) || defined(GPUPIXEL_LINUX)
  if (glfwGetCurrentContext() != gl_context_) {
    LOG_TRACE("Setting current GLFW context");
    glfwMakeContextCurrent(gl_context_);
  }
#elif defined(GPUPIXEL_WASM)
  LOG_TRACE("Setting current WebGL context");
  emscripten_webgl_make_context_current(wasm_context_);
#endif
}

void GPUPixelContext::PresentBufferForDisplay() {
#if defined(GPUPIXEL_IOS)
  [egl_context_ presentRenderbuffer:GL_RENDERBUFFER];
#elif defined(GPUPIXEL_MAC)
  // No implementation needed
#elif defined(GPUPIXEL_ANDROID)
  // For offscreen rendering, no need to swap buffers
  // If display to screen is needed, use eglSwapBuffers(egl_display_,
  // egl_surface_);
#endif
}

#if defined(GPUPIXEL_ANDROID)
bool GPUPixelContext::InitContextWith(EGLDisplay display,
                                      EGLConfig config,
                                      EGLContext share_context) {
  if (display == EGL_NO_DISPLAY || config == nullptr) {
    return false;
  }

  EGLint major = 0, minor = 0;
  if (!eglInitialize(display, &major, &minor)) {
    LOG_ERROR("InitContextWith: eglInitialize failed");
    return false;
  }

  // ⚠️ 共享模式下 priority 用 ES3：实测把 ES2 context 与外部 ES3 context 共享，
  //    部分驱动会直接拒绝；ES3 向后兼容 ESSL 100，GPUPixel 的 shader 无需改动。
  EGLContext ctx = EGL_NO_CONTEXT;
  const EGLint ctx3[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  ctx = eglCreateContext(display, config, share_context, ctx3);
  if (ctx == EGL_NO_CONTEXT) {
    const EGLint ctx2[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    ctx = eglCreateContext(display, config, share_context, ctx2);
  }
  if (ctx == EGL_NO_CONTEXT) {
    LOG_ERROR("InitContextWith: eglCreateContext(shared) failed, err={}",
              static_cast<int>(eglGetError()));
    return false;
  }

  // GPUPixel 需要一张 1×1 pbuffer 作为 makeCurrent 的 surface（离屏渲染无需 window）
  const EGLint pbuffer_attribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLSurface surface = eglCreatePbufferSurface(display, config, pbuffer_attribs);
  if (surface == EGL_NO_SURFACE) {
    LOG_WARN("InitContextWith: pbuffer failed (config lacks EGL_PBUFFER_BIT?), err={}",
             static_cast<int>(eglGetError()));
    eglDestroyContext(display, ctx);
    return false;
  }

  if (!eglMakeCurrent(display, surface, surface, ctx)) {
    LOG_ERROR("InitContextWith: eglMakeCurrent failed, err={}",
              static_cast<int>(eglGetError()));
    eglDestroySurface(display, surface);
    eglDestroyContext(display, ctx);
    return false;
  }

  egl_display_ = display;
  egl_config_ = config;
  egl_surface_ = surface;
  egl_context_ = ctx;
  const char* gl_ver =
      reinterpret_cast<const char*>(glGetString(GL_VERSION));
  LOG_INFO("InitContextWith: shared context ready (GL_VERSION={})",
           gl_ver ? gl_ver : "unknown");
  return true;
}
#endif  // GPUPIXEL_ANDROID

void GPUPixelContext::ReleaseContext() {
  LOG_DEBUG("Releasing OpenGL context");
#if defined(GPUPIXEL_ANDROID)
  if (egl_display_ != EGL_NO_DISPLAY) {
    eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);

    if (egl_surface_ != EGL_NO_SURFACE) {
      LOG_TRACE("Destroying EGL surface");
      eglDestroySurface(egl_display_, egl_surface_);
      egl_surface_ = EGL_NO_SURFACE;
    }

    if (egl_context_ != EGL_NO_CONTEXT) {
      LOG_TRACE("Destroying EGL context");
      eglDestroyContext(egl_display_, egl_context_);
      egl_context_ = EGL_NO_CONTEXT;
    }

    // ⚠️ 共享模式下 EGLDisplay 归外部渲染器所有，绝不能 eglTerminate ——
    //    那会把主渲染的 display 一起销毁。这里只销毁本 context 自己的 surface/context。
    if (context_is_shared_) {
      LOG_INFO(
          "Skip eglTerminate: EGLDisplay is owned by the external renderer "
          "(shared mode)");
    } else {
      LOG_TRACE("Terminating EGL display");
      eglTerminate(egl_display_);
    }
    egl_display_ = EGL_NO_DISPLAY;
    context_is_shared_ = false;
  }
#elif defined(GPUPIXEL_WIN) || defined(GPUPIXEL_LINUX)
  if (gl_context_) {
    LOG_TRACE("Destroying GLFW window");
    glfwDestroyWindow(gl_context_);
  }
  LOG_TRACE("Terminating GLFW");
  glfwTerminate();
#elif defined(GPUPIXEL_WASM)
  LOG_TRACE("Destroying WebGL context");
  emscripten_webgl_destroy_context(wasm_context_);
#endif
  LOG_INFO("OpenGL context released successfully");
}

void GPUPixelContext::SyncRunWithContext(std::function<void(void)> task) {
#if defined(GPUPIXEL_IOS) || defined(GPUPIXEL_MAC)
  if (!Util::IsAppleAppActive()) {
    return;
  }
#endif

#if defined(GPUPIXEL_WASM)
  LOG_TRACE("Running task synchronously (WebGL)");
  UseAsCurrent();
  task();
#else
  LOG_TRACE("Running task on task queue");
  task_queue_->runTask([=]() {
    UseAsCurrent();
    task();
  });
#endif
}
}  // namespace gpupixel
