#include "overlay.h"

#include <SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>

namespace {

// 터치 이벤트의 원래 좌표. 렌더러에 논리 크기(1280x720)를 주면 SDL 이 이벤트의 좌표를 그 안으로 바꾸고
// 바깥(화면 비율이 달라 생긴 양옆)은 가장자리로 잘라 버린다. 화면 컨트롤러는 화면 전체를 쓰므로
// 그 전에(렌더러보다 먼저 등록한 감시 함수에서) 원래 좌표를 적어 둔다. 감시 함수는 이벤트를 넣는 스레드
// (안드로이드는 화면 스레드)에서 불리므로 잠금이 필요하다
struct RawTouch {
    Uint32 type, timestamp;
    SDL_TouchID touch;
    SDL_FingerID finger;
    float x, y;
};
SDL_mutex *gRawLock = nullptr;
std::deque<RawTouch> gRaw;

int SDLCALL RawTouchWatch(void *, SDL_Event *e) {
    if (e->type != SDL_FINGERDOWN && e->type != SDL_FINGERMOTION) return 0;
    SDL_LockMutex(gRawLock);
    gRaw.push_back({e->type, e->tfinger.timestamp, e->tfinger.touchId, e->tfinger.fingerId, e->tfinger.x, e->tfinger.y});
    if (gRaw.size() > 512) gRaw.pop_front();  // 아무도 읽지 않는 동안(진행 화면 등)
    SDL_UnlockMutex(gRawLock);
    return 0;
}

struct Btn {
    int id;             // 런처 버튼 번호
    const char *label, *alt;  // alt: START/SELECT 표시일 때 글자
    SDL_FRect rc;       // 화면 좌표
    bool round;         // 동그라미 (아니면 둥근 막대)
    int group;          // 1 = 방향키 (손가락을 밀어 옮길 수 있음)
};

SDL_Renderer *gR = nullptr;
const std::vector<char> *gFontData = nullptr;
std::vector<Btn> gBtns;
int gW = 0, gH = 0;
float gUnit = 0;
std::map<SDL_FingerID, int> gFinger;   // 손가락 → gBtns 번호
std::map<std::string, SDL_Texture *> gTex;
std::map<int, TTF_Font *> gFonts;

void FreeTextures() {
    for (auto &kv : gTex)
        if (kv.second) SDL_DestroyTexture(kv.second);
    gTex.clear();
}

TTF_Font *Font(int size) {
    auto it = gFonts.find(size);
    if (it != gFonts.end()) return it->second;
    TTF_Font *f = gFontData && !gFontData->empty()
                      ? TTF_OpenFontRW(SDL_RWFromConstMem(gFontData->data(), (int)gFontData->size()), 1, size)
                      : nullptr;
    gFonts[size] = f;
    return f;
}

// 흰 동그라미·둥근 막대 (가장자리 부드럽게). 색·투명도는 그릴 때 입힌다
SDL_Texture *Shape(int w, int h) {
    std::string key = "s" + std::to_string(w) + "x" + std::to_string(h);
    auto it = gTex.find(key);
    if (it != gTex.end()) return it->second;
    SDL_Texture *t = nullptr;
    if (SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32)) {
        float r = std::min(w, h) / 2.0f;
        for (int y = 0; y < h; y++) {
            Uint8 *row = (Uint8 *)sf->pixels + y * sf->pitch;
            for (int x = 0; x < w; x++) {
                int in = 0;
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                        float cx = std::min(std::max(px, r), w - r), cy = std::min(std::max(py, r), h - r);
                        float dx = px - cx, dy = py - cy;
                        if (dx * dx + dy * dy <= r * r) in++;
                    }
                row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
                row[x * 4 + 3] = (Uint8)(255 * in / 16);
            }
        }
        t = SDL_CreateTextureFromSurface(gR, sf);
        SDL_FreeSurface(sf);
        if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    gTex[key] = t;
    return t;
}

