// Joy Launcher (Android) — 미리보기 영상 재생
// MediaExtractor + MediaCodec 으로 영상을 직접 풀어 장면(YUV)을 네이티브(SDL 그림)로 넘긴다.
// 화면에는 런처가 테마의 다른 그림과 똑같이 그리므로 테마의 자리·겹침·화면 전환이 그대로 적용된다.
//
// 디코더를 Surface 없이(ByteBuffer 방식) 쓴다: 출력이 CPU 가 읽을 수 있는 메모리로 나온다.
// (처음에는 MediaPlayer → ImageReader 였으나, 퀄컴 디코더는 ImageReader 에 CPU 로 읽을 수 없는 버퍼를 보내
//  getPlanes() 안에서 프로세스가 강제 종료됐다 — RP6, 2026-11-17)
// 소리는 소리가 필요할 때만 MediaPlayer(그림 없이)로 내고, 영상이 처음으로 돌아갈 때 함께 처음으로 맞춘다.
package com.jyjylab.joylauncher;

import android.media.AudioAttributes;
import android.media.Image;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaPlayer;
import android.os.SystemClock;
import android.util.Log;

import java.nio.ByteBuffer;

final class JoyVideo {
    private static final String TAG = "JoyLauncher";

    private static final Object lock = new Object();
    private static Worker current;  // 지금 재생 중인 것 (null = 없음)

    private JoyVideo() {
    }

    /** 재생 시작 (바로 돌아옴). 이전 영상은 멈춘다. */
    static void start(String path, boolean sound, int tok) {
        synchronized (lock) {
            if (current != null) current.quit = true;
            current = new Worker(path, sound, tok);
            current.start();
        }
    }

    static void stop() {
        synchronized (lock) {
            if (current != null) current.quit = true;
            current = null;
        }
    }

    /** 런처가 화면에서 물러날 때: 멈추고, 재생 중이었으면 네이티브에도 알린다. */
    static void stopForPause() {
        int tok = 0;
        synchronized (lock) {
            if (current != null) {
                current.quit = true;
                tok = current.token;
            }
            current = null;
        }
        if (tok != 0) JoyActivity.nativeVideoState(tok, 0, 0);
    }

    private static final class Worker extends Thread {
        final String path;
        final boolean sound;
        final int token;
        volatile boolean quit;

        Worker(String path, boolean sound, int token) {
            super("JoyVideo");
            this.path = path;
            this.sound = sound;
            this.token = token;
        }

        @Override
        public void run() {
            MediaExtractor ex = null;
            MediaCodec codec = null;
            MediaPlayer audio = null;
            boolean failed = false;
            try {
                ex = new MediaExtractor();
                ex.setDataSource(path);
                int track = -1;
                MediaFormat fmt = null;
                boolean hasAudio = false;
                for (int i = 0; i < ex.getTrackCount(); i++) {
                    MediaFormat f = ex.getTrackFormat(i);
                    String mime = f.getString(MediaFormat.KEY_MIME);
                    if (mime == null) continue;
                    if (mime.startsWith("video/") && track < 0) {
                        track = i;
                        fmt = f;
                    } else if (mime.startsWith("audio/")) {
                        hasAudio = true;
                    }
                }
                if (track < 0) throw new IllegalStateException("no video track");
                ex.selectTrack(track);
                codec = MediaCodec.createDecoderByType(fmt.getString(MediaFormat.KEY_MIME));
                codec.configure(fmt, null, null, 0);  // Surface 없음: 출력은 CPU 메모리
                codec.start();
                if (sound && hasAudio && !quit) audio = openAudio();
                JoyActivity.nativeVideoState(token, 2, audio != null ? 1 : 0);
                play(ex, codec, audio);
            } catch (Throwable t) {
                if (!quit) {
                    Log.w(TAG, "video: " + path + ": " + t);
                    failed = true;
                }
            } finally {
                if (audio != null) {
                    try {
                        audio.release();
                    } catch (Throwable t) {
                        // 무시
                    }
                }
                if (codec != null) {
                    try {
                        codec.stop();
                    } catch (Throwable t) {
                        // 무시
                    }
                    try {
                        codec.release();
                    } catch (Throwable t) {
                        // 무시
                    }
                }
                if (ex != null) {
                    try {
                        ex.release();
                    } catch (Throwable t) {
                        // 무시
                    }
                }
            }
            if (failed) JoyActivity.nativeVideoState(token, -1, 0);
        }

