/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include "gpupixel/sink/sink_texture.h"

#include "core/gpupixel_context.h"
#include "core/gpupixel_framebuffer.h"
#include "core/gpupixel_gl_include.h"
#include "core/gpupixel_program.h"
#include "utils/logging.h"

namespace gpupixel {

const std::string kSinkTextureVertexShaderString = R"(
    attribute vec4 position;
    attribute vec4 inputTextureCoordinate;
    varying vec2 textureCoordinate;
    void main() {
      gl_Position = position;
      textureCoordinate = inputTextureCoordinate.xy;
    })";

const std::string kSinkTextureFragmentShaderString = R"(
    precision mediump float;
    varying vec2 textureCoordinate;
    uniform sampler2D inputImageTexture;
    void main() {
      gl_FragColor = texture2D(inputImageTexture, textureCoordinate);
    })";

std::shared_ptr<SinkTexture> SinkTexture::Create() {
  auto ret = std::shared_ptr<SinkTexture>(new SinkTexture());
  GPUPixelContext::GetInstance()->SyncRunWithContext([&] {
    if (ret && !ret->Init()) {
      ret.reset();
    }
  });
  return ret;
}

SinkTexture::SinkTexture() {}

SinkTexture::~SinkTexture() {
  // framebuffers_ 由 shared_ptr 自动释放；shader_program_ 归 GPUPixelGLProgram 管理
  shader_program_ = nullptr;
}

bool SinkTexture::Init() {
  shader_program_ = GPUPixelGLProgram::CreateWithShaderString(
      kSinkTextureVertexShaderString, kSinkTextureFragmentShaderString);
  if (!shader_program_) {
    LOG_ERROR("SinkTexture: failed to create shader program");
    return false;
  }
  GPUPixelContext::GetInstance()->SetActiveGlProgram(shader_program_);
  position_attribute_ = shader_program_->GetAttribLocation("position");
  tex_coord_attribute_ =
      shader_program_->GetAttribLocation("inputTextureCoordinate");
  LOG_INFO("SinkTexture: initialized (output stays on GPU, no glReadPixels)");
  return true;
}

void SinkTexture::SetInputFramebuffer(
    std::shared_ptr<GPUPixelFramebuffer> framebuffer,
    RotationMode rotation_mode /* = NoRotation*/,
    int tex_idx /* = 0*/) {
  Sink::SetInputFramebuffer(framebuffer, rotation_mode, tex_idx);
}

void SinkTexture::EnsureFramebuffers(int width, int height) {
  if (framebuffers_[0] && framebuffers_[1] && width_ == width &&
      height_ == height) {
    return;
  }
  // 注意：FramebufferFactory 的缓存计数只在「命中」分支递增，连续两次同尺寸
  // CreateFramebuffer 会各自 new 一个 —— 正好满足这里的双缓冲需求。
  auto factory = GPUPixelContext::GetInstance()->GetFramebufferFactory();
  for (int i = 0; i < 2; ++i) {
    framebuffers_[i] = factory->CreateFramebuffer(width, height);
  }
  width_ = width;
  height_ = height;
  write_index_ = 0;
  LOG_INFO("SinkTexture: double framebuffers created {}x{}", width, height);
}

void SinkTexture::SetConsumerFence(int64_t fence) {
  consumer_fence_.store(fence, std::memory_order_release);
}

bool SinkTexture::WaitConsumerFence() {
  int64_t raw = consumer_fence_.exchange(0, std::memory_order_acq_rel);
  if (raw == 0) {
    return true;
  }
  // raw 是外部 context 创建的 GLsync，在本 context 里等待（共享组内可见）。
  // ⚠️ 不带 GL_SYNC_FLUSH_COMMANDS_BIT —— 该位 flush 的是本 context 的命令队列，
  //    而被等待的 sync 由另一个 context 的命令流决定，带它毫无帮助。
  // ⚠️ WAIT_FAILED 在跨 context 场景会偶发，重试通常即成功（否则这一帧跳过写入）。
  GLsync sync = reinterpret_cast<GLsync>(static_cast<intptr_t>(raw));
  const int kMaxAttempts = 3;
  for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
    GLenum status = glClientWaitSync(sync, 0, 200000000);  // 200ms
    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
      glDeleteSync(sync);
      return true;
    }
  }
  fence_wait_failures_.fetch_add(1, std::memory_order_relaxed);
  LOG_WARN("SinkTexture: consumer fence wait failed after {} attempts", kMaxAttempts);
  glDeleteSync(sync);
  // 即便没等到也继续（宁可覆盖也不卡死流水线）；外部有保底帧机制兜底
  return false;
}

void SinkTexture::Render() {
  if (input_framebuffers_.empty()) {
    return;
  }
  auto input = input_framebuffers_[0].frame_buffer;
  if (!input) {
    return;
  }

  // 外部消费端若回传了 fence，先等它 —— 保证「正在被采样的那张」不被覆盖
  WaitConsumerFence();

  const int width = input->GetWidth();
  const int height = input->GetHeight();
  if (width <= 0 || height <= 0) {
    return;
  }
  EnsureFramebuffers(width, height);

  auto& target = framebuffers_[write_index_];
  if (!target) {
    return;
  }

  GPUPixelContext::GetInstance()->SetActiveGlProgram(shader_program_);
  target->Activate();
  GL_CALL(glViewport(0, 0, width, height));
  GL_CALL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
  GL_CALL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

  // 直通：与 SinkRawData::Render 完全同构，唯一区别是**不做 glReadPixels**
  float image_vertices[] = {
      -1.0f, -1.0f,  // Bottom left
      1.0f,  -1.0f,  // Bottom right
      -1.0f, 1.0f,   // Top left
      1.0f,  1.0f    // Top right
  };
  float texture_vertices[] = {
      0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
  };

  GL_CALL(glEnableVertexAttribArray(position_attribute_));
  GL_CALL(glVertexAttribPointer(position_attribute_, 2, GL_FLOAT, 0, 0,
                                image_vertices));
  GL_CALL(glEnableVertexAttribArray(tex_coord_attribute_));
  GL_CALL(glVertexAttribPointer(tex_coord_attribute_, 2, GL_FLOAT, 0, 0,
                                texture_vertices));

  GL_CALL(glActiveTexture(GL_TEXTURE0));
  GL_CALL(glBindTexture(GL_TEXTURE_2D, input->GetTexture()));
  shader_program_->SetUniformValue("inputImageTexture", 0);
  GL_CALL(glDrawArrays(GL_TRIANGLE_STRIP, 0, 4));

  target->Deactivate();

  // 发布结果：先更新 texture，再递增序号（外部按序号判断"是否有新结果"）
  result_texture_.store(target->GetTexture(), std::memory_order_release);
  result_serial_.fetch_add(1, std::memory_order_release);

  // 翻转写指针 —— 下一帧写另一张，正在被外部采样的这张不会被覆盖
  write_index_ = 1 - write_index_;
}

}  // namespace gpupixel
