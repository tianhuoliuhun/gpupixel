/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include "gpupixel/gpupixel_define.h"
#include "gpupixel/source/source.h"

namespace gpupixel {

class GPUPixelFramebuffer;

/**
 * 本地扩展（fork）：SourceTexture —— 直接吃**外部纹理**（共享 EGLContext 下由外部
 * 渲染器创建/写入），把它推入滤镜链，免去每帧 `glTexImage2D` 像素上传。
 *
 * 与 @c SourceRawData 的对比（这就是本类存在的唯一理由）：
 *
 * | | SourceRawData | SourceTexture |
 * |---|---|---|
 * | 输入载体 | `byte[]`（CPU 内存） | 外部 texture id（GPU 上，零拷贝） |
 * | 每帧成本 | JNI 取数组 + 上传 8.29 MB（1080p） | ≈ 0（只包一个 framebuffer 对象） |
 *
 * ## 前置条件
 * 外部渲染器必须与 GPUPixel **共享 EGLContext**（见 `GPUPixelContext::SetSharedEglContext`），
 * 否则 GPUPixel 看不到该 texture id。
 *
 * ## 生命周期约束（调用方负责）
 * 外部纹理在 GPUPixel 采样期间**不能被改写**。调用方需：
 *  - 用**多缓冲**轮转输入帧（避免下一帧覆盖正在被读的那张）
 *  - 采样前用 fence 同步（等外部"写入完成"的信号）
 */
class GPUPIXEL_API SourceTexture : public Source {
 public:
  static std::shared_ptr<SourceTexture> Create();
  virtual ~SourceTexture();

  /**
   * 设置外部输入纹理并**推动一次处理**（把输入交给下游滤镜链）。
   *
   * ⚠️ 必须在 GPUPixel 的 context 线程语义下调用（内部会 `SyncRunWithContext`），
   *    与 `SourceRawData::ProcessData` 的调用约定一致。
   *
   * @param texture 外部纹理 id（共享 context 下由外部渲染器创建）
   * @param width/height 纹理尺寸
   * @return 是否成功（texture=0 / 尺寸非法时返回 false）
   */
  bool PushTexture(uint32_t texture, int width, int height);

  /** 最近一次推入的纹理 id（诊断） */
  uint32_t GetInputTexture() const { return input_texture_; }
  int GetInputWidth() const { return input_width_; }
  int GetInputHeight() const { return input_height_; }
  /** 推入次数（单调递增） */
  uint64_t GetPushSerial() const {
    return push_serial_.load(std::memory_order_acquire);
  }

 private:
  SourceTexture();

  /** 当前包装的 framebuffer（只持有外部 texture id，不拥有它） */
  std::shared_ptr<GPUPixelFramebuffer> wrapping_;
  uint32_t input_texture_ = 0;
  int input_width_ = 0;
  int input_height_ = 0;
  std::atomic<uint64_t> push_serial_{0};
};

}  // namespace gpupixel
