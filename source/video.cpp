#include "video.h"

#include "platform.h"

#include <atomic>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace {

// 장면 한 장. I420: 밝기 w*h, 색 두 판 (w+1)/2 * (h+1)/2 씩 / RGBX: w*h*4
struct Buf {
    int w = 0, h = 0, token = 0;
    bool rgbx = false;
    std::vector<unsigned char> d;
};

// 푸는 스레드는 gBack 에 쓰고 다 쓰면 gReady 와 맞바꾼다. 그리는 쪽은 gReady 를 gFront 와 맞바꿔 읽는다.
// (복사하는 동안 서로를 기다리지 않게 석 장을 돌려 씀)
SDL_mutex *gLock = nullptr;       // gReady·gReadySerial
SDL_mutex *gWriteLock = nullptr;  // gBack (영상을 바꾸는 순간 옛 스레드와 새 스레드가 겹칠 수 있음)
Buf gBack, gReady, gFront;
unsigned gReadySerial = 0, gFrontSerial = 0;

std::atomic<int> gToken{0};       // 지금 재생 요청의 번호
std::atomic<int> gState{0};       // 0 없음 / 1 준비 중 / 2 재생 중 / -1 실패

bool gOn = true, gSound = true;
std::string gCur;                 // 재생을 요청한 영상 ("" = 없음)
std::string gWant;                // 이번 프레임에 보여 줄 자리가 있었던 영상
Uint32 gOpenAt = 0, gFirstAt = 0;
bool gHasFrame = false;           // gTex 에 gCur 의 장면이 올라가 있음
std::set<std::string> gFailed;    // 재생하지 못한 영상 (다시 시도하지 않음)

SDL_Texture *gTex = nullptr;
SDL_Renderer *gTexR = nullptr;
int gTexW = 0, gTexH = 0;
bool gTexRgbx = false;

const Uint32 kStartDelayMs = 300;   // 선택이 멈춘 뒤 재생을 시작하기까지 (빠르게 넘기는 동안은 열지 않음)
const Uint32 kNoFrameMs = 5000;     // 이 시간 안에 장면이 하나도 안 오면 실패로 침

void Locks() {
    if (!gLock) {
        gLock = SDL_CreateMutex();
        gWriteLock = SDL_CreateMutex();
        // 색 변환: 작은 영상은 BT.601, HD(세로 576 초과)는 BT.709 로
        SDL_SetYUVConversionMode(SDL_YUV_CONVERSION_AUTOMATIC);
    }
}

void Close() {
    if (gCur.empty()) return;
    gToken++;  // 이 뒤에 오는 옛 영상의 장면은 버린다
    plat::VideoClose();
    gCur.clear();
    gState = 0;
    gHasFrame = false;
}

void Open(const std::string &path) {
    Close();
    Locks();
    gCur = path;
    gHasFrame = false;
    gState = 1;
    gOpenAt = SDL_GetTicks();
    plat::VideoOpen(path, gSound, ++gToken);
}

// 새 장면이 와 있으면 그림으로 올린다
void Upload(SDL_Renderer *r) {
    bool fresh = false;
    SDL_LockMutex(gLock);
    if (gReadySerial != gFrontSerial) {
        std::swap(gFront, gReady);
        gFrontSerial = gReadySerial;
        fresh = true;
    }
    SDL_UnlockMutex(gLock);
    const Buf &b = gFront;
    if (!fresh || b.token != gToken.load() || b.w <= 0 || b.h <= 0) return;
    if (!gTex || gTexR != r || gTexW != b.w || gTexH != b.h || gTexRgbx != b.rgbx) {
        if (gTex) SDL_DestroyTexture(gTex);
        // RGBX: 메모리 차례가 R G B X 인 형식 (리틀 엔디언에서 XBGR8888)
        gTex = SDL_CreateTexture(r, b.rgbx ? SDL_PIXELFORMAT_BGR888 : SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, b.w, b.h);
        gTexR = r;
        gTexW = b.w;
        gTexH = b.h;
        gTexRgbx = b.rgbx;
        if (!gTex) return;
        SDL_SetTextureBlendMode(gTex, SDL_BLENDMODE_BLEND);  // 나타날 때 서서히 (불투명도)
        SDL_SetTextureScaleMode(gTex, SDL_ScaleModeLinear);
    }
    if (b.rgbx) {
        SDL_UpdateTexture(gTex, nullptr, b.d.data(), b.w * 4);
    } else {
        int cw = (b.w + 1) / 2, ch = (b.h + 1) / 2;
        const unsigned char *y = b.d.data(), *u = y + (size_t)b.w * b.h, *v = u + (size_t)cw * ch;
        SDL_UpdateYUVTexture(gTex, nullptr, y, b.w, u, cw, v, cw);
    }
    if (!gHasFrame) {
        gHasFrame = true;
        gFirstAt = SDL_GetTicks();
    }
}

