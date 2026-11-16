// 테마 빌더 공용: 파일·경로(UTF-8), 글 파일 모델(layout.txt / theme.txt), 그림 가져오기(크기 줄이기), zip 쓰기
#pragma once
#include <SDL.h>
#include <SDL_image.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ---------------------------------------------------------------- 경로·파일 (UTF-8 문자열 <-> path)
inline fs::path P(const std::string &u8) { return fs::u8path(u8); }
inline std::string U8(const fs::path &p) { return p.u8string(); }
inline std::string Lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    return s;
}
inline std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
inline bool Exists(const std::string &p) {
    std::error_code ec;
    return fs::exists(P(p), ec);
}
inline bool IsDir(const std::string &p) {
    std::error_code ec;
    return fs::is_directory(P(p), ec);
}
// SDL_RWops 로 읽기·쓰기 (윈도우에서도 UTF-8 경로가 됨)
inline bool ReadFile(const std::string &path, std::string &out) {
    out.clear();
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw) return false;
    Sint64 n = SDL_RWsize(rw);
    if (n > 0) {
        out.resize((size_t)n);
        size_t got = SDL_RWread(rw, &out[0], 1, (size_t)n);
        out.resize(got);
    }
    SDL_RWclose(rw);
    return true;
}
inline bool WriteFile(const std::string &path, const std::string &data) {
    std::error_code ec;
    fs::create_directories(P(path).parent_path(), ec);
    SDL_RWops *rw = SDL_RWFromFile(path.c_str(), "wb");
    if (!rw) return false;
    size_t w = data.empty() ? 0 : SDL_RWwrite(rw, data.data(), 1, data.size());
    SDL_RWclose(rw);
    return w == data.size();
}
inline std::vector<std::string> ListDir(const std::string &dir, bool dirs) {
    std::vector<std::string> v;
    std::error_code ec;
    for (fs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::string n = U8(it->path().filename());
        if (n.empty() || n[0] == '.') continue;
        if (it->is_directory(ec) == dirs) v.push_back(n);
    }
    std::sort(v.begin(), v.end(), [](const std::string &a, const std::string &b) { return Lower(a) < Lower(b); });
    return v;
}
inline std::string Ext(const std::string &n) {
    size_t d = n.find_last_of('.');
    return d == std::string::npos ? "" : Lower(n.substr(d + 1));
}
inline std::string Stem(const std::string &n) {
    size_t d = n.find_last_of('.');
    return d == std::string::npos ? n : n.substr(0, d);
}
inline bool IsImageName(const std::string &n) {
    std::string e = Ext(n);
    return e == "png" || e == "jpg" || e == "jpeg";
}
inline uintmax_t DirSize(const std::string &dir) {
    uintmax_t t = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) t += it->file_size(ec);
    return t;
}
inline std::string HumanSize(uintmax_t b) {
    char buf[32];
    if (b >= 1024 * 1024) snprintf(buf, sizeof(buf), "%.1f MB", b / 1048576.0);
    else snprintf(buf, sizeof(buf), "%.0f KB", b / 1024.0);
    return buf;
}
inline bool CopyTree(const std::string &src, const std::string &dst, std::string &err) {
    // fs::copy(recursive) 는 mingw 에서 기존 폴더로 복사가 실패해 직접 돈다
    std::error_code ec;
    fs::path s = P(src), d = P(dst);
    fs::create_directories(d, ec);
    for (fs::recursive_directory_iterator it(s, ec), end; !ec && it != end; it.increment(ec)) {
        fs::path rel = fs::relative(it->path(), s, ec), to = d / rel;
        if (it->is_directory(ec)) fs::create_directories(to, ec);
        else {
            fs::create_directories(to.parent_path(), ec);
            fs::copy_file(it->path(), to, fs::copy_options::overwrite_existing, ec);
        }
        if (ec) { err = ec.message() + ": " + U8(rel); return false; }
    }
    if (ec) { err = ec.message(); return false; }
    return true;
}

// ---------------------------------------------------------------- 글 파일 모델: layout.txt ([구역] + key = value, 설명 줄 보존)
struct IniKV { std::string key, val; bool raw = false; };  // raw: 설명·빈 줄 (val 에 원문)
struct IniSec { std::string name; std::vector<IniKV> kv; };
struct IniDoc {
    std::vector<std::string> head;  // 첫 구역 전의 줄
    std::vector<IniSec> secs;

