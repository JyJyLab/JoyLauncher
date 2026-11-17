// Joy Launcher 안드로이드 빌드 과정 스모크 테스트.
//
// 빌드 과정이 실제로 도는 APK 를 만드는지 에뮬레이터·기기에서 확인한다: SDL2 화면·소리·컨트롤러,
// SDL_ttf (APK 안의 글꼴로 한글 쓰기), SDL_image (PNG·JPG 읽기, PNG 쓰기), SDL_mixer (MP3·OGG),
// NDK 의 zlib.
//
// 단계마다 JOYSMOKE 태그로 기록하고, JOYSMOKE 의 마지막 줄은 "SMOKE OK" 또는 "SMOKE FAIL <이유>".
// 그 뒤의 입력 사건과 화면 갱신 확인 줄은 JOYSMOKE_EV 태그로 나간다:
//   adb logcat -d -s JOYSMOKE JOYSMOKE_EV
//
// 에셋 (android/smoketest/assets + romfs/font.ttf, build_smoke.sh 가 한 폴더에 모음):
//   font.ttf            런처 글꼴
//   test.png            64x64 RGBA, 테두리 8 px 투명, 네 칸이 빨강·초록·파랑·주황
//   test.jpg            64x64, 왼쪽 절반 (0,200,200), 오른쪽 절반 (200,0,200)
//   test.ogg, test.mp3  440 Hz 사인파 1초
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_mixer.h>
#include <SDL_ttf.h>

#include <android/log.h>
#include <sys/stat.h>
#include <zlib.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "?"
#endif

namespace {

const char *const kTag = "JOYSMOKE";
const char *const kEvTag = "JOYSMOKE_EV";
const int kW = 1280, kH = 720;  // 논리 크기, 런처와 같음
const char *const kTitle = "조이런처 Smoke OK";
const SDL_Rect kMarker{40, 628, 72, 72};  // 자홍색 네모. 저장한 PNG 에서 이 자리를 다시 찾아본다

struct Line {
    std::string text;
    bool ok;
};
struct Tex {
    SDL_Texture *t = nullptr;
    int w = 0, h = 0;
};

std::vector<Line> gLines;
std::string gFail;
std::string gLastInput = "no input yet";

__attribute__((format(printf, 2, 3))) bool step(bool ok, const char *fmt, ...) {
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ok ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR, kTag, "%s %s", ok ? "[ OK ]" : "[FAIL]", buf);
    gLines.push_back({buf, ok});
    if (!ok && gFail.empty()) gFail = buf;
    return ok;
}

__attribute__((format(printf, 1, 2))) void note(const char *fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, kEvTag, "%s", buf);
    gLastInput = buf;
}

SDL_Color pixelAt(SDL_Surface *s, int x, int y) {
    SDL_Color c{0, 0, 0, 0};
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) return c;
    const int bpp = s->format->BytesPerPixel;
    const Uint8 *p = static_cast<const Uint8 *>(s->pixels) + y * s->pitch + x * bpp;
    Uint32 v = 0;
    SDL_memcpy(&v, p, bpp);  // 리틀 엔디언 전용 (안드로이드 ABI 는 모두 그렇다)
    SDL_GetRGBA(v, s->format, &c.r, &c.g, &c.b, &c.a);
    return c;
}

bool nearColor(SDL_Color c, int r, int g, int b, int tol) {
    return abs(c.r - r) <= tol && abs(c.g - g) <= tol && abs(c.b - b) <= tol;
}

