/*
 * GPUPixel
 *
 * Created by PixPark on 2021/6/24.
 * Copyright © 2021 PixPark. All rights reserved.
 */

package com.pixpark.gpupixel;

import android.app.Activity;
import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.hardware.camera2.CameraCharacteristics;
import android.media.Image;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.util.Log;
import android.view.Surface;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;

/**
 * Main entry point for GPU image processing operations
 */
public class GPUPixel {
    private static final String TAG = "GPUPixel";

    static {
        System.loadLibrary("gpupixel");
        System.loadLibrary("mars-face-kit");
    }

    private GPUPixel() {}

    /**
     * Initializes the GPUPixel library
     * @param context Application context
     */
    public static void Init(Context context) {
        copyResource(context);
    }

    public void Destroy() {}

    /**
     * Copies required resources from assets to external storage
     * @param context Application context
     * @return Path to external storage
     */
    public static String copyResource(Context context) {
        if (context == null) {
            Log.e(TAG, "Context is null in copyResource");
            return "";
        }

        String exPath = "";
        try {
            exPath = context.getExternalFilesDir(null).getAbsolutePath();

            // Create target directories
            File resDir = new File(exPath + "/gpupixel/res");
            if (!resDir.exists()) {
                resDir.mkdirs();
            }

            File modelsDir = new File(exPath + "/gpupixel/models");
            if (!modelsDir.exists()) {
                modelsDir.mkdirs();
            }

            // Get all files under assets
            processAssetsDirectory(context, "", exPath);

            // Set resource path
            String path = exPath + "/gpupixel";
            nativeSetResourcePath(path);
            Log.i(TAG, "nativeSetResourcePath: " + path);
        } catch (Exception e) {
            Log.e(TAG, "Error in copyResource: " + e.getMessage());
        }

        return exPath;
    }

