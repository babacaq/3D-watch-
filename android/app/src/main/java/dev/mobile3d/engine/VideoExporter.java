package dev.mobile3d.engine;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.ByteBuffer;

/**
 * Кодер MP4: нативный код присылает готовые кадры RGBA (сверху вниз), здесь они превращаются в H.264
 * через MediaCodec + MediaMuxer, а готовый файл кладётся в галерею (Movies/PrismLab).
 * Все методы вызываются из одного потока (нативный поток приложения).
 */
final class VideoExporter {
    private final Context ctx;
    private MediaCodec codec;
    private MediaMuxer muxer;
    private File temp;
    private String name;
    private int width, height, fps;
    private int track = -1;
    private boolean muxerStarted = false;
    private long lastPtsUs = -1;
    private final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
    private byte[] row = new byte[0];

    VideoExporter(Context ctx) {
        this.ctx = ctx;
    }

    boolean begin(int w, int h, int fps, int mbps, String name) {
        cleanup();
        try {
            this.width = w & ~1;
            this.height = h & ~1;
            this.fps = Math.max(1, fps);
            this.name = name.endsWith(".mp4") ? name : name + ".mp4";
            temp = new File(ctx.getCacheDir(), "export_tmp.mp4");
            if (temp.exists()) temp.delete();
            MediaFormat f = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height);
            f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
            f.setInteger(MediaFormat.KEY_BIT_RATE, Math.max(1, mbps) * 1000000);
            f.setInteger(MediaFormat.KEY_FRAME_RATE, this.fps);
            f.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);
            codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC);
            codec.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            codec.start();
            muxer = new MediaMuxer(temp.getAbsolutePath(), MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            track = -1;
            muxerStarted = false;
            lastPtsUs = -1;
            return true;
        } catch (Throwable e) {
            android.util.Log.e("PrismLab", "video begin failed", e);
            cleanup();
            return false;
        }
    }

    boolean frame(ByteBuffer rgba, int w, int h, int index) {
        if (codec == null || w < width || h < height) return false;
        try {
            int id = -1;
            for (int tries = 0; tries < 500 && id < 0; tries++) {
                id = codec.dequeueInputBuffer(10000);
                if (id < 0) drain(false);
            }
            if (id < 0) return false;
            Image img = codec.getInputImage(id);
            if (img == null) return false;
            fill(img, rgba, w);
            long pts = (long) index * 1000000L / fps;
            if (pts <= lastPtsUs) pts = lastPtsUs + 1;
            lastPtsUs = pts;
            codec.queueInputBuffer(id, 0, width * height * 3 / 2, pts, 0);
            drain(false);
            return true;
        } catch (Throwable e) {
            android.util.Log.e("PrismLab", "video frame failed", e);
            return false;
        }
    }

    boolean end() {
        if (codec == null) return false;
        boolean ok = false;
        try {
            int id = -1;
            for (int tries = 0; tries < 500 && id < 0; tries++) {
                id = codec.dequeueInputBuffer(10000);
                if (id < 0) drain(false);
            }
            if (id >= 0) {
                codec.queueInputBuffer(id, 0, 0, lastPtsUs + 1, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                drain(true);
            }
            codec.stop();
            codec.release();
            codec = null;
            if (muxerStarted) {
                muxer.stop();
                ok = true;
            }
            muxer.release();
            muxer = null;
            if (ok) ok = saveToGallery();
        } catch (Throwable e) {
            android.util.Log.e("PrismLab", "video end failed", e);
            ok = false;
        }
        cleanup();
        return ok;
    }

    void cancel() {
        cleanup();
    }

    private void drain(boolean eos) {
        while (true) {
            int out = codec.dequeueOutputBuffer(info, eos ? 10000 : 0);
            if (out == MediaCodec.INFO_TRY_AGAIN_LATER) {
                if (!eos) return;
                continue;
            } else if (out == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                if (!muxerStarted) {
                    track = muxer.addTrack(codec.getOutputFormat());
                    muxer.start();
                    muxerStarted = true;
                }
            } else if (out >= 0) {
                ByteBuffer data = codec.getOutputBuffer(out);
                if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) info.size = 0;
                if (info.size > 0 && data != null && muxerStarted) {
                    data.position(info.offset);
                    data.limit(info.offset + info.size);
                    muxer.writeSampleData(track, data, info);
                }
                codec.releaseOutputBuffer(out, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) return;
            }
        }
    }

    /** RGBA -> YUV 4:2:0 (BT.601, диапазон 16..235), в плоскости Image как отдаёт кодер. */
    private void fill(Image img, ByteBuffer rgba, int srcW) {
        Image.Plane[] pl = img.getPlanes();
        ByteBuffer yb = pl[0].getBuffer(), ub = pl[1].getBuffer(), vb = pl[2].getBuffer();
        int ys = pl[0].getRowStride(), yp = pl[0].getPixelStride();
        int us = pl[1].getRowStride(), up = pl[1].getPixelStride();
        int vs = pl[2].getRowStride(), vp = pl[2].getPixelStride();
        if (row.length < width) row = new byte[width];
        for (int y = 0; y < height; y++) {
            int src = y * srcW * 4;
            for (int x = 0; x < width; x++) {
                int p = src + x * 4;
                int r = rgba.get(p) & 255, g = rgba.get(p + 1) & 255, b = rgba.get(p + 2) & 255;
                int yy = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                row[x] = (byte) yy;
            }
            if (yp == 1) {
                yb.position(y * ys);
                yb.put(row, 0, width);
            } else {
                for (int x = 0; x < width; x++) yb.put(y * ys + x * yp, row[x]);
            }
        }
        for (int y = 0; y < height / 2; y++) {
            for (int x = 0; x < width / 2; x++) {
                int r = 0, g = 0, b = 0;
                for (int dy = 0; dy < 2; dy++)
                    for (int dx = 0; dx < 2; dx++) {
                        int p = ((y * 2 + dy) * srcW + (x * 2 + dx)) * 4;
                        r += rgba.get(p) & 255;
                        g += rgba.get(p + 1) & 255;
                        b += rgba.get(p + 2) & 255;
                    }
                r >>= 2; g >>= 2; b >>= 2;
                int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                ub.put(y * us + x * up, (byte) Math.max(0, Math.min(255, u)));
                vb.put(y * vs + x * vp, (byte) Math.max(0, Math.min(255, v)));
            }
        }
    }

    private boolean saveToGallery() {
        try {
            ContentResolver cr = ctx.getContentResolver();
            ContentValues cv = new ContentValues();
            cv.put(MediaStore.Video.Media.DISPLAY_NAME, name);
            cv.put(MediaStore.Video.Media.MIME_TYPE, "video/mp4");
            cv.put(MediaStore.Video.Media.RELATIVE_PATH, Environment.DIRECTORY_MOVIES + "/PrismLab");
            cv.put(MediaStore.Video.Media.IS_PENDING, 1);
            Uri uri = cr.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, cv);
            if (uri == null) return false;
            try (InputStream in = new FileInputStream(temp); OutputStream os = cr.openOutputStream(uri)) {
                if (os == null) return false;
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            }
            ContentValues done = new ContentValues();
            done.put(MediaStore.Video.Media.IS_PENDING, 0);
            cr.update(uri, done, null, null);
            return true;
        } catch (Throwable e) {
            android.util.Log.e("PrismLab", "save video failed", e);
            return false;
        }
    }

    private void cleanup() {
        try { if (codec != null) { codec.stop(); } } catch (Throwable ignored) { }
        try { if (codec != null) codec.release(); } catch (Throwable ignored) { }
        try { if (muxer != null) muxer.release(); } catch (Throwable ignored) { }
        codec = null;
        muxer = null;
        muxerStarted = false;
        if (temp != null && temp.exists()) temp.delete();
    }
}
