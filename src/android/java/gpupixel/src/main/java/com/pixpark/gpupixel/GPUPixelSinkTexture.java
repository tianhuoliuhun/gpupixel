/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

package com.pixpark.gpupixel;

/**
 * 本地扩展（fork）：把处理结果留在 GPU 上并暴露 texture id，供共享 EGLContext 的
 * 外部渲染器直接采样 —— 取代 {@link GPUPixelSinkRawData} 的
 * 「glReadPixels 回读 + JNI 拷贝」双重大开销（1080p 约 2 × 8.29 MB/帧）。
 *
 * <p>使用前提：外部渲染器在创建 GPUPixel 管线**之前**调用过
 * {@code GPUPixel.captureSharedEglContext()}（或等价的
 * {@code GPUPixelContext::SetSharedEglContext}），使两者共享 texture / FBO / sync。
 *
 * <p>典型用法：
 * <pre>
 *   sink = GPUPixelSinkTexture.Create();
 *   source.AddSink(reshape); reshape.AddSink(beauty); beauty.AddSink(sink);
 *   ...
 *   source.ProcessData(rgba, w, h, stride, FRAME_TYPE_RGBA);
 *   int texId = sink.GetTextureId();      // 直接交给外部 GL 采样，无需回读
 *   ...
 *   sink.SetConsumerFence(myGlSync);      // （可选）外部采样完后回传，避免被覆盖
 * </pre>
 */
public class GPUPixelSinkTexture implements GPUPixelSink {
    protected long mNativeClassID = 0;

    protected GPUPixelSinkTexture() {
        if (mNativeClassID != 0) return;
        mNativeClassID = nativeCreate();
    }

    /**
     * 创建 SinkTexture 实例
     *
     * @return 新实例
     */
    public static GPUPixelSinkTexture Create() {
        return new GPUPixelSinkTexture();
    }

    public int GetWidth() {
        return nativeGetWidth(mNativeClassID);
    }

    public int GetHeight() {
        return nativeGetHeight(mNativeClassID);
    }

    /**
     * 最近一次完成渲染的结果纹理 id。
     *
     * <p>⚠️ 这是 GPUPixel 的 EGLContext 里创建的 texture；只有与外部渲染器**共享
     * EGLContext** 时，外部才能采样它。未渲染过时返回 0。
     */
    public int GetTextureId() {
        return nativeGetTextureId(mNativeClassID);
    }

    /**
     * 结果序号，单调递增。外部据此判断「这次拿到的是不是新结果」——
     * 与上次相同即表示本帧没有产生新结果（应由保底帧/上一张兜底）。
     */
    public long GetResultSerial() {
        return nativeGetResultSerial(mNativeClassID);
    }

    /**
     * 外部消费完当前结果后回传一个由**外部 context** 创建的 fence（GLsync native 句柄）。
     *
     * <p>GPUPixel 会在下一次写入前等待它，从而在「外部消费慢于生产」时也不会覆盖
     * 正在被采样的那张。传 0 表示清除/不需要等待。
     */
    public void SetConsumerFence(long fence) {
        if (mNativeClassID != 0) {
            nativeSetConsumerFence(mNativeClassID, fence);
        }
    }

    /** fence 等待失败（重试后仍失败）的次数，诊断用 */
    public int GetFenceWaitFailures() {
        return nativeGetFenceWaitFailures(mNativeClassID);
    }

    public void Destroy() {
        if (mNativeClassID != 0) {
            nativeDestroy(mNativeClassID);
            mNativeClassID = 0;
        }
    }

    @Override
    public long getNativeClassID() {
        return mNativeClassID;
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
    private static native int nativeGetWidth(long nativeObj);
    private static native int nativeGetHeight(long nativeObj);
    private static native int nativeGetTextureId(long nativeObj);
    private static native long nativeGetResultSerial(long nativeObj);
    private static native void nativeSetConsumerFence(long nativeObj, long fence);
    private static native int nativeGetFenceWaitFailures(long nativeObj);
}