// 다 쓴 장면을 그리는 쪽에 넘김 (gWriteLock 을 쥔 채로)
void Publish() {
    SDL_LockMutex(gLock);
    std::swap(gBack, gReady);
    gReadySerial++;
    SDL_UnlockMutex(gLock);
}

bool SinkOk(int token, int w, int h) { return gLock && token == gToken.load() && w > 0 && h > 0 && w <= 8192 && h <= 8192; }

}  // namespace

void VideoConfigure(bool on, bool sound) {
    if (on != gOn || sound != gSound) Close();  // 바뀐 설정으로 다시 시작
    gOn = on;
    gSound = sound;
}

bool VideoUsable(const std::string &path) { return gOn && !path.empty() && !gFailed.count(path); }

SDL_Texture *VideoWant(SDL_Renderer *r, const std::string &path, Uint32 *shownMs) {
    if (!gOn || path.empty() || gFailed.count(path)) return nullptr;
    gWant = path;
    if (path != gCur || gState.load() != 2) return nullptr;
    Upload(r);
    if (!gHasFrame || !gTex) return nullptr;
    if (shownMs) *shownMs = SDL_GetTicks() - gFirstAt;
    return gTex;
}

void VideoEndFrame(Uint32 settledMs, bool active) {
    std::string want;
    want.swap(gWant);
    if (!active || !gOn) want.clear();
    if (!gCur.empty()) {
        int st = gState.load();
        // 열지 못했거나, 재생한다는데 장면이 오지 않음 (그림이 없는 파일·읽을 수 없는 형식)
        if (st == -1 || (!gHasFrame && SDL_GetTicks() - gOpenAt > kNoFrameMs)) {
            gFailed.insert(gCur);
            Close();
        } else if (want != gCur || st == 0) {  // 0: 플랫폼 쪽에서 멈춤 (런처가 화면에서 물러남) → 필요하면 아래에서 다시 시작
            Close();
        }
    }
    if (gCur.empty() && !want.empty() && !gFailed.count(want) && settledMs >= kStartDelayMs) Open(want);
}

void VideoStop() {
    Close();
    gWant.clear();
}

void VideoResetGpu() {
    if (gTex) SDL_DestroyTexture(gTex);
    gTex = nullptr;
    gTexR = nullptr;
    gHasFrame = false;  // 다음 장면이 오면 다시 올림
    gOpenAt = SDL_GetTicks();
}

bool VideoWaitFrame(Uint32 ms) {
    Uint32 until = SDL_GetTicks() + ms;
    while (!gCur.empty() && gState.load() != -1 && SDL_GetTicks() < until) {
        SDL_LockMutex(gLock);
        bool ready = gReadySerial != gFrontSerial && gReady.token == gToken.load();
        SDL_UnlockMutex(gLock);
        if (ready) return true;
        SDL_Delay(5);
    }
    return false;
}

void VideoSinkState(int token, int state, bool) {
    if (token != gToken.load()) return;
    gState = state;
}

void VideoSinkYuv(int token, int w, int h, const unsigned char *y, int yStride, const unsigned char *u, const unsigned char *v,
                  int uvStride, int uvPixelStride) {
    if (!SinkOk(token, w, h) || !y || !u || !v) return;
    SDL_LockMutex(gWriteLock);
    Buf &b = gBack;
    int cw = (w + 1) / 2, ch = (h + 1) / 2;
    b.w = w;
    b.h = h;
    b.token = token;
    b.rgbx = false;
    b.d.resize((size_t)w * h + (size_t)cw * ch * 2);
    unsigned char *dy = b.d.data(), *du = dy + (size_t)w * h, *dv = du + (size_t)cw * ch;
    if (yStride == w) memcpy(dy, y, (size_t)w * h);
    else
        for (int r = 0; r < h; r++) memcpy(dy + (size_t)r * w, y + (size_t)r * yStride, w);
    for (int r = 0; r < ch; r++) {
        const unsigned char *su = u + (size_t)r * uvStride, *sv = v + (size_t)r * uvStride;
        unsigned char *ou = du + (size_t)r * cw, *ov = dv + (size_t)r * cw;
        if (uvPixelStride == 1) {
            memcpy(ou, su, cw);
            memcpy(ov, sv, cw);
        } else {
            for (int x = 0; x < cw; x++) {
                ou[x] = su[(size_t)x * uvPixelStride];
                ov[x] = sv[(size_t)x * uvPixelStride];
            }
        }
    }
    Publish();
    SDL_UnlockMutex(gWriteLock);
}

void VideoSinkRgbx(int token, int w, int h, const unsigned char *p, int stride) {
    if (!SinkOk(token, w, h) || !p) return;
    SDL_LockMutex(gWriteLock);
    Buf &b = gBack;
    b.w = w;
    b.h = h;
    b.token = token;
    b.rgbx = true;
    b.d.resize((size_t)w * h * 4);
    for (int r = 0; r < h; r++) memcpy(b.d.data() + (size_t)r * w * 4, p + (size_t)r * stride, (size_t)w * 4);
    Publish();
    SDL_UnlockMutex(gWriteLock);
}
