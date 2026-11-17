// 기종 ↔ 에뮬레이터 매칭 (emulators.h 설명 참고)
#include "emulators.h"

#include "esde.h"
#include "i18n.h"
#include "platform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/stat.h>

namespace {

// ---------------------------------------------------------------- 글자 도구
std::string Lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
std::string Base(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? p : p.substr(s + 1);
}
std::string ParentDir(const std::string &p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos || s == 0 ? "" : p.substr(0, s);
}
std::string Stem(const std::string &p) {
    std::string b = Base(p);
    size_t d = b.find_last_of('.');
    return d == std::string::npos || d == 0 ? b : b.substr(0, d);
}
std::string Ext(const std::string &p) {
    std::string b = Base(p);
    size_t d = b.find_last_of('.');
    return d == std::string::npos ? "" : Lower(b.substr(d + 1));
}
// 영문·숫자만 남긴 소문자 ("Play Station 2" → "playstation2")
std::string FolderKey(const std::string &name) {
    std::string k;
    for (char c : Lower(name))
        if (std::isalnum((unsigned char)c)) k += c;
    return k;
}
void ReplaceAll(std::string &s, const std::string &from, const std::string &to) {
    if (from.empty()) return;
    for (size_t p = s.find(from); p != std::string::npos; p = s.find(from, p + to.size())) s.replace(p, from.size(), to);
}
bool StartsWith(const std::string &s, const std::string &pre) { return s.compare(0, pre.size(), pre) == 0; }
bool FileExists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}
// 파일 내용을 한 줄로 (ES-DE 의 %INJECT%: 줄바꿈 없이 이어 붙임, 4096 바이트까지)
bool ReadInject(const std::string &path, std::string &out) {
    std::ifstream in(path);
    if (!in) return false;
    out.clear();
    for (std::string line; std::getline(in, line);) {
        ReplaceAll(line, "\r", "");
        out += line;
        if (out.size() > 4096) return false;
    }
    return !out.empty();
}

// ---------------------------------------------------------------- 표
std::vector<EmuSystem> gSystems;
std::map<std::string, size_t> gByName;
std::map<std::string, std::vector<std::string>> gRules;  // 에뮬레이터 이름 → "패키지/액티비티" 들
std::vector<std::pair<std::string, std::string>> gUser;  // emulators.txt
std::string gUserFile, gSig;
std::set<std::string> gKnownCores;
std::map<std::string, bool> gInstalled;  // "패키지/액티비티" → 설치 여부 (화면에 돌아올 때 비움)

