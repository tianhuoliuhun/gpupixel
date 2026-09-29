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
#include <mutex>

#include "gpupixel/gpupixel_define.h"
#include "gpupixel/sink/sink.h"

namespace gpupixel {

class GPUPixelFramebuffer;
class GPUPixelGLProgram;

/**
 * 本地扩展（fork）：SinkTexture —— 把处理结果**留在 GPU 上**并把 texture id 暴露出去，
 * 供**共享 EGLContext 的外部渲染器**直接采样。
 *
 * 与 @c SinkRawData 的对比（这就是本类存在的唯一理由）：
 *
 * | | SinkRawData | SinkTexture |
 * |---|---|---|
 * | 结果位置 | `glReadPixels` 回读到 CPU 内存 | 保留在 GPU texture 上 |
 * | 交给 Java | `NewByteArray` + `SetByteArrayRegion`（再拷一次） | 只回传一个 `int` |
 * | 1080p 每帧开销 | 回读 8.29 MB + JNI 拷贝 8.29 MB | ≈ 0（多一次 texture→texture 直通） |
 *
 * ## 双缓冲
 * 每次 @c Render 交替写入两块 framebuffer，故**外部正在采样的那张不会被下一次写入覆盖**
 * （最多落后 1 帧）。这比单缓冲安全，代价仅是多占一张全尺寸 texture。
 *
 * ## 消费端 fence（可选，但生产建议启用）
 * 外部采样完成后调用 @c SetConsumerFence 回传一个由**外部 context** 创建的 `GLsync`；
 * @c Render 会在下一次写入前等待它。用于「外部消费慢于生产」时进一步兜底
 * （例如外部渲染线程偶发卡顿）。传 0 表示不需要等待。
 *
 * @note 本类**不**做任何 CPU 回读，因此只在「GPUPixel 与外部共享 EGLContext」的前提下
 * 才有意义；未共享时外部拿不到这个 texture id 的内容。
 */
class GPUPIXEL_API SinkTexture : public Sink {
 public:
  static std::shared_ptr<SinkTexture> Create();
  virtual ~SinkTexture();

  void Render() override;
  void SetInputFramebuffer(std::shared_ptr<GPUPixelFramebuffer> framebuffer,
                           RotationMode rotation_mode = NoRotation,
                           int tex_idx = 0) override;

  /** 最近一次完成渲染的结果纹理；共享 context 下外部可直接采样。未渲染过返回 0 */
  uint32_t GetTextureId() const {
    return result_texture_.load(std::memory_order_acquire);
  }
  /** 结果序号（单调递增）—— 外部据此判断是否拿到了新结果 */
  uint64_t GetResultSerial() const {
    return result_serial_.load(std::memory_order_acquire);
  }
  int GetWidth() const { return width_; }
  int GetHeight() const { return height_; }

  /**
   * 外部消费完当前结果后回传的 fence（**由外部 context 创建**；内部转为 GLsync）。
   * Render() 会在下一次写入前等待并删除它。传 0 清除。
   */
  void SetConsumerFence(int64_t fence);

  /** fence 等待失败（重试后仍失败）的次数，诊断用 */
  int GetFenceWaitFailures() const {
    return fence_wait_failures_.load(std::memory_order_relaxed);
  }

 private:
  SinkTexture();

  bool Init();
  void EnsureFramebuffers(int width, int height);
  /** 等待并删除外部 fence；返回是否可继续写入 */
  bool WaitConsumerFence();

  std::mutex mutex_;
  GPUPixelGLProgram* shader_program_ = nullptr;
  uint32_t position_attribute_ = 0;
  uint32_t tex_coord_attribute_ = 0;

  /** 双缓冲：交替写入，避免外部正在采样时被覆盖 */
  std::shared_ptr<GPUPixelFramebuffer> framebuffers_[2];
  int width_ = 0;
  int height_ = 0;
  /** 下一个要写入的槽（0/1） */
  int write_index_ = 0;

  std::atomic<uint32_t> result_texture_{0};
  std::atomic<uint64_t> result_serial_{0};
  std::atomic<int64_t> consumer_fence_{0};
  std::atomic<int> fence_wait_failures_{0};
};

}  // namespace gpupixel
