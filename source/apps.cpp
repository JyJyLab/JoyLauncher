#include "apps.h"

#include "emulators.h"
#include "platform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <sys/stat.h>

namespace {
const char *kPrefix = "app://";

std::string Lower(std::string s) {
    for (auto &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

unsigned long long Fnv(const std::string &s) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char ch : s) h = (h ^ ch) * 1099511628211ull;
    return h;
}

Game AppGame(const plat::App &a) {
    Game g;
    g.title = a.label.empty() ? a.package : a.label;
    g.file = std::string(kPrefix) + a.package + "/" + a.activity;
    g.launch = kLaunchApp;
    g.developer = a.package;
    g.description = a.package + "/" + a.activity;
    g.sortKey = Lower(g.title);
    g.how = "앱";
    return g;
}

bool TitleLess(const Game &a, const Game &b) { return a.sortKey < b.sortKey; }
}  // namespace

std::string AppsKey(const std::string &file) { return file.compare(0, 6, kPrefix) == 0 ? file.substr(6) : std::string(); }

std::vector<Game> AppsScan() {
    std::vector<Game> out;
    for (const auto &a : plat::LaunchableApps()) out.push_back(AppGame(a));
    std::stable_sort(out.begin(), out.end(), TitleLess);
    return out;
}

Collection AppsFavorites(const std::vector<std::string> &favs, const std::string &cacheDir) {
    Collection c;
    c.name = "앱 즐겨찾기";
    c.sortBy = "001";
    c.shortName = "hbfav";  // 스위치판 테마의 hbfav 그림·조건을 그대로 쓰기 위해 같은 이름
    c.virt = true;
    if (favs.empty()) return c;
    std::map<std::string, plat::App> byKey;
    for (const auto &a : plat::LaunchableApps()) byKey[a.package + "/" + a.activity] = a;
    mkdir(cacheDir.c_str(), 0777);
    for (const auto &f : favs) {
        auto it = byKey.find(f);
        if (it == byKey.end()) continue;
        Game g = AppGame(it->second);
        char name[64];
        snprintf(name, sizeof(name), "app_%016llx.png", Fnv(f));
        std::string icon = cacheDir + "/" + name;
        struct stat st;
        if (stat(icon.c_str(), &st) == 0 || plat::SaveAppIcon(it->second.package, it->second.activity, icon, 256)) g.boxart = icon;
        c.games.push_back(g);
    }
    std::stable_sort(c.games.begin(), c.games.end(), TitleLess);
    return c;
}

bool AppsLaunch(const Game &g, std::string &err) {
    std::string key = AppsKey(g.file);
    size_t s = key.find('/');
    if (key.empty() || s == std::string::npos) {
        err = "app";
        return false;
    }
    return plat::StartApp(key.substr(0, s), key.substr(s + 1), err);
}