SDL_Texture *Label(const std::string &text, int size) {
    std::string key = "t" + std::to_string(size) + "|" + text;
    auto it = gTex.find(key);
    if (it != gTex.end()) return it->second;
    SDL_Texture *t = nullptr;
    if (TTF_Font *f = Font(size))
        if (SDL_Surface *sf = TTF_RenderUTF8_Blended(f, text.c_str(), SDL_Color{255, 255, 255, 255})) {
            t = SDL_CreateTextureFromSurface(gR, sf);
            SDL_FreeSurface(sf);
        }
    gTex[key] = t;
    return t;
}

void Layout() {
    gBtns.clear();
    if (!gR) return;
    SDL_GetRendererOutputSize(gR, &gW, &gH);
    if (gW <= 0 || gH <= 0) return;
    // 버튼 지름: 약 9 mm (손가락 크기). 화면 높이의 11 ~ 17 % 안에서
    float dpi = 0;
    if (SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) != 0) dpi = 0;
    float u = dpi > 0 ? dpi * 0.355f : gH * 0.13f;
    u = std::max(gH * 0.11f, std::min(gH * 0.17f, u));
    u = std::min(u, gW * 0.11f);  // 좁은 화면: 양쪽 묶음이 겹치지 않게
    gUnit = u;
    float m = u * 0.35f, step = u * 1.03f;
    float lx = m + u * 1.55f, rx = gW - m - u * 1.55f, cy = gH - m - u * 1.55f;
    auto round = [&](int id, const char *label, float cx, float cyy, int group) {
        gBtns.push_back({id, label, nullptr, {cx - u / 2, cyy - u / 2, u, u}, true, group});
    };
    round(12, "◀", lx - step, cy, 1);
    round(14, "▶", lx + step, cy, 1);
    round(13, "▲", lx, cy - step, 1);
    round(15, "▼", lx, cy + step, 1);
    // 닌텐도 배치: A 오른쪽, B 아래, X 위, Y 왼쪽
    round(0, "A", rx + step, cy, 0);
    round(1, "B", rx, cy + step, 0);
    round(2, "X", rx, cy - step, 0);
    round(3, "Y", rx - step, cy, 0);
    float pw = u * 1.45f, ph = u * 0.72f, top = cy - step - u / 2 - m * 0.8f - ph;
    gBtns.push_back({6, "L", nullptr, {m, top, pw, ph}, false, 0});
    gBtns.push_back({8, "ZL", nullptr, {m + pw + m * 0.6f, top, pw, ph}, false, 0});
    gBtns.push_back({7, "R", nullptr, {gW - m - pw, top, pw, ph}, false, 0});
    gBtns.push_back({9, "ZR", nullptr, {gW - m - pw * 2 - m * 0.6f, top, pw, ph}, false, 0});
    float sw = u * 1.5f, sh = u * 0.6f, sy = gH - m * 0.7f - sh, gap = u * 0.25f;
    gBtns.push_back({11, "−", "SELECT", {gW / 2.0f - gap / 2 - sw, sy, sw, sh}, false, 0});
    gBtns.push_back({10, "+", "START", {gW / 2.0f + gap / 2, sy, sw, sh}, false, 0});
}

// 화면 좌표 (x, y) 에 있는 버튼 (-1 = 없음). 조금 넉넉하게 잡는다
int Hit(float x, float y, int onlyGroup = -1) {
    int best = -1;
    float bestD = 1e9f;
    for (size_t i = 0; i < gBtns.size(); i++) {
        const Btn &b = gBtns[i];
        if (onlyGroup >= 0 && b.group != onlyGroup) continue;
        float cx = b.rc.x + b.rc.w / 2, cyy = b.rc.y + b.rc.h / 2;
        float pad = gUnit * 0.13f;
        bool in;
        if (b.round) {
            float rr = b.rc.w / 2 + pad;
            in = (x - cx) * (x - cx) + (y - cyy) * (y - cyy) <= rr * rr;
        } else {
            in = x >= b.rc.x - pad && x <= b.rc.x + b.rc.w + pad && y >= b.rc.y - pad && y <= b.rc.y + b.rc.h + pad;
        }
        if (!in) continue;
        float d = (x - cx) * (x - cx) + (y - cyy) * (y - cyy);
        if (d < bestD) { bestD = d; best = (int)i; }
    }
    return best;
}