// <!-- … --> 빼기
std::string StripComments(const std::string &x) {
    std::string o;
    size_t p = 0;
    while (true) {
        size_t a = x.find("<!--", p);
        if (a == std::string::npos) { o += x.substr(p); break; }
        o += x.substr(p, a - p);
        size_t b = x.find("-->", a + 4);
        if (b == std::string::npos) break;
        p = b + 3;
    }
    return o;
}
std::string XmlUnescape(std::string s) {
    static const std::pair<const char *, const char *> ents[] = {
        {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&#39;", "'"}, {"&amp;", "&"}};
    for (const auto &e : ents) ReplaceAll(s, e.first, e.second);
    return s;
}
std::string XmlTag(const std::string &blk, const char *tag) {
    std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
    size_t a = blk.find(open);
    if (a == std::string::npos) return "";
    a += open.size();
    size_t b = blk.find(close, a);
    return b == std::string::npos ? "" : Trim(XmlUnescape(blk.substr(a, b - a)));
}
std::string XmlAttr(const std::string &tagText, const char *attr) {
    std::string k = std::string(attr) + "=\"";
    size_t a = tagText.find(k);
    if (a == std::string::npos) return "";
    a += k.size();
    size_t b = tagText.find('"', a);
    return b == std::string::npos ? "" : XmlUnescape(tagText.substr(a, b - a));
}

// 명령에서 %EMULATOR_X% 의 X 와 RetroArch 코어 이름
void AnalyzeCommand(EmuCommand &c) {
    size_t e = c.command.find("%EMULATOR_");
    if (e != std::string::npos) {
        size_t end = c.command.find('%', e + 10);
        if (end != std::string::npos) c.emulator = c.command.substr(e + 10, end - e - 10);
    }
    size_t l = c.command.find("_libretro");
    if (l != std::string::npos) {
        size_t b = c.command.rfind('/', l);
        if (b != std::string::npos) c.core = c.command.substr(b + 1, l - b - 1);
    }
}

// es_systems.xml 한 개 (뒤에 읽은 것이 같은 이름을 덮어씀)
int ParseSystems(const std::string &xmlRaw) {
    std::string xml = StripComments(xmlRaw);
    int n = 0;
    size_t pos = 0;
    while ((pos = xml.find("<system>", pos)) != std::string::npos) {
        size_t end = xml.find("</system>", pos);
        if (end == std::string::npos) break;
        std::string blk = xml.substr(pos, end - pos);
        pos = end + 9;
        EmuSystem s;
        s.name = Lower(XmlTag(blk, "name"));
        if (s.name.empty()) continue;
        s.fullname = XmlTag(blk, "fullname");
        s.theme = XmlTag(blk, "theme");
        std::stringstream es(XmlTag(blk, "extension"));
        for (std::string e; es >> e;) {
            if (!e.empty() && e[0] == '.') e.erase(0, 1);
            e = Lower(e);
            if (!e.empty() && std::find(s.exts.begin(), s.exts.end(), e) == s.exts.end()) s.exts.push_back(e);
        }
        size_t cp = 0;
        while ((cp = blk.find("<command", cp)) != std::string::npos) {
            size_t gt = blk.find('>', cp), ce = blk.find("</command>", cp);
            if (gt == std::string::npos || ce == std::string::npos || gt > ce) break;
            EmuCommand c;
            c.label = XmlAttr(blk.substr(cp, gt - cp), "label");
            c.command = Trim(XmlUnescape(blk.substr(gt + 1, ce - gt - 1)));
            cp = ce + 10;
            if (c.command.empty() || StartsWith(c.command, "PLACEHOLDER")) continue;  // ES-DE 가 아직 지원하지 않는 기종
            AnalyzeCommand(c);
            s.commands.push_back(c);
        }
        auto it = gByName.find(s.name);
        if (it != gByName.end()) gSystems[it->second] = s;
        else {
            gByName[s.name] = gSystems.size();
            gSystems.push_back(s);
        }
        n++;
    }
    return n;
}

// es_find_rules.xml 한 개
int ParseRules(const std::string &xmlRaw) {
    std::string xml = StripComments(xmlRaw);
    int n = 0;
    size_t pos = 0;
    while ((pos = xml.find("<emulator", pos)) != std::string::npos) {
        size_t gt = xml.find('>', pos), end = xml.find("</emulator>", pos);
        if (gt == std::string::npos || end == std::string::npos) break;
        std::string name = XmlAttr(xml.substr(pos, gt - pos), "name");
        std::string blk = xml.substr(gt, end - gt);
        pos = end + 11;
        if (name.empty()) continue;
        std::vector<std::string> entries;
        size_t rp = 0;
        while ((rp = blk.find("<rule", rp)) != std::string::npos) {
            size_t rgt = blk.find('>', rp), rend = blk.find("</rule>", rp);
            if (rgt == std::string::npos || rend == std::string::npos) break;
            bool pkg = XmlAttr(blk.substr(rp, rgt - rp), "type") == "androidpackage";
            std::string rb = blk.substr(rgt, rend - rgt);
            rp = rend + 7;
            if (!pkg) continue;
            size_t ep = 0;
            while ((ep = rb.find("<entry>", ep)) != std::string::npos) {
                size_t ee = rb.find("</entry>", ep);
                if (ee == std::string::npos) break;
                std::string e = Trim(XmlUnescape(rb.substr(ep + 7, ee - ep - 7)));
                ep = ee + 8;
                if (!e.empty()) entries.push_back(e);
            }
        }
        gRules[name] = entries;
        n++;
    }
    return n;
}

bool ReadFile(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}
std::string FileStamp(const std::string &path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return "";
    return path + " " + std::to_string((long long)st.st_mtime) + " " + std::to_string((long long)st.st_size) + "\n";
}

void SplitEntry(const std::string &entry, std::string &pkg, std::string &act) {
    size_t s = entry.find('/');
    pkg = s == std::string::npos ? entry : entry.substr(0, s);
    act = s == std::string::npos ? "" : entry.substr(s + 1);
}
bool EntryInstalled(const std::string &entry) {
    auto it = gInstalled.find(entry);
    if (it != gInstalled.end()) return it->second;
    std::string pkg, act;
    SplitEntry(entry, pkg, act);
    // 액티비티가 ".Main" 처럼 줄여 적힌 것은 패키지 이름을 앞에 붙여 확인
    bool ok = plat::PackageInstalled(pkg, !act.empty() && act[0] == '.' ? pkg + act : act);
    gInstalled[entry] = ok;
    return ok;
}
// 에뮬레이터 이름 → 설치된 "패키지/액티비티" ("" = 없음)
std::string InstalledEntry(const std::string &emulator) {
    auto it = gRules.find(emulator);
    if (it == gRules.end()) return "";
    for (const auto &e : it->second)
        if (EntryInstalled(e)) return e;
    return "";
}
// 패키지가 들어 있는 규칙 이름 ("" = 모름)
std::string RuleOfPackage(const std::string &pkg) {
    for (const auto &kv : gRules)
        for (const auto &e : kv.second) {
            std::string p, a;
            SplitEntry(e, p, a);
            if (p == pkg) return kv.first;
        }
    return "";
}

// ---------------------------------------------------------------- emulators.txt
const char *kUserHeader =
    "# Joy Launcher 에뮬레이터 설정 / emulator choices\n"
    "#\n"
    "# 형식 / format:   <카테고리·롬 폴더·기종 이름>=<에뮬레이터>\n"
    "#   에뮬레이터 이름 (일부만 적어도 됨)       psx=DuckStation (Standalone)      snes=snes9x\n"
    "#   실행 명령 (페가수스 문법)               switch=am start -n 패키지/액티비티 -a android.intent.action.VIEW -d {file.uri}\n"
    "#       {file.path} 경로  {file.uri} 파일 제공자 주소  {file.documenturi} SAF 문서 주소  {file.name} {file.basename} {file.dir}\n"
    "#   ES-DE 명령                              ps2=%EMULATOR_NETHERSX2% %ACTION%=android.intent.action.MAIN %EXTRA_bootPath%=%ROMSAF%\n"
    "#   none                                    그 폴더의 게임은 실행하지 않음\n"
    "# 이름을 찾는 순서: 게임이 든 롬 폴더 → 카테고리 → 기종 (대소문자 구분 없음)\n"
    "# 런처의 옵션 > 에뮬레이터 선택 에서 고르면 여기에 적힙니다. 지우면 자동(메타데이터 → 폴더 이름 → 설치된 에뮬레이터 순).\n"
    "# 새 기종·에뮬레이터를 더하려면 /JoyLauncher/custom_systems/es_systems.xml · es_find_rules.xml (ES-DE 와 같은 문법).\n"
    "\n";

void LoadUser() {
    gUser.clear();
    std::ifstream in(gUserFile);
    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = line.substr(3);
        first = false;
        std::string t = Trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = Trim(t.substr(0, eq)), v = Trim(t.substr(eq + 1));
        if (!k.empty() && !v.empty()) gUser.push_back({k, v});
    }
}
void SaveUser() {
    std::ofstream o(gUserFile, std::ios::binary | std::ios::trunc);
    if (!o) return;
    o << kUserHeader;
    for (const auto &kv : gUser) o << kv.first << "=" << kv.second << "\n";
}
const std::string *UserValue(const std::string &key) {
    if (key.empty()) return nullptr;
    std::string k = Lower(key);
    for (const auto &kv : gUser)
        if (Lower(kv.first) == k) return &kv.second;
    return nullptr;
}

// ---------------------------------------------------------------- 폴더 이름 → 기종
const EmuSystem *ByName(const std::string &n) {
    auto it = gByName.find(n);
    return it == gByName.end() ? nullptr : &gSystems[it->second];
}
// 테마 그림 이름(esde.cpp) → 에뮬레이터 표의 기종 이름
const EmuSystem *ByThemeId(const std::string &id) {
    if (id.empty() || id == "default" || id == "favorites" || id == "hbfav") return nullptr;
    if (StartsWith(id, "arcade_")) return ByName("arcade");
    if (id == "wswan") return ByName("wonderswan");
    if (id == "wswanc") return ByName("wonderswancolor");
    return ByName(id);
}
// 영문·숫자만 남긴 이름 → 기종 이름
const std::map<std::string, const char *> &KeyAlias() {
    static const std::map<std::string, const char *> m = {
        {"fc", "nes"}, {"nintendoentertainmentsystem", "nes"}, {"superfamicom", "sfc"}, {"supernintendo", "snes"},
        {"supernes", "snes"}, {"md", "megadrive"}, {"segagenesis", "genesis"}, {"segamegadrive", "megadrive"},
        {"mdcd", "segacd"}, {"megacd", "megacd"}, {"md32x", "sega32x"}, {"32x", "sega32x"}, {"sms", "mastersystem"},
        {"segamastersystem", "mastersystem"}, {"segams", "mastersystem"}, {"gg", "gamegear"}, {"segagamegear", "gamegear"},
        {"sg1000", "sg-1000"}, {"pce", "pcengine"}, {"pcecd", "pcenginecd"}, {"turbografx16", "tg16"}, {"turbografx", "tg16"},
        {"turbografxcd", "tg-cd"}, {"tgcd", "tg-cd"}, {"sgx", "supergrafx"}, {"ps1", "psx"}, {"ps", "psx"},
        {"playstation", "psx"}, {"playstation1", "psx"}, {"sonyplaystation", "psx"}, {"playstation2", "ps2"},
        {"sonyplaystation2", "ps2"}, {"playstation3", "ps3"}, {"sonyplaystation3", "ps3"}, {"playstation4", "ps4"},
        {"sonyplaystation4", "ps4"}, {"playstationportable", "psp"}, {"pspminis", "psp"}, {"vita", "psvita"}, {"psv", "psvita"},
        {"playstationvita", "psvita"}, {"gameboy", "gb"}, {"gameboycolor", "gbc"}, {"gameboyadvance", "gba"},
        {"nintendods", "nds"}, {"ds", "nds"}, {"3ds", "n3ds"}, {"nintendo3ds", "n3ds"}, {"nintendo64", "n64"},
        {"gamecube", "gc"}, {"ngc", "gc"}, {"nintendogamecube", "gc"}, {"nintendowii", "wii"}, {"nintendowiiu", "wiiu"},
        {"ns", "switch"}, {"nsw", "switch"}, {"nx", "switch"}, {"nintendoswitch", "switch"}, {"ss", "saturn"},
        {"segasaturn", "saturn"}, {"dc", "dreamcast"}, {"segadreamcast", "dreamcast"}, {"ws", "wonderswan"},
        {"wsc", "wonderswancolor"}, {"neogeopocket", "ngp"}, {"neogeopocketcolor", "ngpc"}, {"lynx", "atarilynx"},
        {"jaguar", "atarijaguar"}, {"vb", "virtualboy"}, {"pc9801", "pc98"}, {"pc9800", "pc98"}, {"msdos", "dos"},
        {"x68k", "x68000"}, {"gw", "gameandwatch"}, {"gamewatch", "gameandwatch"}, {"cdi", "cdimono1"},
        {"x360", "xbox360"}, {"xbox1", "xbox"}, {"microsoftxbox", "xbox"}, {"microsoftxbox360", "xbox360"},
        {"win", "windows"}, {"winlator", "windows"}, {"steam", "steam"}, {"apk", "android"}, {"app", "androidapps"},
        {"apps", "androidapps"}, {"finalburnneo", "fbneo"}, {"fbalpha", "fba"}, {"fba2012", "fba"}, {"fbalpha2012", "fba"},
        {"mame2003", "mame"}, {"mame2003plus", "mame"}, {"mame2010", "mame"}, {"neogeocdz", "neogeocd"}, {"sfcmsu1", "sfc"},
        {"msx1", "msx1"}, {"zx", "zxspectrum"}, {"spectrum", "zxspectrum"}, {"amigacd", "amigacd32"}, {"cd32", "amigacd32"},
        {"commodore64", "c64"}, {"atari", "atari2600"}, {"coleco", "colecovision"}, {"intv", "intellivision"},
        {"3doplayer", "3do"}, {"naomi2", "naomi2"}, {"aw", "atomiswave"}, {"model2", "model2"}, {"model3", "model3"},
    };
    return m;
}
const EmuSystem *ByKey(const std::string &key) {
    if (key.empty()) return nullptr;
    for (const auto &s : gSystems)
        if (FolderKey(s.name) == key) return &s;
    auto it = KeyAlias().find(key);
    return it == KeyAlias().end() ? nullptr : ByName(it->second);
}
const EmuSystem *MatchWord(const std::string &name) {
    std::string low = Lower(Trim(name));
    if (low.empty()) return nullptr;
    if (const EmuSystem *s = ByName(low)) return s;
    if (const EmuSystem *s = ByThemeId(EsdeSystemName(low))) return s;
    return ByKey(FolderKey(low));
}

// ---------------------------------------------------------------- 주소·변수
std::string PercentEncode(const std::string &s, bool keepSlash) {
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || strchr("_-!.~'()*", c) || (keepSlash && c == '/')) o += (char)c;
        else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

struct Rom {
    std::string path, dir, base, stem;
    std::string systemDir;  // SAF 주소의 나무(tree) = 기종 폴더
    std::string romPath;    // 기종 폴더들의 위쪽 (%ROMPATH%)
};
Rom MakeRom(const LaunchCtx &c) {
    Rom r;
    r.path = c.game ? c.game->file : "";
    r.dir = ParentDir(r.path);
    r.base = Base(r.path);
    r.stem = Stem(r.path);
    // 이 파일이 든 가장 깊은 롬 루트를 찾아 그 바로 아래 폴더를 기종 폴더로
    std::string best;
    for (const auto &root : c.roots)
        if (!root.empty() && StartsWith(r.path, root + "/") && root.size() > best.size()) best = root;
    if (!best.empty()) {
        std::string rel = r.path.substr(best.size() + 1);
        size_t s = rel.find('/');
        if (s != std::string::npos) {
            r.systemDir = best + "/" + rel.substr(0, s);
            r.romPath = best;
        } else {  // 루트 바로 안의 파일: 루트가 곧 기종 폴더
            r.systemDir = best;
            r.romPath = ParentDir(best);
        }
    } else {
        r.systemDir = r.dir;
        r.romPath = ParentDir(r.dir);
    }
    return r;
}

// ES-DE 값 안의 변수 채우기
std::string ExpandEsde(std::string v, const Rom &rom) {
    ReplaceAll(v, "%QUOTATION%", "\\\"");
    ReplaceAll(v, "%GAMEDIRRAW%", rom.dir);
    ReplaceAll(v, "%GAMEDIR%", rom.dir);
    ReplaceAll(v, "%ROMPATHRAW%", rom.romPath);
    ReplaceAll(v, "%ROMPATH%", rom.romPath);
    ReplaceAll(v, "%ROMRAW%", rom.path);
    ReplaceAll(v, "%BASENAME%", rom.stem);
    ReplaceAll(v, "%FILENAME%", rom.base);
    ReplaceAll(v, "//", "/");
    ReplaceAll(v, "\\/", "/");
    ReplaceAll(v, "%ROMSAF%", EmuDocumentUri(rom.path, rom.systemDir));
    ReplaceAll(v, "%ROMPROVIDER%", EmuContentUri(rom.path));
    ReplaceAll(v, "%ROM%", rom.path);
    return v;
}

// 공백으로 나누되 = 뒤의 큰따옴표 값은 한 덩어리로 (ES-DE 명령)
std::vector<std::string> SplitEsde(const std::string &cmd) {
    std::vector<std::string> out;
    std::string cur;
    bool quote = false, any = false;
    for (char ch : cmd) {
        if (ch == '"') { quote = !quote; any = true; continue; }
        if (!quote && (ch == ' ' || ch == '\t')) {
            if (any) out.push_back(cur);
            cur.clear();
            any = false;
            continue;
        }
        cur += ch;
        any = true;
    }
    if (any) out.push_back(cur);
    return out;
}

// "패키지/액티비티" 를 -n 인자로
void PushComponent(std::vector<std::string> &args, const std::string &entry) {
    std::string pkg, act;
    SplitEntry(entry, pkg, act);
    if (act.empty()) {
        args.push_back("-p");
        args.push_back(pkg);
    } else {
        args.push_back("-n");
        args.push_back(pkg + "/" + act);
    }
}

// ES-DE 명령 → am start 인자. entry: 실행할 "패키지/액티비티" (%EMULATOR_X% 로 찾은 것)
bool EsdeToArgs(const std::string &raw, const std::string &entry, const Rom &rom, std::vector<std::string> &args, std::string &err) {
    std::string command = raw;
    // %INJECT%=<파일>: 파일 내용을 그 자리에 (게임 번호 등을 적어 둔 글 파일). 없으면 빈 채로 둠
    for (size_t ip = command.find("%INJECT%"); ip != std::string::npos; ip = command.find("%INJECT%")) {
        if (ip + 9 >= command.size() || command[ip + 8] != '=') {
            err = Fmt(T("실행 명령의 %s 가 잘못되었습니다"), "%INJECT%");
            return false;
        }
        std::string file;
        size_t cut;
        if (command[ip + 9] == '"') {
            size_t q = command.find('"', ip + 10);
            if (q == std::string::npos) { err = Fmt(T("실행 명령의 %s 가 잘못되었습니다"), "%INJECT%"); return false; }
            file = command.substr(ip + 10, q - ip - 10);
            cut = q + 1;
        } else {
            size_t sp = command.find(' ', ip);
            file = command.substr(ip + 9, sp == std::string::npos ? std::string::npos : sp - ip - 9);
            cut = sp == std::string::npos ? command.size() : sp;
        }
        ReplaceAll(file, "%BASENAME%", rom.stem);
        if (file == "%ROM%") file = rom.path;
        else if (!file.empty() && file[0] != '/') file = rom.dir + "/" + file;
        std::string content;
        ReadInject(file, content);
        command.replace(ip, cut - ip, content);
    }
    std::string pkg, act;
    SplitEntry(entry, pkg, act);
    ReplaceAll(command, "%ANDROIDPACKAGE%", pkg);
    ReplaceAll(command, "\\\"", "%QUOTATION%");
    ReplaceAll(command, "%INTERNALDATA%", plat::InternalDataRoot());
    ReplaceAll(command, "%EXTERNALDATA%", plat::PrimaryStorage());
    args.clear();
    PushComponent(args, entry);
    for (const std::string &tok : SplitEsde(command)) {
        if (StartsWith(tok, "%EMULATOR_")) continue;
        auto valueOf = [&](const char *var) -> const char * {
            size_t n = strlen(var);
            return tok.compare(0, n, var) == 0 ? tok.c_str() + n : nullptr;
        };
        const char *v;
        if ((v = valueOf("%ACTION%="))) { args.push_back("-a"); args.push_back(ExpandEsde(v, rom)); }
        else if ((v = valueOf("%CATEGORY%="))) { args.push_back("-c"); args.push_back(ExpandEsde(v, rom)); }
        else if ((v = valueOf("%MIMETYPE%="))) { args.push_back("-t"); args.push_back(ExpandEsde(v, rom)); }
        else if ((v = valueOf("%DATA%="))) { args.push_back("-d"); args.push_back(ExpandEsde(v, rom)); }
        else if (tok == "%ACTIVITY_CLEAR_TASK%") args.push_back("--activity-clear-task");
        else if (tok == "%ACTIVITY_CLEAR_TOP%") args.push_back("--activity-clear-top");
        else if (tok == "%ACTIVITY_NO_HISTORY%") args.push_back("--activity-no-history");
        else {
            static const std::pair<const char *, const char *> kinds[] = {
                {"%EXTRAARRAY_", "--esa"}, {"%EXTRAINTEGER_", "--ei"}, {"%EXTRABOOL_", "--ez"}, {"%EXTRA_", "--es"}};
            for (const auto &k : kinds) {
                size_t n = strlen(k.first);
                if (tok.compare(0, n, k.first) != 0) continue;
                size_t close = tok.find("%=", n);
                if (close == std::string::npos) break;
                std::string name = tok.substr(n, close - n), val = ExpandEsde(tok.substr(close + 2), rom);
                if (name.empty() || val.empty()) break;  // ES-DE 와 같이 빈 값은 넣지 않음
                args.push_back(k.second);
                args.push_back(name);
                args.push_back(val);
                break;
            }
        }
    }
    return true;
}

// RetroArch 코어 하나로 실행 (목록에 코어 이름만 있을 때: 스위치용·리눅스용 팩 등)
bool RetroArchArgs(const std::string &core, const Rom &rom, std::vector<std::string> &args, std::string &entryOut) {
    std::string entry = InstalledEntry("RETROARCH");
    if (entry.empty()) return false;
    std::string pkg, act;
    SplitEntry(entry, pkg, act);
    args.clear();
    PushComponent(args, entry);
    args.insert(args.end(), {"--es", "CONFIGFILE", plat::PrimaryStorage() + "/Android/data/" + pkg + "/files/retroarch.cfg",
                             "--es", "LIBRETRO", plat::InternalDataRoot() + "/" + pkg + "/cores/" + core + "_libretro_android.so",
                             "--es", "ROM", rom.path});
    entryOut = entry;
    return true;
}

// 페가수스 변수 채우기
std::string ExpandPegasus(std::string t, const Rom &rom) {
    if (t.find('{') == std::string::npos) return t;
    ReplaceAll(t, "{file.path}", rom.path);
    ReplaceAll(t, "{file.uri}", EmuContentUri(rom.path));
    ReplaceAll(t, "{file.name}", rom.base);
    ReplaceAll(t, "{file.basename}", rom.stem);
    ReplaceAll(t, "{file.dir}", rom.dir);
    ReplaceAll(t, "{file.documenturi}", EmuDocumentUri(rom.path, rom.dir));  // 페가수스: 나무 = 파일이 든 폴더
    for (size_t p = t.find("{env."); p != std::string::npos; p = t.find("{env.", p)) {
        size_t e = t.find('}', p);
        if (e == std::string::npos) break;
        const char *v = getenv(t.substr(p + 5, e - p - 5).c_str());
        std::string val = v ? v : "";
        t.replace(p, e - p + 1, val);
        p += val.size();
    }
    return t;
}

// am 인자에서 실행 대상 ("패키지/액티비티" 또는 패키지, 없으면 "")
std::string ArgsComponent(const std::vector<std::string> &args, size_t *at = nullptr) {
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == "-n" || args[i] == "-p") {
            if (at) *at = i + 1;
            return args[i + 1];
        }
    return "";
}

