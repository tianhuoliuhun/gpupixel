/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include "gpupixel/filter/beauty_face_filter.h"
#include "core/gpupixel_context.h"
namespace gpupixel {

BeautyFaceFilter::BeautyFaceFilter() {}

BeautyFaceFilter::~BeautyFaceFilter() {}

std::shared_ptr<BeautyFaceFilter> BeautyFaceFilter::Create() {
  auto ret = std::shared_ptr<BeautyFaceFilter>(new BeautyFaceFilter());
  gpupixel::GPUPixelContext::GetInstance()->SyncRunWithContext([&] {
    if (ret && !ret->Init()) {
      ret.reset();
    }
  });
  return ret;
}

bool BeautyFaceFilter::Init() {
  if (!FilterGroup::Init()) {
    return false;
  }

  box_blur_filter_ = BoxBlurFilter::Create();
  AddFilter(box_blur_filter_);

  box_high_pass_filter_ = BoxHighPassFilter::Create();
  AddFilter(box_high_pass_filter_);

  beauty_face_filter_ = BeautyFaceUnitFilter::Create();
  AddFilter(beauty_face_filter_);

  box_blur_filter_->AddSink(beauty_face_filter_, 1);
  box_high_pass_filter_->AddSink(beauty_face_filter_, 2);

  SetTerminalFilter(beauty_face_filter_);

  box_blur_filter_->SetTexelSpacingMultiplier(4);
  SetRadius(4);

  RegisterProperty("whiteness", 0,
                   "The whiteness of filter with range between -1 and 1.",
                   [this](float& val) { SetWhite(val); });

  RegisterProperty("skin_smoothing", 0,
                   "The smoothing of filter with range between -1 and 1.",
                   [this](float& val) { SetBlurAlpha(val); });

  // ===== 本地扩展（fork）：注册 sharpen 属性 =====
  // 上游只在 Init() 里注册了 whiteness / skin_smoothing，而 BeautyFaceFilter::SetSharpen
  // 早已存在（转发给 BeautyFaceUnitFilter，shader 里有 `uniform highp float sharpen`）——
  // 但由于**未注册成 property**，Java 侧 `SetProperty("sharpen", ...)` 会被静默忽略
  // （Filter::SetProperty 找不到 key 时只打 LOG_WARN），导致应用侧的「GPUPixel 锐化」
  // 滑块完全无效。这里补上注册即可让该滑块真正生效。
  RegisterProperty("sharpen", 0,
                   "The sharpen of filter with range between -1 and 1.",
                   [this](float& val) { SetSharpen(val); });
  return true;
}

void BeautyFaceFilter::SetInputFramebuffer(
    std::shared_ptr<GPUPixelFramebuffer> framebuffer,
    RotationMode rotation_mode /* = NoRotation*/,
    int texIdx /* = 0*/) {
  for (auto& filter : filters_) {
    filter->SetInputFramebuffer(framebuffer, rotation_mode, texIdx);
  }
}

void BeautyFaceFilter::SetHighPassDelta(float highPassDelta) {
  box_high_pass_filter_->SetDelta(highPassDelta);
}

void BeautyFaceFilter::SetSharpen(float sharpen) {
  beauty_face_filter_->SetSharpen(sharpen);
}

void BeautyFaceFilter::SetBlurAlpha(float blurAlpha) {
  beauty_face_filter_->SetBlurAlpha(blurAlpha);
}

void BeautyFaceFilter::SetWhite(float white) {
  beauty_face_filter_->SetWhite(white);
}

void BeautyFaceFilter::SetRadius(float radius) {
  box_blur_filter_->SetRadius(radius);
  box_high_pass_filter_->SetRadius(radius);
}
}  // namespace gpupixel