    /**
     * Process files in assets directory recursively
     * @param context Application context
     * @param assetPath Current path in assets
     * @param exPath External storage path
     */
    private static void processAssetsDirectory(Context context, String assetPath, String exPath) {
        try {
            String[] fileList = context.getAssets().list(assetPath);

            if (fileList != null && fileList.length > 0) {
                // If it's a directory, process recursively
                for (String fileName : fileList) {
                    String subPath = assetPath.isEmpty() ? fileName : assetPath + "/" + fileName;
                    processAssetsDirectory(context, subPath, exPath);
                }
            } else {
                // It's a file, copy based on file type
                String targetPath;

                if (assetPath.toLowerCase().endsWith(".jpg")
                        || assetPath.toLowerCase().endsWith(".jpeg")
                        || assetPath.toLowerCase().endsWith(".png")
                        || assetPath.toLowerCase().endsWith(".gif")) {
                    // Image files to res directory
                    targetPath = exPath + "/gpupixel/res/" + new File(assetPath).getName();
                } else if (assetPath.toLowerCase().endsWith(".mars_model")) {
                    // .mars_model files to models directory
                    targetPath = exPath + "/gpupixel/models/" + new File(assetPath).getName();
                } else {
                    // Other files not processed
                    return;
                }

                // Copy file
                File targetFile = new File(targetPath);
                if (!targetFile.exists()) {
                    InputStream is = context.getAssets().open(assetPath);
                    FileOutputStream fos = new FileOutputStream(targetFile);
                    byte[] buffer = new byte[1024];
                    int byteCount;
                    while ((byteCount = is.read(buffer)) != -1) {
                        fos.write(buffer, 0, byteCount);
                    }
                    fos.flush();
                    is.close();
                    fos.close();
                    Log.d(TAG, "Copied asset file: " + assetPath + " to " + targetPath);
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Error processing asset: " + assetPath + ", " + e.getMessage());
        }
    }

    /**
     * Converts YUV_420_888 format image to RGBA format
     *
     * @param image YUV_420_888 format image
     * @return RGBA format byte array
     */
    public static byte[] YUV_420_888toRGBA(Image image) {
        if (image == null) return null;

        int width = image.getWidth();
        int height = image.getHeight();

        Image.Plane[] planes = image.getPlanes();
        ByteBuffer yBuffer = planes[0].getBuffer();
        ByteBuffer uBuffer = planes[1].getBuffer();
        ByteBuffer vBuffer = planes[2].getBuffer();

        int yRowStride = planes[0].getRowStride();
        int uRowStride = planes[1].getRowStride();
        int vRowStride = planes[2].getRowStride();

        int yPixelStride = planes[0].getPixelStride();
        int uPixelStride = planes[1].getPixelStride();
        int vPixelStride = planes[2].getPixelStride();

        byte[] rgba = new byte[width * height * 4];

        nativeYUV420ToRGBA(yBuffer, uBuffer, vBuffer, width, height, yRowStride, uRowStride,
                vRowStride, yPixelStride, uPixelStride, vPixelStride, rgba);

        return rgba;
    }

    /**
     * Rotates RGBA format image
     *
     * @param rgba Original RGBA format image data
     * @param width Image width
     * @param height Image height
     * @param rotation Rotation angle (0, 90, 180, 270)
     * @return Rotated RGBA image data
     */
    public static byte[] rotateRgbaImage(byte[] rgba, int width, int height, int rotation) {
        if (rgba == null || rgba.length != width * height * 4) {
            Log.e(TAG, "Invalid RGBA data or dimensions mismatch");
            return rgba;
        }

        if (rotation == 0 || rotation == 360) {
            return rgba;
        }

        int outWidth = (rotation == 90 || rotation == 270) ? height : width;
        int outHeight = (rotation == 90 || rotation == 270) ? width : height;

        byte[] rotatedData = new byte[outWidth * outHeight * 4];

        nativeRotateRGBA(rgba, width, height, rotatedData, outWidth, outHeight, rotation);

        return rotatedData;
    }

    /**
     * Calculates image rotation angle
     *
     * @param activity Current Activity
     * @param sensorOrientation Camera sensor orientation
     * @param isFrontCamera Whether it's a front camera
     * @return Angle by which the image needs to be rotated
     */
    public static int calculateRotation(
            Activity activity, int sensorOrientation, boolean isFrontCamera) {
        int displayRotation = activity.getWindowManager().getDefaultDisplay().getRotation();
        int deviceRotation = 0;
        switch (displayRotation) {
            case Surface.ROTATION_0:
                deviceRotation = 0;
                break;
            case Surface.ROTATION_90:
                deviceRotation = 90;
                break;
            case Surface.ROTATION_180:
                deviceRotation = 180;
                break;
            case Surface.ROTATION_270:
                deviceRotation = 270;
                break;
        }

        int rotation;
        if (isFrontCamera) {
            rotation = (sensorOrientation + deviceRotation) % 360;
        } else {
            rotation = (sensorOrientation - deviceRotation + 360) % 360;
        }

        return rotation;
    }

    /**
     * Checks if the camera is front-facing
     *
     * @param facing Camera facing direction
     * @return Whether it's a front-facing camera
     */
    public static boolean isFrontCamera(int facing) {
        return facing == CameraCharacteristics.LENS_FACING_FRONT;
    }

    // ===== 本地扩展（fork）：与外部分享 EGLContext =====

    /**
     * 捕获**当前线程**的 EGL context，登记为 GPUPixel 自己的 share_context。
     *
     * 作用：共享后 GPUPixel 与外部渲染器互通 texture / FBO / sync —— 可直接把处理
     * 结果留在 GPU 上供外部采样，彻底免去每帧「回读成字节 → 再上传」的跨界搬运
     * （1080p 下约 5 × 8.29 MB/帧）。
     *
     * ⚠️ 必须在**有 current context 的线程**调用（GLSurfaceView 的渲染线程，
     *    典型位置 Renderer.onSurfaceCreated 开头），且必须在任何会触发 GPUPixel
     *    GPU 操作的调用之前。
     * ⚠️ 若未登记（或登记后建立失败），GPUPixel 会退回「私有 context」的原有行为，
     *    功能不受影响，只是没有零拷贝收益。
     *
     * @return 是否捕获成功
     */
    public static boolean captureSharedEglContext() {
        return nativeCaptureSharedEglContext();
    }

    /** 是否已登记共享 context（诊断用） */
    public static boolean hasSharedEglContext() {
        return nativeHasSharedEglContext();
    }

    // JNI Native methods
    private static native void nativeYUV420ToRGBA(ByteBuffer yBuffer, ByteBuffer uBuffer,
            ByteBuffer vBuffer, int width, int height, int yRowStride, int uRowStride,
            int vRowStride, int yPixelStride, int uPixelStride, int vPixelStride, byte[] rgbaOut);

    private static native void nativeRotateRGBA(byte[] rgbaIn, int width, int height,
            byte[] rgbaOut, int outWidth, int outHeight, int rotationDegrees);

    private static native void nativeSetResourcePath(String path);

    private static native boolean nativeCaptureSharedEglContext();

    private static native boolean nativeHasSharedEglContext();
}