    void Parse(const std::string &text) {
        head.clear();
        secs.clear();
        size_t pos = 0;
        bool first = true;
        while (pos <= text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
            first = false;
            std::string t = Trim(line);
            if (!t.empty() && t[0] == '[') {
                size_t c = t.find(']');
                secs.push_back({Lower(Trim(t.substr(1, c == std::string::npos ? std::string::npos : c - 1))), {}});
                continue;
            }
            size_t eq = t.find('=');
            bool isKv = !t.empty() && t[0] != '#' && t[0] != ';' && eq != std::string::npos;
            if (secs.empty()) { head.push_back(line); continue; }
            if (isKv) {
                std::string k = Trim(t.substr(0, eq)), v = Trim(t.substr(eq + 1));
                // [strings] 는 키 대소문자 그대로, 나머지는 소문자
                if (secs.back().name.rfind("strings", 0) != 0 && secs.back().name != "names") k = Lower(k);
                secs.back().kv.push_back({k, v, false});
            } else {
                secs.back().kv.push_back({"", line, true});
            }
        }
        // 끝의 빈 줄 정리
        for (auto &s : secs)
            while (!s.kv.empty() && s.kv.back().raw && Trim(s.kv.back().val).empty()) s.kv.pop_back();
        while (!head.empty() && Trim(head.back()).empty()) head.pop_back();
    }
    std::string Str() const {
        std::string o;
        for (const auto &h : head) o += h + "\n";
        if (!head.empty()) o += "\n";
        for (size_t i = 0; i < secs.size(); i++) {
            o += "[" + secs[i].name + "]\n";
            for (const auto &kv : secs[i].kv) o += kv.raw ? kv.val + "\n" : kv.key + " = " + kv.val + "\n";
            if (i + 1 < secs.size()) o += "\n";
        }
        return o;
    }
    IniSec *Find(const std::string &name) {
        for (auto &s : secs)
            if (s.name == name) return &s;
        return nullptr;
    }
    const IniSec *Find(const std::string &name) const {
        for (auto &s : secs)
            if (s.name == name) return &s;
        return nullptr;
    }
    bool Has(const std::string &sec, const std::string &key) const {
        const IniSec *s = Find(sec);
        if (!s) return false;
        for (auto &kv : s->kv)
            if (!kv.raw && kv.key == key) return true;
        return false;
    }
    std::string Get(const std::string &sec, const std::string &key, const std::string &def = "") const {
        const IniSec *s = Find(sec);
        if (!s) return def;
        for (auto &kv : s->kv)
            if (!kv.raw && kv.key == key) return kv.val;
        return def;
    }
    IniSec &Sec(const std::string &name) {
        if (IniSec *s = Find(name)) return *s;
        // 칸 안 요소는 목록 구역 바로 뒤(같은 목록의 다른 칸 요소 뒤)에 넣는다
        size_t at = secs.size();
        size_t d1 = name.find('.'), d2 = d1 == std::string::npos ? d1 : name.find('.', d1 + 1);
        if (d2 != std::string::npos) {
            std::string parent = name.substr(0, d2);
            for (size_t i = 0; i < secs.size(); i++)
                if (secs[i].name == parent || secs[i].name.rfind(parent + ".", 0) == 0) at = i + 1;
        }
        secs.insert(secs.begin() + at, IniSec{name, {}});
        return secs[at];
    }
    void Set(const std::string &sec, const std::string &key, const std::string &val) {
        IniSec &s = Sec(sec);
        for (auto &kv : s.kv)
            if (!kv.raw && kv.key == key) { kv.val = val; return; }
        // type 은 맨 앞에
        if (key == "type") s.kv.insert(s.kv.begin(), {key, val, false});
        else s.kv.push_back({key, val, false});
    }
    void Erase(const std::string &sec, const std::string &key) {
        IniSec *s = Find(sec);
        if (!s) return;
        s->kv.erase(std::remove_if(s->kv.begin(), s->kv.end(), [&](const IniKV &kv) { return !kv.raw && kv.key == key; }), s->kv.end());
    }
    void RemoveSec(const std::string &name) {
        secs.erase(std::remove_if(secs.begin(), secs.end(),
                                  [&](const IniSec &s) { return s.name == name || s.name.rfind(name + ".", 0) == 0; }),
                   secs.end());
    }
};

