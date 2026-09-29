/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#pragma once

#include "core/gpupixel_gl_include.h"

#include <memory>
#include <vector>

namespace gpupixel {
typedef struct GPUPIXEL_API {
  GLenum minFilter;
  GLenum magFilter;
  GLenum wrapS;
  GLenum wrapT;
  GLenum internalFormat;
  GLenum format;
  GLenum type;
} TextureAttributes;

class GPUPIXEL_API GPUPixelFramebuffer {
 public:
  GPUPixelFramebuffer(
      int width,
      int height,
      bool only_generate_texture = false,
      const TextureAttributes texture_attributes = default_texture_attributes);
  ~GPUPixelFramebuffer();

  uint32_t GetTexture() const { return texture_; }

  uint32_t GetFramebuffer() const { return framebuffer_; }

  /**
   * 本地扩展（fork）：包装一个**外部**纹理（共享 EGLContext 下由外部渲染器创建/写入）。
   *
   * 用途：让 GPUPixel 的滤镜链直接采样外部渲染结果，**免去每帧
   * `glTexImage2D` 上传**（1080p 约 8.29 MB/帧）。
   *
   * 语义差异（与普通 framebuffer 相比）：
   *  - 不创建自己的纹理/FBO，只持有外部 texture id 与尺寸
   *  - 仅可作为**采样源**（下游 filter 用 `GetTexture()`），不可 `Activate()` 作为渲染目标
   *  - 析构时**不会**删除该纹理（所有权归外部）
   */
  static std::shared_ptr<GPUPixelFramebuffer> CreateFromExistingTexture(
      uint32_t texture,
      int width,
      int height);

  /** 是否为「包装外部纹理」模式（诊断用） */
  bool IsWrappedTexture() const { return is_wrapped_texture_; }

  int GetWidth() const { return width_; }
  int GetHeight() const { return height_; }
  const TextureAttributes& GetTextureAttributes() const {
    return texture_attributes_;
  };
  bool HasFramebuffer() { return has_framebuffer_; };

  void Activate();
  void Deactivate();

  static TextureAttributes default_texture_attributes;

 private:
  int width_;
  int height_;
  TextureAttributes texture_attributes_;
  bool has_framebuffer_;
  uint32_t texture_;
  uint32_t framebuffer_;
  /** 本地扩展（fork）：true = 包装外部纹理（析构不删它） */
  bool is_wrapped_texture_ = false;

  /** 包装模式构造：不创建纹理/FBO */
  GPUPixelFramebuffer(uint32_t existing_texture, int width, int height);

  void GenerateTexture();
  void GenerateFramebuffer();

  //    static std::vector<std::shared_ptr<GPUPixelFramebuffer>> framebuffers_;
};

}  // namespace gpupixel
