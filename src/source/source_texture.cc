/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include "gpupixel/source/source_texture.h"

#include "core/gpupixel_context.h"
#include "core/gpupixel_framebuffer.h"
#include "utils/logging.h"

namespace gpupixel {

std::shared_ptr<SourceTexture> SourceTexture::Create() {
  // 与 SourceRawData 不同，本类不需要自己的 shader program（不做像素上传/绘制），
  // 因此无需在 GPUPixel context 里做初始化 —— 构造即可用。
  return std::shared_ptr<SourceTexture>(new SourceTexture());
}

SourceTexture::SourceTexture() {}

SourceTexture::~SourceTexture() {
  // wrapping_ 由 shared_ptr 释放；它是「包装外部纹理」模式，析构时不会删外部纹理
  wrapping_ = nullptr;
}

bool SourceTexture::PushTexture(uint32_t texture, int width, int height) {
  if (texture == 0 || width <= 0 || height <= 0) {
    return false;
  }

  bool ok = false;
  GPUPixelContext::GetInstance()->SyncRunWithContext([&] {
    // 包装外部纹理（轻量对象：只存 id + 尺寸，不建纹理/FBO），同尺寸同 id 时复用
    if (!wrapping_ || input_texture_ != texture || input_width_ != width ||
        input_height_ != height) {
      wrapping_ =
          GPUPixelFramebuffer::CreateFromExistingTexture(texture, width, height);
      if (!wrapping_) {
        LOG_WARN("SourceTexture: failed to wrap texture {}", texture);
        return;
      }
      input_texture_ = texture;
      input_width_ = width;
      input_height_ = height;
    }

    // 把输入设为当前 framebuffer 并推给下游滤镜链。
    // 这一步就是 SourceRawData 里「上传后绘制」的等价物 —— 只是这里**没有上传**，
    // 下游直接采样外部纹理。
    SetFramebuffer(wrapping_, NoRotation);
    DoRender(true);
    push_serial_.fetch_add(1, std::memory_order_release);
    ok = true;
  });
  return ok;
}

}  // namespace gpupixel