// 손가락의 실제 화면 좌표: 그 이벤트의 원래 좌표 (이벤트의 좌표는 논리 크기에 맞춰 잘려 있어서 쓰지 않음).
// 지금 손가락 위치를 물어보면 안 된다 — 짧게 톡 친 터치는 이벤트를 처리할 때 이미 떼어져 있어 눌림을 놓친다
bool FingerPos(const SDL_TouchFingerEvent &tf, float &x, float &y) {
    if (gRawLock) {
        bool found = false;
        SDL_LockMutex(gRawLock);
        // 이벤트는 적어 둔 차례대로 온다: 앞에서부터 찾고, 그보다 앞의 것(런처가 읽지 않은 이벤트)은 버린다
        for (size_t i = 0; i < gRaw.size(); i++) {
            const RawTouch &r = gRaw[i];
            if (r.type == tf.type && r.timestamp == tf.timestamp && r.touch == tf.touchId && r.finger == tf.fingerId) {
                x = r.x * gW;
                y = r.y * gH;
                found = true;
                gRaw.erase(gRaw.begin(), gRaw.begin() + i + 1);
                break;
            }
        }
        SDL_UnlockMutex(gRawLock);
        if (found) return true;
    }
    int n = SDL_GetNumTouchFingers(tf.touchId);
    for (int i = 0; i < n; i++) {
        SDL_Finger *f = SDL_GetTouchFinger(tf.touchId, i);
        if (f && f->id == tf.fingerId) {
            x = f->x * gW;
            y = f->y * gH;
            return true;
        }
    }
    return false;
}

bool Pressed(int idx) {
    for (const auto &kv : gFinger)
        if (kv.second == idx) return true;
    return false;
}
}  // namespace

void OverlayWatchTouches() {
    if (gRawLock) return;
    gRawLock = SDL_CreateMutex();
    SDL_AddEventWatch(RawTouchWatch, nullptr);
}

void OverlayInit(SDL_Renderer *r, const std::vector<char> *bold) {
    gR = r;
    gFontData = bold;
    Layout();
}

void OverlayShutdown() {
    FreeTextures();
    for (auto &kv : gFonts)
        if (kv.second) TTF_CloseFont(kv.second);
    gFonts.clear();
    gBtns.clear();
    gFinger.clear();
    gR = nullptr;
}

void OverlayReset() {
    FreeTextures();
    gFinger.clear();
    Layout();
}

void OverlayReleaseAll(std::vector<OverlayPress> &out) {
    for (const auto &kv : gFinger)
        if (kv.second >= 0 && kv.second < (int)gBtns.size()) out.push_back({gBtns[kv.second].id, false});
    gFinger.clear();
}

bool OverlayEvent(const SDL_Event &e, bool active, std::vector<OverlayPress> &out) {
    if (e.type != SDL_FINGERDOWN && e.type != SDL_FINGERUP && e.type != SDL_FINGERMOTION) return false;
    const SDL_TouchFingerEvent &tf = e.tfinger;
    if (e.type == SDL_FINGERUP) {
        auto it = gFinger.find(tf.fingerId);
        if (it != gFinger.end()) {
            if (it->second >= 0 && it->second < (int)gBtns.size()) out.push_back({gBtns[it->second].id, false});
            gFinger.erase(it);
        }
        return true;
    }
    if (!active || gBtns.empty()) return true;
    float x, y;
    if (!FingerPos(tf, x, y)) return true;
    auto it = gFinger.find(tf.fingerId);
    if (e.type == SDL_FINGERDOWN) {
        int h = Hit(x, y);
        if (h < 0) return true;
        if (it != gFinger.end() && it->second >= 0) out.push_back({gBtns[it->second].id, false});
        gFinger[tf.fingerId] = h;
        out.push_back({gBtns[h].id, true});
        return true;
    }
    // 움직임: 방향키 위에서 민 손가락은 다른 방향으로 옮겨 간다
    if (it == gFinger.end() || it->second < 0 || gBtns[it->second].group != 1) return true;
    int h = Hit(x, y, 1);
    if (h >= 0 && h != it->second) {
        out.push_back({gBtns[it->second].id, false});
        it->second = h;
        out.push_back({gBtns[h].id, true});
    }
    return true;
}