// theme.txt: 줄 단위 (key=value, 설명·[모둠] 줄 보존)
struct ThemeTxt {
    std::vector<std::string> lines;
    void Parse(const std::string &text) {
        lines.clear();
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (lines.empty() && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
            lines.push_back(line);
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    std::string Str() const {
        std::string o;
        for (const auto &l : lines) o += l + "\n";
        return o;
    }
    static bool Split(const std::string &line, std::string &k, std::string &v) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') return false;
        size_t eq = t.find('=');
        if (eq == std::string::npos) return false;
        k = Trim(t.substr(0, eq));
        v = Trim(t.substr(eq + 1));
        return true;
    }
    std::string Get(const std::string &key) const {
        std::string k, v;
        for (const auto &l : lines)
            if (Split(l, k, v) && Lower(k) == Lower(key)) return v;
        return "";
    }
    void Set(const std::string &key, const std::string &val) {
        std::string k, v;
        for (auto &l : lines)
            if (Split(l, k, v) && Lower(k) == Lower(key)) { l = k + "=" + val; return; }
        // name/name_en/dim 은 앞쪽에
        size_t at = lines.size();
        if (key == "name" || key == "name_en" || key == "dim") {
            at = 0;
            for (size_t i = 0; i < lines.size(); i++)
                if (Split(lines[i], k, v) && (Lower(k) == "name" || Lower(k) == "name_en" || Lower(k) == "dim")) at = i + 1;
        }
        lines.insert(lines.begin() + at, key + "=" + val);
    }
    void Erase(const std::string &key) {
        std::string k, v;
        lines.erase(std::remove_if(lines.begin(), lines.end(), [&](const std::string &l) { return Split(l, k, v) && Lower(k) == Lower(key); }),
                    lines.end());
    }
    // 이름 연결 (name/name_en/dim 제외)
    std::vector<std::pair<std::string, std::string>> Maps() const {
        std::vector<std::pair<std::string, std::string>> m;
        std::string k, v;
        for (const auto &l : lines)
            if (Split(l, k, v)) {
                std::string lk = Lower(k);
                if (lk != "name" && lk != "name_en" && lk != "dim") m.push_back({k, v});
            }
        return m;
    }
};

// ---------------------------------------------------------------- 그림 가져오기: 크기 줄이기 + PNG/JPG 저장
// 면적 평균으로 줄이기 (RGBA32)
inline SDL_Surface *ScaleDown(SDL_Surface *src, int dw, int dh) {
    SDL_Surface *s = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA32, 0);
    if (!s) return nullptr;
    if (dw >= s->w && dh >= s->h) return s;
    SDL_Surface *d = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_RGBA32);
    const Uint8 *sp = (const Uint8 *)s->pixels;
    Uint8 *dp = (Uint8 *)d->pixels;
    double fx = (double)s->w / dw, fy = (double)s->h / dh;
    for (int y = 0; y < dh; y++) {
        int y0 = (int)(y * fy), y1 = std::max(y0 + 1, std::min(s->h, (int)((y + 1) * fy)));
        for (int x = 0; x < dw; x++) {
            int x0 = (int)(x * fx), x1 = std::max(x0 + 1, std::min(s->w, (int)((x + 1) * fx)));
            double acc[4] = {0, 0, 0, 0}, wsum = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++) {
                    const Uint8 *p = sp + yy * s->pitch + xx * 4;
                    double a = p[3] / 255.0;  // 투명한 곳의 색이 번지지 않게 알파로 가중
                    acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += p[3];
                    wsum += a;
                }
            int n = (x1 - x0) * (y1 - y0);
            Uint8 *q = dp + y * d->pitch + x * 4;
            for (int i = 0; i < 3; i++) q[i] = wsum > 0 ? (Uint8)std::min(255.0, acc[i] / wsum + 0.5) : 0;
            q[3] = (Uint8)std::min(255.0, acc[3] / n + 0.5);
        }
    }
    SDL_FreeSurface(s);
    return d;
}
inline bool HasAlpha(SDL_Surface *s) {
    if (!s->format->Amask) return false;
    SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    if (!c) return true;
    bool a = false;
    for (int y = 0; y < c->h && !a; y++) {
        const Uint8 *p = (const Uint8 *)c->pixels + y * c->pitch;
        for (int x = 0; x < c->w; x++)
            if (p[x * 4 + 3] < 250) { a = true; break; }
    }
    SDL_FreeSurface(c);
    return a;
}
// 그림 종류별 최대 크기 (cover: 배경은 화면을 덮는 크기까지만 줄임)
struct ImgLimit { int w, h; bool cover; };
inline ImgLimit LimitFor(const std::string &kind) {
    std::string k = Lower(kind);
    if (k == "backgrounds") return {1280, 720, true};
    if (k == "logos") return {640, 200, false};
    if (k == "icons" || k == "controllers") return {512, 512, false};
    return {1280, 720, false};
}
// src 그림을 dstDir/stem.(png|jpg) 로 저장 (같은 이름의 다른 확장자는 지움). 결과 경로를 돌려줌
inline std::string ImportImage(const std::string &src, const std::string &dstDir, const std::string &stem, const std::string &kind,
                               std::string &err) {
    SDL_Surface *s = IMG_Load(src.c_str());
    if (!s) { err = IMG_GetError(); return ""; }
    ImgLimit lim = LimitFor(kind);
    double sc = lim.cover ? std::max((double)lim.w / s->w, (double)lim.h / s->h) : std::min((double)lim.w / s->w, (double)lim.h / s->h);
    sc = std::min(1.0, sc);
    int dw = std::max(1, (int)(s->w * sc + 0.5)), dh = std::max(1, (int)(s->h * sc + 0.5));
    bool alpha = HasAlpha(s);
    SDL_Surface *d = ScaleDown(s, dw, dh);
    SDL_FreeSurface(s);
    if (!d) { err = SDL_GetError(); return ""; }
    std::error_code ec;
    fs::create_directories(P(dstDir), ec);
    for (const char *e : {".png", ".jpg", ".jpeg", ".PNG", ".JPG"}) fs::remove(P(dstDir + "/" + stem + e), ec);
    std::string out = dstDir + "/" + stem + (alpha ? ".png" : ".jpg");
    int r = alpha ? IMG_SavePNG(d, out.c_str()) : IMG_SaveJPG(d, out.c_str(), 85);
    SDL_FreeSurface(d);
    if (r != 0) { err = IMG_GetError(); return ""; }
    return out;
}

