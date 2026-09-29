/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#pragma once

#include <mutex>
#include "core/gpupixel_framebuffer_factory.h"
#include "gpupixel/filter/filter.h"
#include "gpupixel/gpupixel_define.h"

#include "core/gpupixel_gl_include.h"
#include "core/gpupixel_program.h"

class DispatchQueue;

namespace gpupixel {

class GPUPIXEL_API GPUPixelContext {
 public:
  static GPUPixelContext* GetInstance();
  static void Destroy();

  FramebufferFactory* GetFramebufferFactory() const;
  void SetActiveGlProgram(GPUPixelGLProgram* shaderProgram);
  void Clean();

  void SyncRunWithContext(std::function<void(void)> func);
  void UseAsCurrent(void);
  void PresentBufferForDisplay();

#if defined(GPUPIXEL_IOS)
  EAGLContext* GetEglContext() const { return egl_context_; };
#elif defined(GPUPIXEL_MAC)
  NSOpenGLContext* GetOpenGLContext() const {
    return image_processing_context_;
  };
#elif defined(GPUPIXEL_ANDROID)
  EGLContext GetEglContext() const { return egl_context_; };
  EGLDisplay GetEglDisplay() const { return egl_display_; };
  EGLSurface GetEglSurface() const { return egl_surface_; };
  EGLConfig GetEglConfig() const { return egl_config_; };

  // ===== 本地扩展（fork）：与外部分享 EGLContext =====
  /**
   * 注入外部 (display, config, share_context)。
   *
   * 作用：让 GPUPixel 自己的 EGLContext 以外部 context 为 share_context 创建，
   * 从而与外部渲染器**共享 texture / FBO / sync** —— 免去每帧
   * 「回读成字节 → 再上传」的跨界搬运（1080p 下约 5 × 8.29 MB/帧）。
   *
   * 用法（Android）：在**已有 current context 的线程**（如 GLSurfaceView 的渲染线程）
   * 调用 `GPUPixelContext::CaptureCurrentEglContextAsShare()` 捕获，或直接传句柄。
   *
   * ⚠️ 必须在 GPUPixelContext **懒创建之前**调用（即任何会触发 GPU 操作的
   *    GPUPixel API 之前），否则本次注入无效。
   * ⚠️ 传入的 config 需支持 EGL_PBUFFER_BIT（GPUPixel 用它创建 1×1 pbuffer 作为
   *    makeCurrent 的 surface）；不满足时会自动回退为「不共享」的私有 context。
   */
  static void SetSharedEglContext(EGLDisplay display,
                                  EGLConfig config,
                                  EGLContext share_context);

  /**
   * 捕获**当前线程**的 EGL context 作为共享源（须在有 current context 的线程调用）。
   * 内部用 eglGetCurrentContext / eglGetCurrentDisplay + 按 EGL_CONFIG_ID 反查 config。
   *
   * @return 是否捕获成功
   */
  static bool CaptureCurrentEglContextAsShare();

  /** 是否已注入共享 context（诊断用） */
  static bool HasSharedEglContext();

  /** 本次是否真的以共享方式建立了 context（诊断用） */
  bool IsContextShared() const { return context_is_shared_; }
#elif defined(GPUPIXEL_WIN) || defined(GPUPIXEL_LINUX)
  GLFWwindow* GetGLContext() const { return gl_context_; };
#endif

 private:
  GPUPixelContext();
  ~GPUPixelContext();

  void Init();

  void CreateContext();
  void ReleaseContext();

 private:
  static GPUPixelContext* instance_;
  static std::mutex mutex_;
  FramebufferFactory* framebuffer_factory_;
  GPUPixelGLProgram* current_shader_program_;
  std::shared_ptr<DispatchQueue> task_queue_;

#if defined(GPUPIXEL_IOS)
  EAGLContext* egl_context_;
#elif defined(GPUPIXEL_MAC)
  NSOpenGLPixelFormat* pixel_format_;
  NSOpenGLContext* image_processing_context_;
#elif defined(GPUPIXEL_ANDROID)
  EGLDisplay egl_display_;
  EGLConfig egl_config_;
  EGLSurface egl_surface_;
  EGLContext egl_context_;

  // ===== 本地扩展（fork）：共享 context 的注入槽位（静态：需在实例创建前生效）=====
  static EGLDisplay shared_display_;
  static EGLConfig shared_config_;
  static EGLContext shared_context_;
  /** 本次是否以共享方式建立 context（失败会自动回退到私有 context） */
  bool context_is_shared_ = false;
  /** 用给定 display/config/share_context 建立 context + 1×1 pbuffer；失败返回 false */
  bool InitContextWith(EGLDisplay display,
                       EGLConfig config,
                       EGLContext share_context);
#elif defined(GPUPIXEL_WIN) || defined(GPUPIXEL_LINUX)
  GLFWwindow* gl_context_;
#elif defined(GPUPIXEL_WASM)
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE wasm_context_;
#endif
};

}  // namespace gpupixel
