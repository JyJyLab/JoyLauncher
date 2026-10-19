#include "bgm.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <random>
#include <strings.h>
#include <vector>

namespace {

std::vector<std::string> gSongs;
std::vector<int> gOrder;
size_t gPos = 0;
bool gOpen = false, gOn = false, gShuffle = false, gPaused = false;
int gVolume = 60;  // 0~100
Mix_Music *gMusic = nullptr;
Uint32 gFadeStart = 0;  // 페이드 인 시작 시각 (0 = 페이드 없음)
int gFailStreak = 0;    // 연속으로 열지 못한 곡 수 (모두 실패하면 멈춤)
const Uint32 kFadeInMs = 1000;

int MixVolume() { return gVolume * MIX_MAX_VOLUME / 100; }

void MakeOrder() {
    gOrder.resize(gSongs.size());
    for (size_t i = 0; i < gOrder.size(); i++) gOrder[i] = (int)i;
    if (gShuffle) {
        std::mt19937 rng((unsigned)SDL_GetTicks() ^ (unsigned)time(nullptr));
        std::shuffle(gOrder.begin(), gOrder.end(), rng);
    }
    gPos = 0;
}

void StartFade() {
    gFadeStart = SDL_GetTicks();
    if (gFadeStart == 0) gFadeStart = 1;
    Mix_VolumeMusic(0);
}

void FreeMusic() {
    if (!gMusic) return;
    Mix_HaltMusic();
    Mix_FreeMusic(gMusic);
    gMusic = nullptr;
}

void PlayNext() {
    FreeMusic();
    while (!gSongs.empty() && gFailStreak < (int)gSongs.size()) {
        if (gPos >= gOrder.size()) MakeOrder();
        const std::string &path = gSongs[gOrder[gPos++]];
        gMusic = Mix_LoadMUS(path.c_str());
        if (gMusic && Mix_PlayMusic(gMusic, 1) == 0) {
            gFailStreak = 0;
            StartFade();
            return;
        }
        printf("bgm: cannot play %s: %s\n", path.c_str(), Mix_GetError());
        if (gMusic) { Mix_FreeMusic(gMusic); gMusic = nullptr; }
        gFailStreak++;
    }
}

bool OpenAudio() {
    if (gOpen) return true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
    Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
    if (Mix_OpenAudio(48000, MIX_DEFAULT_FORMAT, 2, 4096) != 0) {
        printf("bgm: Mix_OpenAudio: %s\n", Mix_GetError());
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    gOpen = true;
    gFailStreak = 0;
    gPaused = false;
    MakeOrder();
    return true;
}

void CloseAudio() {
    if (!gOpen) return;
    FreeMusic();
    Mix_CloseAudio();
    Mix_Quit();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    gOpen = false;
    gFadeStart = 0;
}

bool HasExt(const std::string &n, const char *ext) {
    size_t l = strlen(ext);
    return n.size() > l && strcasecmp(n.c_str() + n.size() - l, ext) == 0;
}

}  // namespace

void BgmScan(const std::string &dir) {
    gSongs.clear();
    if (DIR *d = opendir(dir.c_str())) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.empty() || n[0] == '.') continue;
            if (HasExt(n, ".mp3") || HasExt(n, ".ogg")) gSongs.push_back(dir + "/" + n);
        }
        closedir(d);
    }
    std::sort(gSongs.begin(), gSongs.end());
}

int BgmCount() { return (int)gSongs.size(); }

void BgmConfigure(bool on, int volume, bool shuffle) {
    gVolume = std::max(0, std::min(100, volume));
    bool reorder = shuffle != gShuffle;
    gShuffle = shuffle;
    gOn = on && !gSongs.empty();
    if (!gOn) { CloseAudio(); return; }
    if (!gOpen) {
        if (!OpenAudio()) return;
        PlayNext();
        return;
    }
    if (reorder) MakeOrder();  // 다음 곡부터 새 순서
    if (!gFadeStart) Mix_VolumeMusic(MixVolume());
}

void BgmUpdate() {
    if (!gOpen || gPaused) return;
    if (!Mix_PlayingMusic()) {
        if (gFailStreak < (int)gSongs.size()) PlayNext();
        return;
    }
    if (gFadeStart) {
        Uint32 t = SDL_GetTicks() - gFadeStart;
        if (t >= kFadeInMs) {
            gFadeStart = 0;
            Mix_VolumeMusic(MixVolume());
        } else {
            Mix_VolumeMusic((int)(MixVolume() * (t / (float)kFadeInMs)));
        }
    }
}

void BgmSkip() {
    if (!gOpen) return;
    gPaused = false;
    gFailStreak = 0;
    PlayNext();
}

void BgmPause() {
    if (!gOpen || gPaused) return;
    gPaused = true;
    Mix_PauseMusic();
}

void BgmResume() {
    if (!gOpen) return;
    gPaused = false;
    StartFade();  // 갑자기 큰 소리가 나지 않게
    Mix_ResumeMusic();
}

void BgmShutdown(int fadeMs) {
    if (!gOpen) return;
    if (fadeMs > 0 && !gPaused && Mix_PlayingMusic()) {
        Mix_FadeOutMusic(fadeMs);
        Uint32 until = SDL_GetTicks() + fadeMs + 200;
        while (Mix_PlayingMusic() && SDL_GetTicks() < until) SDL_Delay(10);
    }
    CloseAudio();
}