// old 패키지 이름을 new 로 (뒤에 이름 글자가 이어지면 다른 패키지이므로 그대로)
std::string SwapPackage(const std::string &s, const std::string &oldPkg, const std::string &newPkg) {
    std::string o;
    size_t p = 0;
    while (true) {
        size_t f = s.find(oldPkg, p);
        if (f == std::string::npos) { o += s.substr(p); break; }
        size_t end = f + oldPkg.size();
        char next = end < s.size() ? s[end] : '\0';
        bool boundary = !(std::isalnum((unsigned char)next) || next == '_' || next == '.');
        o += s.substr(p, f - p) + (boundary ? newPkg : oldPkg);
        p = end;
    }
    return o;
}

// 앱 패키지 이름 → 화면에 보일 에뮬레이터 이름 (표에 없으면 패키지 이름 그대로)
std::string FriendlyName(const std::string &pkg) {
    std::string rule = RuleOfPackage(pkg);
    if (rule.empty()) return pkg;
    if (rule == "RETROARCH") return "RetroArch";
    for (const auto &s : gSystems)
        for (const auto &c : s.commands)
            if (c.emulator == rule && c.core.empty()) {
                std::string n = c.label;
                size_t par = n.find(" (Standalone)");
                if (par != std::string::npos) n.erase(par);
                size_t br = n.find(" [");  // "MAME4droid Current [Diskette]" 같은 실행 방식 표시는 뺌
                if (br != std::string::npos) n.erase(br);
                return n;
            }
    return pkg;
}

