/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include <jni.h>

#include "android/jni/jni_helpers.h"
#include "gpupixel/source/source_texture.h"

using namespace gpupixel;

// 创建 SourceTexture 实例
extern "C" JNIEXPORT jlong JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativeCreate(JNIEnv* env,
                                                             jclass clazz) {
  auto source_texture = SourceTexture::Create();
  if (!source_texture) {
    return 0;
  }
  auto* ptr = new std::shared_ptr<SourceTexture>(source_texture);
  return reinterpret_cast<jlong>(ptr);
}

// 销毁实例
extern "C" JNIEXPORT void JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativeDestroy(JNIEnv* env,
                                                              jclass clazz,
                                                              jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SourceTexture>*>(native_obj);
  delete ptr;
}

/**
 * 推入外部纹理并触发一次处理（替代 SourceRawData 的 ProcessData）。
 *
 * @return 是否成功
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativePushTexture(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj,
    jint texture,
    jint width,
    jint height) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SourceTexture>*>(native_obj);
  if (!ptr || !*ptr) {
    return JNI_FALSE;
  }
  bool ok = (*ptr)->PushTexture(static_cast<uint32_t>(texture),
                                static_cast<int>(width),
                                static_cast<int>(height));
  return ok ? JNI_TRUE : JNI_FALSE;
}

// 推入次数（诊断）
extern "C" JNIEXPORT jlong JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativeGetPushSerial(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SourceTexture>*>(native_obj);
  if (!ptr || !*ptr) {
    return 0;
  }
  return static_cast<jlong>((*ptr)->GetPushSerial());
}

// 最近一次推入的纹理 id / 尺寸（诊断）
extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativeGetInputTexture(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SourceTexture>*>(native_obj);
  if (!ptr || !*ptr) {
    return 0;
  }
  return static_cast<jint>((*ptr)->GetInputTexture());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativeGetInputWidth(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SourceTexture>*>(native_obj);
  return ptr && *ptr ? (*ptr)->GetInputWidth() : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSourceTexture_nativeGetInputHeight(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SourceTexture>*>(native_obj);
  return ptr && *ptr ? (*ptr)->GetInputHeight() : 0;
}