// ---------------------------------------------------------------- zip (저장 방식, 압축 없음: 그림은 이미 압축돼 있음)
inline uint32_t Crc32(const std::string &d) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : d) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
// dir 안의 파일을 prefix/ 아래 이름으로 zip 에 담는다 (layout_errors.txt 제외)
inline bool WriteZip(const std::string &zipPath, const std::string &dir, const std::string &prefix, std::string &err) {
    std::string out, central;
    uint16_t count = 0;
    auto le16 = [](std::string &s, uint16_t v) { s += (char)(v & 0xFF); s += (char)(v >> 8); };
    auto le32 = [](std::string &s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)((v >> (i * 8)) & 0xFF); };
    std::error_code ec;
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec)) files.push_back(it->path());
    std::sort(files.begin(), files.end());
    for (const auto &f : files) {
        std::string rel = U8(fs::relative(f, P(dir), ec));
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (rel == "layout_errors.txt") continue;
        std::string name = prefix + "/" + rel, data;
        if (!ReadFile(U8(f), data)) continue;
        uint32_t crc = Crc32(data), off = (uint32_t)out.size();
        le32(out, 0x04034b50); le16(out, 20); le16(out, 0x0800); le16(out, 0); le16(out, 0); le16(out, 0x21);
        le32(out, crc); le32(out, (uint32_t)data.size()); le32(out, (uint32_t)data.size());
        le16(out, (uint16_t)name.size()); le16(out, 0);
        out += name;
        out += data;
        le32(central, 0x02014b50); le16(central, 20); le16(central, 20); le16(central, 0x0800); le16(central, 0); le16(central, 0);
        le16(central, 0x21); le32(central, crc); le32(central, (uint32_t)data.size()); le32(central, (uint32_t)data.size());
        le16(central, (uint16_t)name.size()); le16(central, 0); le16(central, 0); le16(central, 0); le16(central, 0);
        le32(central, 0); le32(central, off);
        central += name;
        count++;
    }
    uint32_t cdOff = (uint32_t)out.size();
    out += central;
    le32(out, 0x06054b50); le16(out, 0); le16(out, 0); le16(out, count); le16(out, count);
    le32(out, (uint32_t)central.size()); le32(out, cdOff); le16(out, 0);
    if (!WriteFile(zipPath, out)) { err = "write failed: " + zipPath; return false; }
    return true;
}
