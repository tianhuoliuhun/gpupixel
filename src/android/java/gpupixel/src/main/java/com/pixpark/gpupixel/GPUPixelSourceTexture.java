/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

package com.pixpark.gpupixel;

/**
 * 本地扩展（fork）：直接吃**外部纹理**（共享 EGLContext 下由外部渲染器创建/写入）的输入源，
 * 免去 {@link GPUPixelSourceRawData} 每帧的 `glTexImage2D` 像素上传（1080p 约 8.29 MB/帧）。
 *
 * <p>使用前提：外部渲染器在创建 GPUPixel 管线**之前**调用过
 * {@code GPUPixel.captureSharedEglContext()}，使两者共享 texture / FBO / sync。
 *
 * <p>典型用法：
 * <pre>
 *   source = GPUPixelSourceTexture.Create();
 *   source.AddSink(reshape); reshape.AddSink(beauty); beauty.AddSink(sinkTexture);
 *   ...
 *   source.PushTexture(myGlTextureId, w, h);   // 替代 ProcessData(byte[], ...)
 *   int outTex = sinkTexture.GetTextureId();
 * </pre>
 *
 * <p>⚠️ 调用方负责生命周期：外部纹理在 GPUPixel 采样期间**不能被改写** ——
 * 需用多缓冲轮转输入帧，并在采样前用 fence 同步。
 */
public class GPUPixelSourceTexture extends GPUPixelSource {

    protected GPUPixelSourceTexture() {}

    public static GPUPixelSourceTexture Create() {
        final GPUPixelSourceTexture source = new GPUPixelSourceTexture();
        if (source.mNativeClassID != 0) return source;
        source.mNativeClassID = nativeCreate();
        return source;
    }

    /**
     * 设置外部输入纹理并推动一次处理（替代 {@code ProcessData(byte[], ...)}）。
     *
     * @param texture 共享 context 下由外部渲染器创建的纹理 id
     * @param width   纹理宽
     * @param height  纹理高
     * @return 是否成功（texture=0 / 尺寸非法时为 false）
     */
    public boolean PushTexture(int texture, int width, int height) {
        if (mNativeClassID == 0) return false;
        return nativePushTexture(mNativeClassID, texture, width, height);
    }

    /** 推入次数（单调递增，诊断用） */
    public long GetPushSerial() {
        return mNativeClassID == 0 ? 0 : nativeGetPushSerial(mNativeClassID);
    }

    /** 最近一次推入的纹理 id（诊断用） */
    public int GetInputTexture() {
        return mNativeClassID == 0 ? 0 : nativeGetInputTexture(mNativeClassID);
    }

    public int GetInputWidth() {
        return mNativeClassID == 0 ? 0 : nativeGetInputWidth(mNativeClassID);
    }

    public int GetInputHeight() {
        return mNativeClassID == 0 ? 0 : nativeGetInputHeight(mNativeClassID);
    }

    @Override
    public void Destroy() {
        if (mNativeClassID != 0) {
            nativeDestroy(mNativeClassID);
            mNativeClassID = 0;
        }
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            if (mNativeClassID != 0) {
                nativeDestroy(mNativeClassID);
                mNativeClassID = 0;
            }
        } catch (Exception e) {
            e.printStackTrace();
        } finally {
            super.finalize();
        }
    }

    // JNI Native methods
    private static native long nativeCreate();
    private static native void nativeDestroy(long nativeObj);
    private static native boolean nativePushTexture(long nativeObj, int texture, int width, int height);
    private static native long nativeGetPushSerial(long nativeObj);
    private static native int nativeGetInputTexture(long nativeObj);
    private static native int nativeGetInputWidth(long nativeObj);
    private static native int nativeGetInputHeight(long nativeObj);
}