enum class Peg { Ok, NotAm, NotInstalled };

// 페가수스 launch → am 인자. 대상 앱이 없으면 같은 에뮬레이터의 다른 패키지(RetroArch 64비트 ↔ 일반 등)를 찾는다
Peg PegasusToArgs(const std::string &launch, const Rom &rom, LaunchPlan &plan) {
    std::vector<std::string> tok = EmuTokenize(launch);
    if (tok.size() < 2 || Lower(tok[0]) != "am" || Lower(tok[1]) != "start") return Peg::NotAm;
    std::vector<std::string> args;
    for (size_t i = 2; i < tok.size(); i++) args.push_back(ExpandPegasus(tok[i], rom));
    size_t at = 0;
    std::string comp = ArgsComponent(args, &at);
    plan.how = "메타데이터 실행 명령 그대로";
    if (!comp.empty()) {
        std::string pkg, act;
        SplitEntry(comp, pkg, act);
        if (!EntryInstalled(comp) && !EntryInstalled(pkg)) {
            std::string rule = RuleOfPackage(pkg), other = rule.empty() ? "" : InstalledEntry(rule);
            if (other.empty()) {
                plan.package = pkg;
                return Peg::NotInstalled;
            }
            std::string npkg, nact;
            SplitEntry(other, npkg, nact);
            for (auto &a : args) a = SwapPackage(a, pkg, npkg);
            args[at] = args[at - 1] == "-p" ? npkg : other;
            plan.how = "메타데이터의 앱이 없어 같은 에뮬레이터의 다른 앱으로 (" + npkg + ")";
            pkg = npkg;
        }
        plan.package = pkg;
    }
    plan.args = args;
    plan.emulator = plan.package.empty() ? std::string("am start") : FriendlyName(plan.package);
    std::string core = EmuLaunchIdent(launch);
    if (!core.empty() && core != plan.package && core.find('.') == std::string::npos) plan.emulator += " (" + core + ")";
    return Peg::Ok;
}

