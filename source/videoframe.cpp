#include "videoframe.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <sys/stat.h>
#include <vector>

// JPEG 저장은 stb_image_write 로 (SDL_image 의 저장 함수는 내부에서 JPEG 라이브러리를 켜고 끄므로
// 메인 스레드의 이미지 읽기와 동시에 쓰면 스위치에서 튕김)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image_write.h"
#pragma GCC diagnostic pop

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

bool ExtractVideoFrame(const std::string &video, const std::string &outJpg, int maxW) {
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, video.c_str(), nullptr, nullptr) != 0) return false;
    bool ok = false;
    AVCodecContext *ctx = nullptr;
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    SwsContext *sws = nullptr;
    do {
        if (avformat_find_stream_info(fmt, nullptr) < 0) break;
        const AVCodec *codec = nullptr;
        int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (si < 0 || !codec) break;
        AVStream *st = fmt->streams[si];
        ctx = avcodec_alloc_context3(codec);
        if (!ctx || avcodec_parameters_to_context(ctx, st->codecpar) < 0) break;
        ctx->thread_count = 1;  // 스위치: 디코더 내부 스레드 없이
        if (avcodec_open2(ctx, codec, nullptr) < 0) break;

        // 목표 위치: 3초 (영상이 짧으면 30% 지점)
        double dur = fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0;
        double target = dur > 0 ? std::min(3.0, dur * 0.3) : 0;
        int64_t ts = (int64_t)(target / av_q2d(st->time_base));
        if (target > 0) av_seek_frame(fmt, si, ts, AVSEEK_FLAG_BACKWARD);

        bool got = false;
        int packets = 0;
        while (!got && av_read_frame(fmt, pkt) >= 0 && packets++ < 2000) {
            if (pkt->stream_index == si && avcodec_send_packet(ctx, pkt) == 0) {
                while (avcodec_receive_frame(ctx, frame) == 0) {
                    int64_t pts = frame->best_effort_timestamp;
                    if (target <= 0 || pts == AV_NOPTS_VALUE || pts * av_q2d(st->time_base) >= target - 0.05) {
                        got = true;
                        break;
                    }
                }
            }
            av_packet_unref(pkt);
        }
        if (!got) {  // 끝까지 갔으면 남은 프레임이라도
            avcodec_send_packet(ctx, nullptr);
            got = avcodec_receive_frame(ctx, frame) == 0;
        }
        if (!got || frame->width <= 0 || frame->height <= 0) break;

        int w = frame->width, h = frame->height;
        if (w > maxW) {
            h = h * maxW / w;
            w = maxW;
        }
        sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format, w, h, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!sws) break;
        // sws_scale 은 SIMD 로 줄 끝을 넘겨 몇 바이트 더 쓸 수 있으므로 줄 간격을 64 바이트로 맞추고
        // 여유 줄을 둔 버퍼에 받은 뒤, 빈틈 없는 RGB 로 옮긴다 (그대로 쓰면 크기에 따라 메모리가 깨짐)
        int stride = (w * 3 + 63) & ~63;
        std::vector<uint8_t> padded((size_t)stride * (h + 2));
        uint8_t *dst[4] = {padded.data(), nullptr, nullptr, nullptr};
        int dstStride[4] = {stride, 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, dstStride);
        std::vector<uint8_t> rgb((size_t)w * h * 3);
        for (int y = 0; y < h; y++) memcpy(&rgb[(size_t)y * w * 3], &padded[(size_t)y * stride], (size_t)w * 3);
        std::string tmp = outJpg + ".tmp";
        ok = stbi_write_jpg(tmp.c_str(), w, h, 3, rgb.data(), 85) != 0;
        if (ok) {
            remove(outJpg.c_str());
            ok = rename(tmp.c_str(), outJpg.c_str()) == 0;
        } else {
            remove(tmp.c_str());
        }
    } while (false);
    if (sws) sws_freeContext(sws);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    if (ctx) avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    return ok;
}

// ---------------------------------------------------------------- 백그라운드 작업
namespace {
SDL_Thread *gThread = nullptr;
SDL_mutex *gLock = nullptr;
SDL_cond *gCond = nullptr;
bool gQuit = false;
std::string gPendingVideo, gPendingOut;
std::map<std::string, int> gState;  // outJpg -> 1 성공 / -1 실패 / 0 작업 중

int Worker(void *) {
    SDL_LockMutex(gLock);
    while (!gQuit) {
        if (gPendingOut.empty()) {
            SDL_CondWait(gCond, gLock);
            continue;
        }
        std::string video = gPendingVideo, out = gPendingOut;
        gPendingVideo.clear();
        gPendingOut.clear();
        SDL_UnlockMutex(gLock);
        bool ok = ExtractVideoFrame(video, out);
        SDL_LockMutex(gLock);
        gState[out] = ok ? 1 : -1;
    }
    SDL_UnlockMutex(gLock);
    return 0;
}
}  // namespace

void FrameWorkerStart() {
    if (gThread) return;
    gLock = SDL_CreateMutex();
    gCond = SDL_CreateCond();
    gQuit = false;
    gThread = SDL_CreateThreadWithStackSize(Worker, "frames", 1024 * 1024, nullptr);
}

void FrameWorkerStop() {
    if (!gThread) return;
    SDL_LockMutex(gLock);
    gQuit = true;
    SDL_CondSignal(gCond);
    SDL_UnlockMutex(gLock);
    SDL_WaitThread(gThread, nullptr);
    gThread = nullptr;
    SDL_DestroyCond(gCond);
    SDL_DestroyMutex(gLock);
}

void FrameWorkerRequest(const std::string &video, const std::string &outJpg) {
    if (!gThread) return;
    SDL_LockMutex(gLock);
    if (!gState.count(outJpg)) {
        gPendingVideo = video;
        gPendingOut = outJpg;
        SDL_CondSignal(gCond);
    }
    SDL_UnlockMutex(gLock);
}

int FrameWorkerState(const std::string &outJpg) {
    int st = 0;
    if (gLock) SDL_LockMutex(gLock);
    auto it = gState.find(outJpg);
    if (it != gState.end()) st = it->second;
    if (gLock) SDL_UnlockMutex(gLock);
    return st;
}