void OverlayDraw(SDL_Renderer *r, bool startSelect) {
    if (!r) return;
    if (r != gR) { gR = r; OverlayReset(); }
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    if (w != gW || h != gH || gBtns.empty()) OverlayReset();
    if (gBtns.empty()) return;
    // 런처 화면의 논리 크기(1280x720)를 잠시 풀고 실제 화면 좌표로
    int lw, lh;
    SDL_RenderGetLogicalSize(r, &lw, &lh);
    if (lw || lh) SDL_RenderSetLogicalSize(r, 0, 0);
    SDL_RenderSetViewport(r, nullptr);
    SDL_RenderSetScale(r, 1.0f, 1.0f);
    for (size_t i = 0; i < gBtns.size(); i++) {
        const Btn &b = gBtns[i];
        bool down = Pressed((int)i);
        SDL_Rect d = {(int)lroundf(b.rc.x), (int)lroundf(b.rc.y), (int)lroundf(b.rc.w), (int)lroundf(b.rc.h)};
        // 바탕: 어두운 반투명 + 밝은 테두리 느낌 (밝은 화면·어두운 화면 모두에서 보이게 두 겹)
        if (SDL_Texture *s = Shape(d.w, d.h)) {
            SDL_SetTextureColorMod(s, 20, 20, 26);
            SDL_SetTextureAlphaMod(s, down ? 150 : 72);
            SDL_RenderCopy(r, s, nullptr, &d);
            int inset = std::max(2, (int)(gUnit * 0.045f));
            SDL_Rect in = {d.x + inset, d.y + inset, d.w - inset * 2, d.h - inset * 2};
            if (SDL_Texture *s2 = Shape(in.w, in.h)) {
                if (down) SDL_SetTextureColorMod(s2, 255, 196, 64);
                else SDL_SetTextureColorMod(s2, 235, 235, 240);
                SDL_SetTextureAlphaMod(s2, down ? 215 : 46);
                SDL_RenderCopy(r, s2, nullptr, &in);
            }
        }
        const char *text = startSelect && b.alt ? b.alt : b.label;
        bool word = startSelect && b.alt;
        int fs = (int)(b.rc.h * (b.round ? 0.5f : word ? 0.42f : 0.58f));
        if (!word && (b.id == 10 || b.id == 11)) {
            // + · − 는 직접 그린다 (글꼴에 − 글자가 없을 수 있음)
            int len = (int)(b.rc.h * 0.42f), th = std::max(3, (int)(b.rc.h * 0.11f));
            int cx = d.x + d.w / 2, cy = d.y + d.h / 2;
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            if (down) SDL_SetRenderDrawColor(r, 20, 20, 26, 255);
            else SDL_SetRenderDrawColor(r, 255, 255, 255, 175);
            SDL_Rect bar = {cx - len / 2, cy - th / 2, len, th};
            SDL_RenderFillRect(r, &bar);
            if (b.id == 10) {
                SDL_Rect up = {cx - th / 2, cy - len / 2, th, len / 2 - th / 2}, dn = {cx - th / 2, cy + th - th / 2, th, len / 2 - th / 2};
                SDL_RenderFillRect(r, &up);
                SDL_RenderFillRect(r, &dn);
            }
        } else if (SDL_Texture *t = Label(text, std::max(10, fs))) {
            int tw, th;
            SDL_QueryTexture(t, nullptr, nullptr, &tw, &th);
            SDL_Rect td = {d.x + (d.w - tw) / 2, d.y + (d.h - th) / 2, tw, th};
            SDL_SetTextureColorMod(t, down ? 20 : 255, down ? 20 : 255, down ? 26 : 255);
            SDL_SetTextureAlphaMod(t, down ? 255 : 175);
            SDL_RenderCopy(r, t, nullptr, &td);
        }
    }
    if (lw || lh) SDL_RenderSetLogicalSize(r, lw, lh);
}