// 게임의 기종: @sys 값 → 롬 폴더 이름 → 위 폴더 → 카테고리 → 목록에 적힌 코어·앱 → 확장자
const EmuSystem *SystemOf(const LaunchCtx &c, std::string *alt = nullptr) {
    const Game *g = c.game;
    if (g && StartsWith(g->launch, kLaunchSys)) {
        std::string v = g->launch.substr(strlen(kLaunchSys));
        size_t bar = v.find('|');
        if (alt && bar != std::string::npos) *alt = v.substr(bar + 1);
        if (const EmuSystem *s = ByName(v.substr(0, bar))) return s;
    }
    if (g) {
        std::string d = ParentDir(g->file);
        for (int up = 0; up < 2 && !d.empty(); up++, d = ParentDir(d))
            if (const EmuSystem *s = EmuSystemForFolder(Base(d))) return s;
    }
    for (const auto &f : c.folders)
        if (const EmuSystem *s = EmuSystemForFolder(f)) return s;
    for (const std::string &n : {c.shortName, c.category})
        if (const EmuSystem *s = EmuSystemForFolder(n)) return s;
    if (g && !StartsWith(g->launch, "@"))
        if (const EmuSystem *s = EmuSystemForIdent(g->launch)) return s;
    return g ? EmuSystemForExt(Ext(g->file)) : nullptr;
}

// 기종의 명령 가운데 이름이 맞는 것 (전체 일치 → 일부 일치, 대소문자 무시)
const EmuCommand *CommandByLabel(const EmuSystem &s, const std::string &label) {
    std::string want = Lower(Trim(label));
    if (want.empty()) return nullptr;
    for (const auto &c : s.commands)
        if (Lower(c.label) == want) return &c;
    std::string key = FolderKey(want);
    for (const auto &c : s.commands)
        if (!key.empty() && (FolderKey(c.label).find(key) != std::string::npos || FolderKey(c.core) == key)) return &c;
    return nullptr;
}

bool IsDirPath(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// 같은 에뮬레이터에 실행 방식이 여럿일 때(aPS3e: 게임 번호 / 폴더 / ISO, GameNative: Steam / Epic …) 파일에 맞는 것
const EmuCommand *Variant(const EmuSystem &s, const EmuCommand &cmd, const Rom &rom) {
    std::string ext = Ext(rom.path);
    bool dir = IsDirPath(rom.path);
    auto score = [&](const EmuCommand &c) {
        std::string low = Lower(c.label), word;
        bool hasExt = false, hasDir = false, hasSerial = false;
        for (char ch : low + " ") {
            if (std::isalnum((unsigned char)ch)) { word += ch; continue; }
            if (!word.empty()) {
                hasExt = hasExt || (!ext.empty() && word == ext);
                hasDir = hasDir || word == "directory";
                hasSerial = hasSerial || word == "serial";
            }
            word.clear();
        }
        if (dir) return hasDir ? 2 : 0;
        if (hasExt) return 2;
        return hasSerial && ext != "iso" ? 1 : 0;
    };
    const EmuCommand *best = &cmd;
    int bestScore = score(cmd);
    for (const auto &c : s.commands)
        if (c.emulator == cmd.emulator && c.core.empty() && score(c) > bestScore) {
            best = &c;
            bestScore = score(c);
        }
    // 같은 에뮬레이터에 맞는 방식이 없으면, 설치된 다른 단독 에뮬레이터 가운데 이 파일 종류를 맡는 것
    // (윈도우 기종: .steam → GameNative Steam, .desktop → Winlator …)
    if (bestScore == 0)
        for (const auto &c : s.commands)
            if (c.core.empty() && !c.emulator.empty() && score(c) > 0 && !InstalledEntry(c.emulator).empty()) return &c;
    return best;
}

// 단독 에뮬레이터를 깔아 쓰는 것이 보통인 기종 (RetroArch 코어가 목록 앞에 있어도 설치된 단독 에뮬레이터를 먼저).
// 그 밖의 기종(8·16비트, 아케이드 등)은 RetroArch 가 보통이고, 목록의 단독 에뮬레이터는 다른 기종이 주력인 것
// (GBA 목록의 NooDS·SkyEmu, 아케이드 목록의 Flycast …)도 섞여 있어 ES-DE 순서를 그대로 따른다.
// 목록 맨 앞이 단독 에뮬레이터인 기종(ps3·switch·xbox …)은 어느 쪽이든 그것이 먼저다
bool PreferStandalone(const EmuSystem &s) {
    static const char *const k[] = {"psx", "ps2", "psp", "n64", "n64dd", "nds", "n3ds", "gc", "wii", "saturn", "saturnjp", "dreamcast"};
    for (const char *n : k)
        if (s.name == n) return true;
    return false;
}

// 설치된 에뮬레이터 가운데 자동으로 고름.
//   ① 롬 모음 목록에서 본 RetroArch 코어 (그 코어는 깔려 있을 가능성이 높음)
//   ② ES-DE 가 적어 둔 순서의 첫 번째. 다만 PS1·PS2·PSP·N64·NDS·3DS·GC·Wii·새턴·드림캐스트는 설치된 단독 에뮬레이터를 먼저
//      (일부러 깔아 둔 것이고, RetroArch 에 어떤 코어가 깔렸는지는 밖에서 알 수 없음)
const EmuCommand *AutoCommand(const EmuSystem &s, const Rom &rom, std::string &entry) {
    const EmuCommand *first = nullptr, *knownRa = nullptr, *firstSa = nullptr;
    for (const auto &c : s.commands) {
        if (c.emulator.empty() || InstalledEntry(c.emulator).empty()) continue;
        if (!first) first = &c;
        if (!c.core.empty()) {
            if (!knownRa && gKnownCores.count(c.core)) knownRa = &c;
        } else if (!firstSa) {
            firstSa = &c;
        }
    }
    const EmuCommand *pick = knownRa ? knownRa : (PreferStandalone(s) && firstSa) ? firstSa : first;
    if (pick && pick->core.empty()) pick = Variant(s, *pick, rom);
    entry = pick ? InstalledEntry(pick->emulator) : "";
    return pick;
}

// 기종에 쓸 수 있는 에뮬레이터 이름들 (알림용, 몇 개만)
std::string EmulatorNames(const EmuSystem &s) {
    std::vector<std::string> names;
    for (const auto &c : s.commands) {
        std::string n = c.core.empty() ? c.label : "RetroArch";
        size_t par = n.find(" (Standalone)");
        if (par != std::string::npos) n.erase(par);
        if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    }
    std::string out;
    for (size_t i = 0; i < names.size() && i < 5; i++) out += (i ? ", " : "") + names[i];
    if (names.size() > 5) out += " …";
    return out;
}

// 앱 실행 항목 (%ANDROIDAPP%=): 파일 안에 "패키지[/액티비티]"
bool AndroidAppArgs(const std::string &command, const Rom &rom, LaunchPlan &plan, std::string &err) {
    size_t p = command.find("%ANDROIDAPP%=");
    std::string v = command.substr(p + 13);
    v = v.substr(0, v.find(' '));
    if (v == "%FILEINJECT%" && !ReadInject(rom.path, v)) {
        err = T("앱 정보 파일이 비었거나 읽을 수 없습니다");
        return false;
    }
    v = Trim(v);
    std::string pkg, act;
    SplitEntry(v, pkg, act);
    if (pkg.empty() || !plat::PackageInstalled(pkg, "")) {
        err = Fmt(T("앱이 설치되어 있지 않습니다: %s"), pkg.c_str());
        return false;
    }
    plan.args.clear();
    PushComponent(plan.args, v);
    plan.package = pkg;
    plan.emulator = pkg;
    return true;
}

// 기종의 명령 하나로 실행 계획 만들기
bool PlanFromCommand(const EmuSystem &s, const EmuCommand &cmd, std::string entry, const Rom &rom, LaunchPlan &plan, std::string &err) {
    if (cmd.command.find("%ANDROIDAPP%=") != std::string::npos) return AndroidAppArgs(cmd.command, rom, plan, err);
    if (entry.empty()) entry = InstalledEntry(cmd.emulator);
    if (entry.empty()) {
        err = Fmt(T("에뮬레이터가 설치되어 있지 않습니다: %s"), cmd.label.c_str());
        return false;
    }
    if (!EsdeToArgs(cmd.command, entry, rom, plan.args, err)) return false;
    std::string act;
    SplitEntry(entry, plan.package, act);
    plan.emulator = cmd.core.empty() ? cmd.label : "RetroArch (" + cmd.label + ")";
    (void)s;
    return true;
}

// 사용자가 적은 값으로 (이름 / am start … / ES-DE 명령)
bool PlanFromUser(const std::string &value, const EmuSystem *sys, const Rom &rom, LaunchPlan &plan, std::string &err) {
    std::string low = Lower(value);
    if (low == "none") {
        err = T("emulators.txt 에서 실행하지 않도록 정한 폴더입니다");
        return false;
    }
    if (StartsWith(low, "am ")) {
        Peg r = PegasusToArgs(value, rom, plan);
        if (r == Peg::Ok) return true;
        err = r == Peg::NotAm ? std::string(T("emulators.txt 의 실행 명령은 am start 로 시작해야 합니다"))
                              : Fmt(T("에뮬레이터가 설치되어 있지 않습니다: %s"), plan.package.c_str());
        return false;
    }
    if (value.find('%') != std::string::npos) {
        EmuCommand c;
        c.command = value;
        AnalyzeCommand(c);
        c.label = c.emulator.empty() ? value : c.emulator;
        static const EmuSystem none;
        return PlanFromCommand(sys ? *sys : none, c, "", rom, plan, err);
    }
    if (!sys) {
        err = T("기종을 알 수 없어 emulators.txt 의 에뮬레이터 이름을 쓸 수 없습니다 (am start … 명령으로 적어 주세요)");
        return false;
    }
    const EmuCommand *c = CommandByLabel(*sys, value);
    if (!c) {
        err = Fmt(T("emulators.txt 의 에뮬레이터 이름을 찾지 못했습니다: %s"), value.c_str());
        return false;
    }
    return PlanFromCommand(*sys, *c, "", rom, plan, err);
}

// 이 게임에 적용되는 사용자 지정 (롬 폴더 → 카테고리 → 기종)
const std::string *UserFor(const LaunchCtx &c, const EmuSystem *sys, std::string *keyOut = nullptr) {
    std::vector<std::string> keys;
    if (c.game) keys.push_back(Base(ParentDir(c.game->file)));
    else keys.insert(keys.end(), c.folders.begin(), c.folders.end());
    keys.push_back(c.category);
    keys.push_back(c.shortName);
    if (sys) keys.push_back(sys->name);
    for (const auto &k : keys)
        if (const std::string *v = UserValue(k)) {
            if (keyOut) *keyOut = k;
            return v;
        }
    return nullptr;
}

}  // namespace

