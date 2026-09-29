/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include "core/gpupixel_framebuffer.h"
#include <assert.h>
#include <algorithm>
#include "core/gpupixel_context.h"
#include "utils/util.h"

namespace gpupixel {

// std::vector<std::shared_ptr<GPUPixelFramebuffer>>
// GPUPixelFramebuffer::framebuffers_;
#ifndef GPUPIXEL_WIN
TextureAttributes GPUPixelFramebuffer::default_texture_attributes = {
    .minFilter = GL_LINEAR,
    .magFilter = GL_LINEAR,
    .wrapS = GL_CLAMP_TO_EDGE,
    .wrapT = GL_CLAMP_TO_EDGE,
    .internalFormat = GL_RGBA,
    .format = GL_RGBA,
    .type = GL_UNSIGNED_BYTE};
#else
TextureAttributes GPUPixelFramebuffer::default_texture_attributes = {
    GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
    GL_RGBA,   GL_RGBA,   GL_UNSIGNED_BYTE};
#endif
GPUPixelFramebuffer::GPUPixelFramebuffer(
    int width,
    int height,
    bool only_generate_texture /* = false*/,
    const TextureAttributes
        texture_attributes /* = default_texture_attributes*/)
    : texture_(-1), framebuffer_(-1) {
  width_ = width;
  height_ = height;
  texture_attributes_ = texture_attributes;
  has_framebuffer_ = !only_generate_texture;

  if (has_framebuffer_) {
    GenerateFramebuffer();
  } else {
    GenerateTexture();
  }
}

// 本地扩展（fork）：包装外部纹理 —— 不创建纹理/FBO，只持有 id 与尺寸。
// 成员初始化顺序必须与头文件中的声明顺序一致。
GPUPixelFramebuffer::GPUPixelFramebuffer(uint32_t existing_texture,
                                         int width,
                                         int height)
    : width_(width),
      height_(height),
      has_framebuffer_(false),
      texture_(existing_texture),
      framebuffer_(-1),
      is_wrapped_texture_(true) {
  texture_attributes_ = default_texture_attributes;
}

std::shared_ptr<GPUPixelFramebuffer>
GPUPixelFramebuffer::CreateFromExistingTexture(uint32_t texture,
                                               int width,
                                               int height) {
  if (texture == 0 || width <= 0 || height <= 0) {
    return nullptr;
  }
  return std::shared_ptr<GPUPixelFramebuffer>(
      new GPUPixelFramebuffer(texture, width, height));
}

GPUPixelFramebuffer::~GPUPixelFramebuffer() {
  gpupixel::GPUPixelContext::GetInstance()->SyncRunWithContext([&] {
    // ⚠️ 包装模式下纹理归**外部**所有，绝不能删 —— 否则会连带毁掉外部渲染器的画面
    bool should_delete_texture = (!is_wrapped_texture_ && texture_ != -1);
    bool should_delete_framebuffer = (framebuffer_ != -1);

    if (should_delete_texture) {
      GL_CALL(glDeleteTextures(1, &texture_));
      texture_ = -1;
    }
    if (should_delete_framebuffer) {
      GL_CALL(glDeleteFramebuffers(1, &framebuffer_));
      framebuffer_ = -1;
    }
  });
}

void GPUPixelFramebuffer::Activate() {
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_));
  GL_CALL(glViewport(0, 0, width_, height_));
}

void GPUPixelFramebuffer::Deactivate() {
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

void GPUPixelFramebuffer::GenerateTexture() {
  GL_CALL(glGenTextures(1, &texture_));
  GL_CALL(glBindTexture(GL_TEXTURE_2D, texture_));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                          texture_attributes_.minFilter));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                          texture_attributes_.magFilter));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                          texture_attributes_.wrapS));
  GL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                          texture_attributes_.wrapT));

  // TODO: Handle mipmaps
  GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
}

void GPUPixelFramebuffer::GenerateFramebuffer() {
  GL_CALL(glGenFramebuffers(1, &framebuffer_));
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_));
  GenerateTexture();
  GL_CALL(glBindTexture(GL_TEXTURE_2D, texture_));
  GL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, texture_attributes_.internalFormat,
                       width_, height_, 0, texture_attributes_.format,
                       texture_attributes_.type, 0));
  GL_CALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, texture_, 0));
  GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
  GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
}

}  // namespace gpupixel