        private MediaPlayer openAudio() {
            MediaPlayer mp = new MediaPlayer();
            try {
                mp.setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA)
                        .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build());
                mp.setDataSource(path);
                mp.prepare();  // 그림 없이 소리만 (Surface 를 주지 않음)
                mp.setLooping(true);
                return mp;
            } catch (Throwable t) {
                Log.w(TAG, "video audio: " + t);
                mp.release();
                return null;
            }
        }

        // 풀어서 제 시간에 넘기기. 끝나면 처음부터 되풀이
        private void play(MediaExtractor ex, MediaCodec codec, MediaPlayer audio) throws Exception {
            MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            boolean inputDone = false;
            long startMs = -1;    // 이번 바퀴의 첫 장면이 나온 때 (SystemClock.uptimeMillis)
            long firstPtsUs = 0;  // 이번 바퀴의 첫 장면 시각
            int frames = 0;
            while (!quit) {
                if (!inputDone) {
                    int in = codec.dequeueInputBuffer(5000);
                    if (in >= 0) {
                        ByteBuffer buf = codec.getInputBuffer(in);
                        int n = buf == null ? -1 : ex.readSampleData(buf, 0);
                        if (n < 0) {
                            codec.queueInputBuffer(in, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM);
                            inputDone = true;
                        } else {
                            codec.queueInputBuffer(in, 0, n, ex.getSampleTime(), 0);
                            ex.advance();
                        }
                    }
                }
                int out = codec.dequeueOutputBuffer(info, 5000);
                if (out < 0) continue;  // 아직 없음 / 형식 바뀜 (Image 가 크기·형식을 알려 줌)
                boolean eos = (info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
                if (info.size > 0 || !eos) {
                    if (startMs < 0) {
                        startMs = SystemClock.uptimeMillis();
                        firstPtsUs = info.presentationTimeUs;
                        if (audio != null) {
                            audio.seekTo(0);
                            if (!audio.isPlaying()) audio.start();
                        }
                    }
                    long dueMs = startMs + (info.presentationTimeUs - firstPtsUs) / 1000;
                    long waitMs = dueMs - SystemClock.uptimeMillis();
                    while (waitMs > 0 && !quit) {  // 제 시간까지 기다림 (멈춤 요청은 바로 받음)
                        Thread.sleep(Math.min(waitMs, 20));
                        waitMs = dueMs - SystemClock.uptimeMillis();
                    }
                    if (quit) {
                        codec.releaseOutputBuffer(out, false);
                        break;
                    }
                    if (waitMs > -150 || frames == 0) {  // 많이 늦은 장면은 건너뜀
                        deliver(codec, out);
                        frames++;
                    }
                }
                codec.releaseOutputBuffer(out, false);
                if (eos) {  // 처음으로
                    if (frames == 0) throw new IllegalStateException("no frames");
                    ex.seekTo(0, MediaExtractor.SEEK_TO_CLOSEST_SYNC);
                    codec.flush();
                    inputDone = false;
                    startMs = -1;
                }
            }
        }

        private void deliver(MediaCodec codec, int index) {
            Image img = null;
            try {
                img = codec.getOutputImage(index);
                if (img == null) return;
                Image.Plane[] pl = img.getPlanes();
                if (pl.length < 3) return;
                android.graphics.Rect c = img.getCropRect();
                JoyActivity.nativeVideoYuv(token, pl[0].getBuffer(), pl[1].getBuffer(), pl[2].getBuffer(), pl[0].getRowStride(),
                        pl[1].getRowStride(), pl[1].getPixelStride(), c.left, c.top, c.width(), c.height());
            } finally {
                if (img != null) img.close();
            }
        }
    }
}