// ================================================================ 공개 함수
void EmuLoad(const std::string &home, const std::vector<std::string> &esdeDirs) {
    gSystems.clear();
    gByName.clear();
    gRules.clear();
    gInstalled.clear();
    gSig.clear();
    std::vector<char> buf;
    if (plat::ReadAsset("systems/es_systems.xml", buf)) ParseSystems(std::string(buf.begin(), buf.end()));
    if (plat::ReadAsset("systems/es_find_rules.xml", buf)) ParseRules(std::string(buf.begin(), buf.end()));
    gSig += "S" + std::to_string(gSystems.size()) + " R" + std::to_string(gRules.size()) + "\n";
    // 사용자 표: ES-DE 폴더의 것 → 런처 폴더의 것 (뒤가 우선)
    std::vector<std::string> dirs;
    for (const auto &e : esdeDirs) dirs.push_back(e + "/custom_systems");
    dirs.push_back(home + "/custom_systems");
    for (const auto &d : dirs) {
        std::string x;
        if (ReadFile(d + "/es_systems.xml", x)) { ParseSystems(x); gSig += FileStamp(d + "/es_systems.xml"); }
        if (ReadFile(d + "/es_find_rules.xml", x)) { ParseRules(x); gSig += FileStamp(d + "/es_find_rules.xml"); }
    }
    gUserFile = home + "/emulators.txt";
    LoadUser();
    for (const auto &kv : gUser) gSig += "U" + Lower(kv.first) + "\n";  // 값은 실행할 때 쓰므로 이름만
    if (!FileExists(gUserFile)) SaveUser();
}

std::string EmuSignature() { return gSig; }
const std::vector<EmuSystem> &EmuSystems() { return gSystems; }
const EmuSystem *EmuFind(const std::string &name) { return ByName(Lower(name)); }

const EmuSystem *EmuSystemForFolder(const std::string &name) {
    if (name.empty()) return nullptr;
    if (const EmuSystem *s = MatchWord(name)) return s;
    // "FBNEO ACT", "gba.custom", "PS2 (한글)" 처럼 뒤에 말이 붙은 이름: 첫 단어
    std::string first = name.substr(0, name.find_first_of(" _-(.["));
    if (first != name)
        if (const EmuSystem *s = MatchWord(first)) return s;
    std::string key = FolderKey(name), fk = FolderKey(first);
    // 앞부분이 같은 이름
    static const std::pair<const char *, const char *> prefixes[] = {
        {"fbalpha2012", "fba"}, {"fba2012", "fba"}, {"fbneo", "fbneo"}, {"finalburn", "fbneo"}, {"fba", "fba"},
        {"arcade", "arcade"}, {"mame", "mame"}, {"naomi2", "naomi2"}, {"naomi", "naomi"}, {"atomiswave", "atomiswave"},
        {"turbografx", "tg16"}, {"wonderswancolor", "wonderswancolor"}, {"wonderswan", "wonderswan"},
        {"neogeopocketcolor", "ngpc"}, {"neogeopocket", "ngp"}, {"neogeocd", "neogeocd"}};
    for (const auto &p : prefixes)
        for (const std::string &k : {key, fk})
            if (!k.empty() && StartsWith(k, p.first))
                if (const EmuSystem *s = ByName(p.second)) return s;
    // "Sony PlayStation 2", "Nintendo 3DS (USA)" 처럼 이름 안에 들어 있는 경우 (긴 이름부터).
    // CPS2·CPS3 는 이름에 ps2·ps3 가 들어 있어도 플레이스테이션이 아님
    if (key.find("cps") != std::string::npos) return nullptr;
    static const std::pair<const char *, const char *> parts[] = {
        {"playstationportable", "psp"}, {"playstationvita", "psvita"}, {"playstation4", "ps4"}, {"playstation3", "ps3"},
        {"playstation2", "ps2"}, {"playstation", "psx"}, {"nintendoswitch", "switch"}, {"nintendo3ds", "n3ds"},
        {"nintendods", "nds"}, {"nintendo64", "n64"}, {"gamecube", "gc"}, {"wiiu", "wiiu"}, {"xbox360", "xbox360"},
        {"xbox", "xbox"}, {"dreamcast", "dreamcast"}, {"saturn", "saturn"}, {"megadrive", "megadrive"},
        {"genesis", "genesis"}, {"superfamicom", "sfc"}, {"supernintendo", "snes"}, {"gameboyadvance", "gba"},
        {"gameboycolor", "gbc"}, {"gameboy", "gb"}, {"pcengine", "pcengine"}, {"neogeo", "neogeo"}, {"psvita", "psvita"},
        {"ps4", "ps4"}, {"ps3", "ps3"}, {"ps2", "ps2"}, {"3ds", "n3ds"}};
    for (const auto &p : parts)
        if (key.find(p.first) != std::string::npos)
            if (const EmuSystem *s = ByName(p.second)) return s;
    return nullptr;
}