double msSince(Uint64 t0) {
    return (double)(SDL_GetPerformanceCounter() - t0) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

void freeTex(Tex &t) {
    if (t.t) SDL_DestroyTexture(t.t);
    t = Tex();
}

Tex textTex(SDL_Renderer *r, TTF_Font *f, const std::string &s, SDL_Color c) {
    Tex out;
    if (!r || !f || s.empty()) return out;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(f, s.c_str(), c);
    if (!surf) return out;
    out.t = SDL_CreateTextureFromSurface(r, surf);
    out.w = surf->w;
    out.h = surf->h;
    SDL_FreeSurface(surf);
    return out;
}

Tex surfaceTex(SDL_Renderer *r, SDL_Surface *s) {
    Tex out;
    if (!r || !s) return out;
    out.t = SDL_CreateTextureFromSurface(r, s);
    out.w = s->w;
    out.h = s->h;
    return out;
}

void drawTex(SDL_Renderer *r, const Tex &t, int x, int y, int w = 0, int h = 0) {
    if (!t.t) return;
    SDL_Rect d{x, y, w ? w : t.w, h ? h : t.h};
    SDL_RenderCopy(r, t.t, nullptr, &d);
}

void fill(SDL_Renderer *r, SDL_Rect rc, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderFillRect(r, &rc);
}

// 상대 경로면 SDL 은 앱 내부 저장소(files)를 먼저 보고 그다음 APK 의 assets 를 본다.
// SDL_ttf 는 글꼴이 열려 있는 동안 그 에셋 스트림에서 계속 읽는다.
TTF_Font *openAssetFont(const char *name, int px) {
    SDL_RWops *rw = SDL_RWFromFile(name, "rb");
    return rw ? TTF_OpenFontRW(rw, 1, px) : nullptr;
}

struct Scene {
    SDL_Renderer *ren = nullptr;
    TTF_Font *fontBig = nullptr, *fontSmall = nullptr;
    Tex title, png, jpg, pngLabel, jpgLabel, verdict, status;
    std::vector<Tex> lines;
    Uint32 frame = 0;

    // 줄 그림은 그릴 때 모자란 만큼 만든다: 화면의 목록이 그때까지의 기록과 늘 같다.
    void syncLines() {
        while (lines.size() < gLines.size()) {
            const Line &l = gLines[lines.size()];
            SDL_Color c = l.ok ? SDL_Color{120, 230, 140, 255} : SDL_Color{255, 90, 90, 255};
            lines.push_back(textTex(ren, fontSmall, std::string(l.ok ? "[ OK ] " : "[FAIL] ") + l.text, c));
        }
    }

    void draw() {
        syncLines();
        SDL_RenderSetClipRect(ren, nullptr);
        SDL_SetRenderDrawColor(ren, 16, 20, 40, 255);
        SDL_RenderClear(ren);
        drawTex(ren, title, 40, 14);

        SDL_Rect clip{40, 100, 910, 520};  // 긴 줄이 오른쪽 그림 칸을 덮지 않게
        SDL_RenderSetClipRect(ren, &clip);
        int y = 104;
        for (const Tex &l : lines) {
            drawTex(ren, l, 40, y);
            y += 23;
        }
        SDL_RenderSetClipRect(ren, nullptr);

        // PNG 는 밝은 판 위에 그린다: 투명한 테두리로 판 색이 비쳐야 한다.
        drawTex(ren, pngLabel, 980, 100);
        fill(ren, {980, 128, 216, 216}, 70, 80, 130);
        drawTex(ren, png, 992, 140, 192, 192);
        drawTex(ren, jpgLabel, 980, 360);
        drawTex(ren, jpg, 992, 388, 192, 192);

        fill(ren, kMarker, 255, 0, 255);
        drawTex(ren, verdict, 132, 622);
        drawTex(ren, status, 540, 668);

        // 움직이는 점: 화면이 계속 갱신되면 두 번 찍은 화면 사진이 서로 다르다.
        int x = 980 + (int)(frame * 4 % 216);
        fill(ren, {x, 600, 16, 16}, 255, 220, 60);
    }

    void present() {
        SDL_RenderPresent(ren);
        ++frame;
    }
};

Mix_Music *loadMusic(const char *name, Mix_MusicType want, const char *label) {
    Mix_Music *m = Mix_LoadMUS(name);
    if (m) {
        step(Mix_GetMusicType(m) == want, "Mix_LoadMUS(%s): decoded as %s (type %d, want %d), %.2f s", name, label,
             (int)Mix_GetMusicType(m), (int)want, Mix_MusicDuration(m));
    } else {
        step(false, "Mix_LoadMUS(%s): %s", name, Mix_GetError());
    }
    return m;
}

// 방금 그린 화면을 (내보내기 전에) 읽어 PNG 로 쓴다.
// rect 가 NULL 이면 "실제 출력 픽셀 단위의 뷰포트" 를 읽는데, 대부분의 기기에서 논리 크기
// 1280x720 보다 크다. 그래서 실제 출력 크기로 표면을 만들고 rect 를 직접 넘긴다.
bool saveFramePng(SDL_Renderer *ren, const std::string &path, int *w, int *h, std::string *err) {
    int ow = 0, oh = 0;
    if (SDL_GetRendererOutputSize(ren, &ow, &oh) != 0 || ow <= 0 || oh <= 0) {
        *err = SDL_GetError();
        return false;
    }
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) {
        *err = SDL_GetError();
        return false;
    }
    SDL_Rect full{0, 0, ow, oh};
    bool ok = SDL_RenderReadPixels(ren, &full, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0;
    if (!ok) *err = std::string("SDL_RenderReadPixels: ") + SDL_GetError();
    if (ok && IMG_SavePNG(s, path.c_str()) != 0) {
        *err = std::string("IMG_SavePNG: ") + IMG_GetError();
        ok = false;
    }
    SDL_FreeSurface(s);
    *w = ow;
    *h = oh;
    return ok;
}

}  // namespace

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    const SDL_Color white{255, 255, 255, 255}, grey{170, 176, 200, 255};
    Scene sc;

    // ---- SDL 기본
    bool sdlOk = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) == 0;
    SDL_version lv;
    SDL_GetVersion(&lv);
    step(sdlOk, "SDL_Init video+audio+gamecontroller: SDL %d.%d.%d, Android API %d, app %s%s%s", lv.major, lv.minor,
         lv.patch, SDL_GetAndroidSDKVersion(), APP_VERSION_STR, sdlOk ? "" : ": ", sdlOk ? "" : SDL_GetError());

    SDL_Window *win = SDL_CreateWindow("JoySmoke", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kW, kH,
                                       SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    sc.ren = ren;
    SDL_RendererInfo ri;
    SDL_zero(ri);
    int ow = 0, oh = 0;
    if (ren) {
        SDL_GetRendererInfo(ren, &ri);
        SDL_GetRendererOutputSize(ren, &ow, &oh);
    }
    bool renOk = ren && (ri.flags & SDL_RENDERER_ACCELERATED);
    step(renOk, "fullscreen window + accelerated renderer: %s, output %dx%d%s%s", ri.name ? ri.name : "none", ow, oh,
         renOk ? "" : ": ", renOk ? "" : SDL_GetError());
    step(ren && SDL_RenderSetLogicalSize(ren, kW, kH) == 0, "SDL_RenderSetLogicalSize(%d,%d)", kW, kH);
    step(zlibVersion() != nullptr && crc32(0L, reinterpret_cast<const Bytef *>("joy"), 3) == 0x71bd2914UL,
         "zlib %s from the NDK (crc32 check)", zlibVersion());

    // ---- SDL_image
    const int imgWant = IMG_INIT_PNG | IMG_INIT_JPG;
    const int imgGot = IMG_Init(imgWant);
    step((imgGot & imgWant) == imgWant, "IMG_Init PNG|JPG -> 0x%x", imgGot);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");  // 가장 가까운 점: 시험 그림의 칸 경계가 또렷하게
    if (SDL_Surface *s = IMG_Load("test.png")) {
        SDL_Color edge = pixelAt(s, 0, 0), red = pixelAt(s, 16, 16), orange = pixelAt(s, 47, 47);
        bool good = s->w == 64 && s->h == 64 && edge.a == 0 && red.a == 255 && nearColor(red, 255, 0, 0, 0) &&
                    nearColor(orange, 255, 128, 0, 0);
        step(good, "IMG_Load(test.png): %dx%d %s, corner a=%d, px %d,%d,%d / %d,%d,%d", s->w, s->h,
             SDL_GetPixelFormatName(s->format->format) + 16, edge.a, red.r, red.g, red.b, orange.r, orange.g, orange.b);
        sc.png = surfaceTex(ren, s);
        SDL_FreeSurface(s);
    } else {
        step(false, "IMG_Load(test.png): %s", IMG_GetError());
    }
    if (SDL_Surface *s = IMG_Load("test.jpg")) {
        SDL_Color l = pixelAt(s, 16, 32), r = pixelAt(s, 48, 32);
        bool good = s->w == 64 && s->h == 64 && nearColor(l, 0, 200, 200, 12) && nearColor(r, 200, 0, 200, 12);
        step(good, "IMG_Load(test.jpg): %dx%d %s, px %d,%d,%d / %d,%d,%d", s->w, s->h,
             SDL_GetPixelFormatName(s->format->format) + 16, l.r, l.g, l.b, r.r, r.g, r.b);
        sc.jpg = surfaceTex(ren, s);
        SDL_FreeSurface(s);
    } else {
        step(false, "IMG_Load(test.jpg): %s", IMG_GetError());
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");  // 글자는 720p 에서 화면 크기로 늘어나므로 부드럽게

    // ---- SDL_ttf, 글꼴은 APK 에셋에서 바로 읽는다
    bool ttfOk = TTF_Init() == 0;
    const SDL_version *tv = TTF_Linked_Version();
    int ftMaj = 0, ftMin = 0, ftPatch = 0, hbMaj = 0, hbMin = 0, hbPatch = 0;
    TTF_GetFreeTypeVersion(&ftMaj, &ftMin, &ftPatch);
    TTF_GetHarfBuzzVersion(&hbMaj, &hbMin, &hbPatch);
    step(ttfOk && hbMaj == 0, "TTF_Init: SDL_ttf %d.%d.%d, FreeType %d.%d.%d, HarfBuzz %s", tv->major, tv->minor, tv->patch,
         ftMaj, ftMin, ftPatch, hbMaj == 0 ? "off" : "ON (unexpected)");

    Uint64 t0 = SDL_GetPerformanceCounter();
    Sint64 fontBytes = -1;
    bool fromAsset = false;
    if (SDL_RWops *rw = SDL_RWFromFile("font.ttf", "rb")) {
        fontBytes = SDL_RWsize(rw);
        fromAsset = rw->type == SDL_RWOPS_JNIFILE;
        SDL_RWclose(rw);
    }
    step(fontBytes > 0 && fromAsset, "SDL_RWFromFile(\"font.ttf\"): %lld bytes, %s%s%s", (long long)fontBytes,
         fromAsset ? "APK asset" : "NOT an APK asset", fontBytes > 0 ? "" : ": ", fontBytes > 0 ? "" : SDL_GetError());
    sc.fontBig = openAssetFont("font.ttf", 60);
    sc.fontSmall = openAssetFont("font.ttf", 17);
    bool fontsOk = sc.fontBig && sc.fontSmall;
    step(fontsOk, "TTF_OpenFontRW(font.ttf from assets) at 60 px and 17 px%s%s", fontsOk ? "" : ": ", fontsOk ? "" : TTF_GetError());
    step(sc.fontBig && TTF_GlyphIsProvided32(sc.fontBig, 0xC870) && TTF_GlyphIsProvided32(sc.fontBig, 0xCC98),
         "font provides Hangul glyphs U+C870 U+CC98");
    sc.title = textTex(ren, sc.fontBig, kTitle, white);
    step(sc.title.t != nullptr, "TTF_RenderUTF8_Blended(\"%s\") -> %dx%d texture; font open+render %.1f ms", kTitle, sc.title.w,
         sc.title.h, msSince(t0));
    sc.pngLabel = textTex(ren, sc.fontSmall, "test.png (alpha border)", grey);
    sc.jpgLabel = textTex(ren, sc.fontSmall, "test.jpg", grey);

    // ---- SDL_mixer
    const int mixWant = MIX_INIT_MP3 | MIX_INIT_OGG;
    const int mixGot = Mix_Init(mixWant);
    step((mixGot & mixWant) == mixWant, "Mix_Init(MP3|OGG) -> 0x%x (want 0x%x)%s%s", mixGot, mixWant,
         (mixGot & mixWant) == mixWant ? "" : ": ", (mixGot & mixWant) == mixWant ? "" : Mix_GetError());
    bool audioOk = Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) == 0;  // 런처 bgm.cpp 와 같은 값
    int freq = 0, channels = 0;
    Uint16 fmt = 0;
    if (audioOk) Mix_QuerySpec(&freq, &fmt, &channels);
    const char *adrv = SDL_GetCurrentAudioDriver();
    step(audioOk, "Mix_OpenAudio: driver %s, %d Hz, %d ch, format 0x%04x%s%s", adrv ? adrv : "none", freq, channels, fmt,
         audioOk ? "" : ": ", audioOk ? "" : Mix_GetError());
    std::string music, chunk;
    for (int i = 0; i < Mix_GetNumMusicDecoders(); ++i) music += std::string(i ? " " : "") + Mix_GetMusicDecoder(i);
    for (int i = 0; i < Mix_GetNumChunkDecoders(); ++i) chunk += std::string(i ? " " : "") + Mix_GetChunkDecoder(i);
    step(Mix_HasMusicDecoder("MP3") && Mix_HasMusicDecoder("OGG"), "decoders: music [%s] chunk [%s]", music.c_str(), chunk.c_str());
    Mix_Music *ogg = loadMusic("test.ogg", MUS_OGG, "OGG");
    Mix_Music *mp3 = loadMusic("test.mp3", MUS_MP3, "MP3");
    Mix_VolumeMusic(MIX_MAX_VOLUME / 8);
    bool playing = mp3 && Mix_PlayMusic(mp3, -1) == 0 && Mix_PlayingMusic();
    step(playing, "Mix_PlayMusic(test.mp3, loop)%s%s", playing ? "" : ": ", playing ? "" : Mix_GetError());

    // ---- 화면이 실제로 갱신되는지
    if (ren) {
        Uint64 tf = SDL_GetPerformanceCounter();
        for (int i = 0; i < 30; ++i) {
            SDL_PumpEvents();
            sc.draw();
            sc.present();
        }
        // 창을 만든 직후의 출력 크기는 시스템 막대를 숨기기 전 값일 수 있다 (예: 1920x984).
        // 몇 프레임 뒤에는 전체 화면 크기로 바뀌어 있어야 한다.
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        step(true, "rendered 30 frames in %.0f ms, output now %dx%d", msSince(tf), ow, oh);
    } else {
        step(false, "no renderer, nothing rendered");
    }

    // ---- IMG_SavePNG 로 앱 내부 저장소에 쓰고 다시 읽어 본다
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (ren && internal) {
        const std::string path = std::string(internal) + "/smoke_out.png";
        remove(path.c_str());
        sc.draw();
        int sw = 0, sh = 0;
        std::string err;
        bool saved = saveFramePng(ren, path, &sw, &sh, &err);
        sc.present();
        struct stat st;
        long long bytes = (saved && stat(path.c_str(), &st) == 0) ? (long long)st.st_size : -1;
        step(saved && bytes > 0, "IMG_SavePNG(%s): %dx%d, %lld bytes%s%s", path.c_str(), sw, sh, bytes, saved ? "" : ": ", err.c_str());

        // 네모는 논리 좌표로 그렸다. 그 가운데가 실제 출력 픽셀로 어디인지 구한다.
        float sx = 1, sy = 1;
        SDL_Rect vp{0, 0, 0, 0};
        SDL_RenderGetScale(ren, &sx, &sy);
        SDL_RenderGetViewport(ren, &vp);
        int mx = (int)((vp.x + kMarker.x + kMarker.w / 2) * sx), my = (int)((vp.y + kMarker.y + kMarker.h / 2) * sy);
        SDL_Surface *back = saved ? IMG_Load(path.c_str()) : nullptr;
        SDL_Color m = pixelAt(back, mx, my);
        bool same = back && back->w == sw && back->h == sh && nearColor(m, 255, 0, 255, 2);
        step(same, "saved PNG reloads as %dx%d, marker pixel (%d,%d)=%d,%d,%d (want 255,0,255)", back ? back->w : 0,
             back ? back->h : 0, mx, my, m.r, m.g, m.b);
        if (back) SDL_FreeSurface(back);
    } else {
        step(false, "IMG_SavePNG skipped: %s", ren ? "no internal storage path" : "no renderer");
    }

    // ---- 판정: JOYSMOKE 의 마지막 줄
    const bool pass = gFail.empty();
    if (pass) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "SMOKE OK");
    } else {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "SMOKE FAIL %s", gFail.c_str());
    }
    sc.verdict = textTex(ren, sc.fontBig, pass ? "SMOKE OK" : "SMOKE FAIL", pass ? SDL_Color{120, 230, 140, 255} : SDL_Color{255, 90, 90, 255});

    // ---- 액티비티가 닫힐 때까지 계속 그린다
    bool running = ren != nullptr;
    std::string shown;
    Uint32 beatAt = 60;
    Uint64 tLoop = SDL_GetPerformanceCounter();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                    if (!e.key.repeat)
                        note("key down: %s (scancode %s)", SDL_GetKeyName(e.key.keysym.sym), SDL_GetScancodeName(e.key.keysym.scancode));
                    break;
                case SDL_CONTROLLERDEVICEADDED: {
                    SDL_GameController *gc = SDL_GameControllerOpen(e.cdevice.which);
                    note("controller added: %s", gc ? SDL_GameControllerName(gc) : SDL_GetError());
                    break;
                }
                case SDL_CONTROLLERDEVICEREMOVED:
                    note("controller removed (instance %d)", (int)e.cdevice.which);
                    break;
                case SDL_CONTROLLERBUTTONDOWN:
                    note("pad button down: %s", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button));
                    break;
                case SDL_CONTROLLERAXISMOTION:
                    if (abs(e.caxis.value) > 24000)
                        note("pad axis %s = %d", SDL_GameControllerGetStringForAxis((SDL_GameControllerAxis)e.caxis.axis), (int)e.caxis.value);
                    break;
                case SDL_FINGERDOWN:
                    note("touch down at %.2f,%.2f", e.tfinger.x, e.tfinger.y);
                    break;
                default:
                    break;
            }
        }
        if (sc.frame == beatAt) {
            __android_log_print(ANDROID_LOG_INFO, kEvTag, "render loop alive: frame %u, %.0f ms since the verdict", sc.frame, msSince(tLoop));
            beatAt += 600;
        }
        if (sc.frame % 30 == 0 || shown != gLastInput) {
            shown = gLastInput;
            freeTex(sc.status);
            sc.status = textTex(ren, sc.fontSmall, "frame " + std::to_string(sc.frame) + "   last input: " + shown, grey);
        }
        sc.draw();
        sc.present();
    }

    Mix_HaltMusic();
    if (ogg) Mix_FreeMusic(ogg);
    if (mp3) Mix_FreeMusic(mp3);
    Mix_CloseAudio();
    Mix_Quit();
    if (sc.fontBig) TTF_CloseFont(sc.fontBig);
    if (sc.fontSmall) TTF_CloseFont(sc.fontSmall);
    TTF_Quit();
    IMG_Quit();
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
    return pass ? 0 : 1;
}
