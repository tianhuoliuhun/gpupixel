/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

#include <jni.h>

#include "android/jni/jni_helpers.h"
#include "gpupixel/sink/sink_texture.h"

using namespace gpupixel;

// 创建 SinkTexture 实例
extern "C" JNIEXPORT jlong JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeCreate(JNIEnv* env,
                                                           jclass clazz) {
  auto sink_texture = SinkTexture::Create();
  if (!sink_texture) {
    return 0;
  }
  auto* ptr = new std::shared_ptr<SinkTexture>(sink_texture);
  return reinterpret_cast<jlong>(ptr);
}

// 销毁实例
extern "C" JNIEXPORT void JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeDestroy(JNIEnv* env,
                                                            jclass clazz,
                                                            jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  delete ptr;
}

// 取结果纹理 id（共享 EGLContext 下外部可直接采样；未渲染过为 0）
extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeGetTextureId(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  if (!ptr || !*ptr) {
    return 0;
  }
  return static_cast<jint>((*ptr)->GetTextureId());
}

// 取结果序号（单调递增；外部据此判断是否拿到了新结果）
extern "C" JNIEXPORT jlong JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeGetResultSerial(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  if (!ptr || !*ptr) {
    return 0;
  }
  return static_cast<jlong>((*ptr)->GetResultSerial());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeGetWidth(JNIEnv* env,
                                                             jclass clazz,
                                                             jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  return ptr && *ptr ? (*ptr)->GetWidth() : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeGetHeight(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  return ptr && *ptr ? (*ptr)->GetHeight() : 0;
}

// 外部消费完当前结果后回传 fence（GLsync 的 native 句柄；0 表示清除）
extern "C" JNIEXPORT void JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeSetConsumerFence(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj,
    jlong fence) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  if (ptr && *ptr) {
    (*ptr)->SetConsumerFence(static_cast<int64_t>(fence));
  }
}

// fence 等待失败次数（诊断）
extern "C" JNIEXPORT jint JNICALL
Java_com_pixpark_gpupixel_GPUPixelSinkTexture_nativeGetFenceWaitFailures(
    JNIEnv* env,
    jclass clazz,
    jlong native_obj) {
  auto* ptr = reinterpret_cast<std::shared_ptr<SinkTexture>*>(native_obj);
  return ptr && *ptr ? (*ptr)->GetFenceWaitFailures() : 0;
}