const EmuSystem *EmuSystemForExt(const std::string &extIn) {
    static const std::map<std::string, const char *> uniq = {
        {"gba", "gba"}, {"gb", "gb"}, {"gbc", "gbc"}, {"nes", "nes"}, {"fds", "fds"}, {"sfc", "snes"}, {"smc", "snes"},
        {"md", "megadrive"}, {"gen", "megadrive"}, {"smd", "megadrive"}, {"32x", "sega32x"}, {"sms", "mastersystem"},
        {"gg", "gamegear"}, {"pce", "pcengine"}, {"ws", "wonderswan"}, {"wsc", "wonderswancolor"}, {"ngp", "ngp"},
        {"ngc", "ngpc"}, {"nds", "nds"}, {"n64", "n64"}, {"z64", "n64"}, {"v64", "n64"}, {"cso", "psp"}, {"pbp", "psx"},
        {"cue", "psx"}, {"chd", "psx"}, {"lnx", "atarilynx"}, {"vb", "virtualboy"}, {"cdi", "dreamcast"},
        {"gdi", "dreamcast"}, {"hdi", "pc98"}, {"fdi", "pc98"}, {"d88", "pc98"}, {"dosz", "dos"}, {"zip", "fbneo"},
        {"7z", "fbneo"}, {"nsp", "switch"}, {"xci", "switch"}, {"3ds", "n3ds"}, {"cci", "n3ds"}, {"cxi", "n3ds"},
        {"rvz", "gc"}, {"gcz", "gc"}, {"gcm", "gc"}, {"wbfs", "wii"}, {"wua", "wiiu"}, {"wux", "wiiu"}, {"rpx", "wiiu"},
        {"psvita", "psvita"}, {"xex", "xbox360"}, {"xiso", "xbox"},
    };
    auto it = uniq.find(Lower(extIn));
    return it == uniq.end() ? nullptr : ByName(it->second);
}

const EmuSystem *EmuSystemForIdent(const std::string &identIn) {
    // 위에서부터 부분 일치 (더 구체적인 이름을 먼저)
    static const std::pair<const char *, const char *> t[] = {
        {"aethersx2", "ps2"}, {"nethersx2", "ps2"}, {"armsx2", "ps2"}, {"pcsx2", "ps2"}, {"lrps2", "ps2"}, {"pcee2", "ps2"},
        {"virtualapplications.play", "ps2"}, {"rpcs3", "ps3"}, {"aps3e", "ps3"}, {"armsx3", "ps3"}, {"citra", "n3ds"},
        {"lime3ds", "n3ds"}, {"azahar", "n3ds"}, {"mandarine", "n3ds"}, {"panda3ds", "n3ds"}, {"dolphin", "gc"},
        {"cemu", "wiiu"}, {"yuzu", "switch"}, {"suyu", "switch"}, {"sudachi", "switch"}, {"citron", "switch"},
        {"dev.eden", "switch"}, {"eden_emulator", "switch"}, {"strato", "switch"}, {"skyline", "switch"},
        {"ryujinx", "switch"}, {"kenjinx", "switch"}, {"vita3k", "psvita"}, {"ax360e", "xbox360"}, {"xenia", "xbox360"},
        {"winlator", "windows"}, {"mobox", "windows"}, {"exagear", "windows"}, {"gamehub", "windows"},
        {"gamenative", "windows"}, {"melonds", "nds"}, {"desmume", "nds"}, {"drastic", "nds"}, {"nds4droid", "nds"},
        {"noods", "nds"}, {"mupen64", "n64"}, {"parallel_n64", "n64"}, {"m64plus", "n64"}, {"yabause", "saturn"},
        {"yabasanshiro", "saturn"}, {"yabasanshioro", "saturn"}, {"kronos", "saturn"}, {"mednafen_saturn", "saturn"},
        {"beetle_saturn", "saturn"}, {"flycast", "dreamcast"}, {"reicast", "dreamcast"}, {"redream", "dreamcast"},
        {"duckstation", "psx"}, {"swanstation", "psx"}, {"pcsx", "psx"}, {"mednafen_psx", "psx"}, {"beetle_psx", "psx"},
        {"epsxe", "psx"}, {"fpse", "psx"}, {"ppsspp", "psp"}, {"mesen-s", "snes"}, {"mesen_s", "snes"}, {"snes9x", "snes"},
        {"bsnes", "snes"}, {"fceumm", "nes"}, {"nestopia", "nes"}, {"quicknes", "nes"}, {"mesen", "nes"},
        {"gambatte", "gb"}, {"sameboy", "gb"}, {"gearboy", "gb"}, {"tgbdual", "gb"}, {"mgba", "gba"}, {"gpsp", "gba"},
        {"vba", "gba"}, {"meteor", "gba"}, {"myboy", "gba"}, {"pizzaboy", "gba"}, {"genesis_plus_gx", "megadrive"},
        {"picodrive", "megadrive"}, {"blastem", "megadrive"}, {"md.emu", "megadrive"}, {"mednafen_supergrafx", "supergrafx"},
        {"mednafen_pce", "pcengine"}, {"beetle_pce", "pcengine"}, {"geargrafx", "pcengine"}, {"mednafen_wswan", "wonderswan"},
        {"beetle_wswan", "wonderswan"}, {"mednafen_ngp", "ngp"}, {"beetle_ngp", "ngp"}, {"mame", "mame"},
        {"fbneo", "fbneo"}, {"fbalpha", "fba"}, {"dosbox", "dos"}, {"np2", "pc98"}, {"nekop2", "pc98"},
        {"bluemsx", "msx"}, {"fmsx", "msx"}, {"handy", "atarilynx"}, {"mednafen_lynx", "atarilynx"},
        {"mednafen_vb", "virtualboy"}, {"mednafen_pcfx", "pcfx"}, {"opera", "3do"}, {"stella", "atari2600"},
        {"prosystem", "atari7800"}, {"virtualjaguar", "atarijaguar"}, {"puae", "amiga"}, {"vice", "c64"}, {"scummvm", "scummvm"},
    };
    std::string id = Lower(identIn);
    if (id.empty()) return nullptr;
    for (const auto &kv : t)
        if (id.find(kv.first) != std::string::npos)
            if (const EmuSystem *s = ByName(kv.second)) return s;
    return nullptr;
}

bool EmuUserHas(const std::string &key) { return UserValue(key) != nullptr; }

std::string EmuLaunchIdent(const std::string &launch) {
    size_t p = launch.find("_libretro");
    if (p != std::string::npos) {
        size_t b = launch.find_last_of("/ \t\"'", p);
        return launch.substr(b == std::string::npos ? 0 : b + 1, p - (b == std::string::npos ? 0 : b + 1));
    }
    std::vector<std::string> tok = EmuTokenize(launch);
    std::string comp = ArgsComponent(tok);
    if (!comp.empty()) return comp.substr(0, comp.find('/'));
    return "";
}

void EmuLearnCores(const std::vector<Collection> &cols) {
    gKnownCores.clear();
    for (const auto &c : cols)
        for (const auto &g : c.games) {
            size_t p = g.launch.find("_libretro");
            if (p == std::string::npos || g.launch[0] == '@') continue;
            std::string core = EmuLaunchIdent(g.launch);
            if (!core.empty()) gKnownCores.insert(core);
        }
}

std::vector<std::string> EmuTokenize(const std::string &s) {
    std::vector<std::string> out;
    size_t i = 0, n = s.size();
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    while (i < n) {
        while (i < n && space(s[i])) i++;
        if (i >= n) break;
        char ch = s[i];
        size_t end;
        if (ch == '\'' || ch == '"') {
            size_t q = s.find(ch, i + 1);
            end = q == std::string::npos ? n : q + 1;
        } else {
            end = i + 1;
            while (end < n && !space(s[end])) end++;
        }
        bool quoted = (ch == '\'' || ch == '"') && end - i > 1;
        bool full = quoted && s[end - 1] == ch;
        std::string t = s.substr(quoted ? i + 1 : i, full ? end - i - 2 : end - i - (quoted ? 1 : 0));
        out.push_back(Trim(t));
        i = end;
    }
    return out;
}

std::string EmuContentUri(const std::string &path) {
    return "content://com.jyjylab.joylauncher.files/root" + PercentEncode(path, true);
}

std::string EmuDocumentUri(const std::string &path, const std::string &treeDir) {
    // 파일이 든 저장소 (가장 깊은 것)
    plat::Volume best;
    for (const auto &v : plat::Volumes())
        if (!v.path.empty() && (path == v.path || StartsWith(path, v.path + "/")) && v.path.size() > best.path.size()) best = v;
    std::string id = "primary", root = plat::PrimaryStorage();
    if (!best.path.empty()) {
        id = best.id;
        root = best.path;
    }
    auto rel = [&](const std::string &p) {
        if (p == root) return std::string();
        return StartsWith(p, root + "/") ? p.substr(root.size() + 1) : (!p.empty() && p[0] == '/' ? p.substr(1) : p);
    };
    std::string tree = treeDir.empty() || !(path == treeDir || StartsWith(path, treeDir + "/")) ? ParentDir(path) : treeDir;
    return "content://com.android.externalstorage.documents/tree/" + PercentEncode(id + ":" + rel(tree), false) + "/document/" +
           PercentEncode(id + ":" + rel(path), false);
}

bool EmuBuildLaunch(const LaunchCtx &c, LaunchPlan &plan, std::string &err) {
    plan = LaunchPlan();
    err.clear();
    if (!c.game) return false;
    const Game &g = *c.game;
    Rom rom = MakeRom(c);
    std::string alt;
    const EmuSystem *sys = SystemOf(c, &alt);
    // 0) 사용자 지정 (emulators.txt)
    std::string ukey;
    if (const std::string *uv = UserFor(c, sys, &ukey)) {
        if (!PlanFromUser(*uv, sys, rom, plan, err)) return false;
        plan.how = "emulators.txt (" + ukey + ")";
        return true;
    }
    // 1) 페가수스: 목록의 실행 명령
    std::string missing;
    if (!g.launch.empty() && g.launch[0] != '@') {
        Peg r = PegasusToArgs(g.launch, rom, plan);
        if (r == Peg::Ok) return true;
        if (r == Peg::NotInstalled) missing = plan.package;
        // 다른 기기용 목록 (스위치 nro·리눅스 명령) 에 RetroArch 코어 이름이 있으면 그 코어로
        std::string core = g.launch.find("_libretro") != std::string::npos ? EmuLaunchIdent(g.launch) : "";
        std::string entry;
        if (r == Peg::NotAm && !core.empty() && RetroArchArgs(core, rom, plan.args, entry)) {
            std::string act;
            SplitEntry(entry, plan.package, act);
            plan.emulator = "RetroArch (" + core + ")";
            plan.how = "메타데이터의 코어 이름으로 RetroArch 실행";
            return true;
        }
        plan = LaunchPlan();
    }
    // 2) 기종의 에뮬레이터 목록 (ES-DE 방식)
    if (!sys) {
        err = missing.empty() ? std::string(T("이 게임을 실행할 에뮬레이터를 알 수 없습니다 (emulators.txt 에 롬 폴더 이름으로 정해 주세요)"))
                              : Fmt(T("에뮬레이터가 설치되어 있지 않습니다: %s"), missing.c_str());
        return false;
    }
    const EmuCommand *cmd = nullptr;
    std::string entry;
    if (!alt.empty()) {  // ES-DE gamelist 의 altemulator
        cmd = CommandByLabel(*sys, alt);
        if (cmd && !cmd->emulator.empty() && InstalledEntry(cmd->emulator).empty()) cmd = nullptr;
    }
    bool app = false;
    for (const auto &k : sys->commands) app = app || k.command.find("%ANDROIDAPP%=") != std::string::npos;
    if (!cmd && app) cmd = &sys->commands[0];
    if (!cmd) cmd = AutoCommand(*sys, rom, entry);
    if (!cmd) {
        err = Fmt(T("%s 에뮬레이터가 설치되어 있지 않습니다 (지원: %s)"), sys->fullname.empty() ? sys->name.c_str() : sys->fullname.c_str(),
                  EmulatorNames(*sys).c_str());
        return false;
    }
    if (!PlanFromCommand(*sys, *cmd, entry, rom, plan, err)) return false;
    plan.how = missing.empty() ? "기종의 에뮬레이터 목록에서 고름 (" + sys->name + ")"
                               : "메타데이터의 앱(" + missing + ")이 없어 기종의 에뮬레이터 목록에서 고름 (" + sys->name + ")";
    return true;
}

std::vector<EmuChoice> EmuChoices(const LaunchCtx &c, std::string &systemName) {
    std::vector<EmuChoice> out;
    const EmuSystem *sys = SystemOf(c);
    systemName = sys ? sys->name : "";
    const std::string *uv = UserFor(c, sys);
    EmuChoice aut;
    aut.installed = true;
    aut.current = uv == nullptr;
    out.push_back(aut);
    if (!sys) return out;
    const EmuCommand *cur = uv ? CommandByLabel(*sys, *uv) : nullptr;
    for (const auto &k : sys->commands) {
        if (k.emulator.empty()) continue;
        EmuChoice ch;
        ch.label = k.label;
        ch.installed = !InstalledEntry(k.emulator).empty();
        ch.current = cur == &k;
        out.push_back(ch);
    }
    return out;
}

void EmuSetUser(const std::string &key, const std::string &value) {
    if (key.empty()) return;
    std::string k = Lower(key);
    gUser.erase(std::remove_if(gUser.begin(), gUser.end(), [&](const std::pair<std::string, std::string> &kv) { return Lower(kv.first) == k; }),
                gUser.end());
    if (!value.empty()) gUser.push_back({key, value});
    SaveUser();
}

void EmuForgetInstalled() { gInstalled.clear(); }
