// 런처 안의 작은 FTP 서버: PC(테마 빌더·FTP 프로그램)에서 테마를 받는다.
// 한 번에 한 PC, 패시브 모드, 이진 전송. 따로 도는 스레드 하나가 접속을 받고 처리한다.
#include "ftpserver.h"

#include <SDL.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <direct.h>
typedef SOCKET fsock;
#define FCLOSE closesocket
#define FBAD INVALID_SOCKET
static int MkDir(const std::string &p) { return _mkdir(p.c_str()); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int fsock;
#define FCLOSE close
#define FBAD (-1)
static int MkDir(const std::string &p) { return mkdir(p.c_str(), 0777); }
#endif
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace {

SDL_Thread *gThread = nullptr;
SDL_mutex *gMx = nullptr;
volatile bool gQuit = false;
fsock gListen = FBAD;
std::string gHome;
FtpServerStatus gSt;
#ifdef __SWITCH__
bool gSockInit = false;
#endif

void Log(const std::string &s) {
    SDL_LockMutex(gMx);
    gSt.log.push_back(s);
    if (gSt.log.size() > 40) gSt.log.erase(gSt.log.begin());
    SDL_UnlockMutex(gMx);
}

bool SendAll(fsock s, const char *d, size_t n) {
    while (n > 0) {
        int w = send(s, d, (int)std::min<size_t>(n, 32768), 0);
        if (w <= 0) return false;
        d += w;
        n -= (size_t)w;
    }
    return true;
}
bool Reply(fsock s, const std::string &line) {
    std::string l = line + "\r\n";
    return SendAll(s, l.data(), l.size());
}
// 읽을 것이 있을 때까지 (끝내라는 표시를 보며) 기다림. 1 = 있음, 0 = 시간 초과, -1 = 끝
int WaitRead(fsock s, int ms) {
    int waited = 0;
    while (!gQuit) {
        fd_set rs;
        FD_ZERO(&rs);
        FD_SET(s, &rs);
        timeval tv{0, 200000};
        int r = select((int)s + 1, &rs, nullptr, nullptr, &tv);
        if (r > 0) return 1;
        if (r < 0) return -1;
        waited += 200;
        if (ms >= 0 && waited >= ms) return 0;
    }
    return -1;
}

// ---------------------------------------------------------------- 경로
// 가상 경로(/JoyLauncher/…)를 정리: . 과 .. 처리
std::string Normalize(const std::string &cwd, const std::string &arg) {
    std::string p = arg.empty() ? cwd : (arg[0] == '/' ? arg : cwd + "/" + arg);
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        std::string seg = p.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (seg == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (!seg.empty() && seg != ".") parts.push_back(seg);
        if (j == std::string::npos) break;
        i = j + 1;
    }
    std::string out;
    for (const auto &s : parts) out += "/" + s;
    return out.empty() ? "/" : out;
}
// 보이는 곳: / , /JoyLauncher , 그리고 쓸 수 있는 곳
bool Writable(const std::string &v) {
    return v.rfind("/JoyLauncher/themes/", 0) == 0 || v == "/JoyLauncher/categories.txt" || v.rfind("/JoyLauncher/categories/", 0) == 0;
}
bool Visible(const std::string &v) {
    return v == "/" || v == "/JoyLauncher" || v == "/JoyLauncher/themes" || v == "/JoyLauncher/categories" || Writable(v);
}
std::string Real(const std::string &v) {
    if (v == "/" ) return gHome;  // 쓰이지 않음 (목록은 따로 만듦)
    if (v.rfind("/JoyLauncher", 0) == 0) return gHome + v.substr(12);
    return "";
}
bool IsDir(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool Stat(const std::string &p, long long &size, bool &dir) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0) return false;
    size = st.st_size;
    dir = S_ISDIR(st.st_mode);
    return true;
}

// ---------------------------------------------------------------- 데이터 연결 (패시브)
fsock OpenPasv(fsock ctrl, int &port, std::string &ip) {
    fsock s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == FBAD) return FBAD;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = 0;
    if (bind(s, (sockaddr *)&a, sizeof(a)) != 0 || listen(s, 1) != 0) { FCLOSE(s); return FBAD; }
    socklen_t len = sizeof(a);
    getsockname(s, (sockaddr *)&a, &len);
    port = ntohs(a.sin_port);
    sockaddr_in me{};
    len = sizeof(me);
    getsockname(ctrl, (sockaddr *)&me, &len);
    char buf[32];
    unsigned char *b = (unsigned char *)&me.sin_addr.s_addr;
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    ip = buf;
    return s;
}
fsock AcceptData(fsock &pasv) {
    if (pasv == FBAD) return FBAD;
    fsock d = FBAD;
    if (WaitRead(pasv, 10000) == 1) d = accept(pasv, nullptr, nullptr);
    FCLOSE(pasv);
    pasv = FBAD;
    return d;
}

// ---------------------------------------------------------------- 목록
struct Entry { std::string name; bool dir; long long size; };
std::vector<Entry> ListVirtual(const std::string &v) {
    std::vector<Entry> out;
    if (v == "/") { out.push_back({"JoyLauncher", true, 0}); return out; }
    if (v == "/JoyLauncher") {
        out.push_back({"themes", true, 0});
        long long sz;
        bool d;
        if (Stat(gHome + "/categories", sz, d)) out.push_back({"categories", true, 0});
        if (Stat(gHome + "/categories.txt", sz, d)) out.push_back({"categories.txt", false, sz});
        return out;
    }
    std::string r = Real(v);
    if (DIR *dd = opendir(r.c_str())) {
        while (dirent *e = readdir(dd)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            long long sz = 0;
            bool d = false;
            Stat(r + "/" + n, sz, d);
            out.push_back({n, d, sz});
        }
        closedir(dd);
    }
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
    return out;
}

// ---------------------------------------------------------------- 한 PC 접속 처리
void Session(fsock c) {
    std::string cwd = "/JoyLauncher", buf, rnfr;
    fsock pasv = FBAD;
    Reply(c, "220 Joy Launcher theme FTP");
    while (!gQuit) {
        size_t nl;
        while ((nl = buf.find('\n')) == std::string::npos) {
            int w = WaitRead(c, 300000);  // 5분 동안 아무것도 없으면 끊음
            if (w <= 0) goto done;
            char tmp[1024];
            int n = recv(c, tmp, sizeof(tmp), 0);
            if (n <= 0) goto done;
            buf.append(tmp, n);
        }
        {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t sp = line.find(' ');
            std::string cmd = line.substr(0, sp), arg = sp == std::string::npos ? "" : line.substr(sp + 1);
            for (auto &ch : cmd) ch = (char)toupper((unsigned char)ch);
            std::string v = Normalize(cwd, arg);
            if (cmd == "USER") Reply(c, "331 OK");
            else if (cmd == "PASS") Reply(c, "230 Logged in");
            else if (cmd == "SYST") Reply(c, "215 UNIX Type: L8");
            else if (cmd == "FEAT") { Reply(c, "211-Features:"); Reply(c, " UTF8"); Reply(c, " MLSD"); Reply(c, " SIZE"); Reply(c, " PASV"); Reply(c, " EPSV"); Reply(c, "211 End"); }
            else if (cmd == "OPTS") Reply(c, "200 OK");
            else if (cmd == "NOOP") Reply(c, "200 OK");
            else if (cmd == "TYPE" || cmd == "MODE" || cmd == "STRU") Reply(c, "200 OK");
            else if (cmd == "PWD" || cmd == "XPWD") Reply(c, "257 \"" + cwd + "\"");
            else if (cmd == "CWD" || cmd == "XCWD" || cmd == "CDUP") {
                if (cmd == "CDUP") v = Normalize(cwd, "..");
                bool ok = v == "/" || v == "/JoyLauncher" || (Visible(v) && IsDir(Real(v)));
                if (ok) { cwd = v; Reply(c, "250 OK"); }
                else Reply(c, "550 No such directory");
            } else if (cmd == "PASV" || cmd == "EPSV") {
                if (pasv != FBAD) FCLOSE(pasv);
                int port;
                std::string ip;
                pasv = OpenPasv(c, port, ip);
                if (pasv == FBAD) { Reply(c, "425 Cannot open data port"); continue; }
                if (cmd == "EPSV") {
                    Reply(c, "229 Entering Extended Passive Mode (|||" + std::to_string(port) + "|)");
                } else {
                    std::string h = ip;
                    std::replace(h.begin(), h.end(), '.', ',');
                    Reply(c, "227 Entering Passive Mode (" + h + "," + std::to_string(port / 256) + "," + std::to_string(port % 256) + ")");
                }
            } else if (cmd == "LIST" || cmd == "NLST" || cmd == "MLSD") {
                if (!arg.empty() && arg[0] == '-') v = cwd;  // "LIST -a" 등
                if (!Visible(v)) { Reply(c, "550 Not allowed"); continue; }
                Reply(c, "150 Listing");
                fsock d = AcceptData(pasv);
                if (d == FBAD) { Reply(c, "425 No data connection"); continue; }
                std::string out;
                for (const auto &e : ListVirtual(v)) {
                    if (cmd == "NLST") out += e.name + "\r\n";
                    else if (cmd == "MLSD") out += std::string("type=") + (e.dir ? "dir" : "file") + ";size=" + std::to_string(e.size) + "; " + e.name + "\r\n";
                    else {
                        char b[64];
                        snprintf(b, sizeof(b), "%c%s 1 joy joy %12lld Jan 01 00:00 ", e.dir ? 'd' : '-', e.dir ? "rwxr-xr-x" : "rw-r--r--", e.size);
                        out += b + e.name + "\r\n";
                    }
                }
                SendAll(d, out.data(), out.size());
                FCLOSE(d);
                Reply(c, "226 Done");
            } else if (cmd == "SIZE") {
                long long sz;
                bool dir;
                if (Visible(v) && Stat(Real(v), sz, dir) && !dir) Reply(c, "213 " + std::to_string(sz));
                else Reply(c, "550 No such file");
            } else if (cmd == "MKD" || cmd == "XMKD") {
                if (!Writable(v + "/")) { Reply(c, "550 Not allowed"); continue; }
                if (IsDir(Real(v)) || MkDir(Real(v)) == 0) Reply(c, "257 \"" + v + "\" created");
                else Reply(c, "550 Cannot create");
            } else if (cmd == "STOR") {
                if (!Writable(v) || IsDir(Real(v))) { Reply(c, "550 Not allowed"); continue; }
                std::string r = Real(v), tmp = r + ".part";
                FILE *f = fopen(tmp.c_str(), "wb");
                if (!f) { Reply(c, "550 Cannot write"); continue; }
                Reply(c, "150 Receiving");
                fsock d = AcceptData(pasv);
                if (d == FBAD) { fclose(f); remove(tmp.c_str()); Reply(c, "425 No data connection"); continue; }
                long long total = 0;
                bool ok = true;
                std::vector<char> tb(65536);
                while (true) {
                    int w = WaitRead(d, 30000);
                    if (w <= 0) { ok = w == 0 ? false : ok; if (w < 0) ok = false; break; }
                    int n = recv(d, tb.data(), (int)tb.size(), 0);
                    if (n < 0) { ok = false; break; }
                    if (n == 0) break;
                    if (fwrite(tb.data(), 1, n, f) != (size_t)n) { ok = false; break; }
                    total += n;
                }
                FCLOSE(d);
                fclose(f);
                if (ok) {
                    remove(r.c_str());
                    ok = rename(tmp.c_str(), r.c_str()) == 0;
                }
                if (!ok) remove(tmp.c_str());
                if (ok) {
                    SDL_LockMutex(gMx);
                    gSt.files++;
                    gSt.bytes += total;
                    SDL_UnlockMutex(gMx);
                    Log(std::string("받음 ") + v.substr(13) + " (" + std::to_string((total + 1023) / 1024) + " KB)");
                    Reply(c, "226 Stored");
                } else {
                    Log("실패 " + v.substr(13));
                    Reply(c, "451 Transfer failed");
                }
            } else if (cmd == "RETR") {
                long long sz;
                bool dir;
                if (!Visible(v) || !Stat(Real(v), sz, dir) || dir) { Reply(c, "550 No such file"); continue; }
                FILE *f = fopen(Real(v).c_str(), "rb");
                if (!f) { Reply(c, "550 Cannot read"); continue; }
                Reply(c, "150 Sending");
                fsock d = AcceptData(pasv);
                if (d == FBAD) { fclose(f); Reply(c, "425 No data connection"); continue; }
                std::vector<char> tb(65536);
                size_t n;
                bool ok = true;
                while ((n = fread(tb.data(), 1, tb.size(), f)) > 0)
                    if (!SendAll(d, tb.data(), n)) { ok = false; break; }
                fclose(f);
                FCLOSE(d);
                Reply(c, ok ? "226 Sent" : "451 Transfer failed");
            } else if (cmd == "DELE") {
                if (!Writable(v) || IsDir(Real(v)) || remove(Real(v).c_str()) != 0) { Reply(c, "550 Cannot delete"); continue; }
                SDL_LockMutex(gMx);
                gSt.removed++;
                SDL_UnlockMutex(gMx);
                Log("지움 " + v.substr(13));
                Reply(c, "250 Deleted");
            } else if (cmd == "RMD" || cmd == "XRMD") {
                if (!Writable(v + "/") || v == "/JoyLauncher/themes" || rmdir(Real(v).c_str()) != 0) { Reply(c, "550 Cannot remove"); continue; }
                SDL_LockMutex(gMx);
                gSt.removed++;
                SDL_UnlockMutex(gMx);
                Log("지움 " + v.substr(13) + "/");
                Reply(c, "250 Removed");
            } else if (cmd == "RNFR") {
                if (!Writable(v)) { Reply(c, "550 Not allowed"); continue; }
                rnfr = v;
                Reply(c, "350 Ready");
            } else if (cmd == "RNTO") {
                if (rnfr.empty() || !Writable(v) || rename(Real(rnfr).c_str(), Real(v).c_str()) != 0) Reply(c, "550 Cannot rename");
                else Reply(c, "250 Renamed");
                rnfr.clear();
            } else if (cmd == "QUIT") {
                Reply(c, "221 Bye");
                break;
            } else {
                Reply(c, "502 Not implemented");
            }
        }
    }
done:
    if (pasv != FBAD) FCLOSE(pasv);
}

int Run(void *) {
    while (!gQuit) {
        if (WaitRead(gListen, -1) != 1) break;
        fsock c = accept(gListen, nullptr, nullptr);
        if (c == FBAD) continue;
        SDL_LockMutex(gMx);
        gSt.client = true;
        SDL_UnlockMutex(gMx);
        Log("PC 연결됨");
        Session(c);
        FCLOSE(c);
        SDL_LockMutex(gMx);
        gSt.client = false;
        SDL_UnlockMutex(gMx);
        Log("PC 연결 끝");
    }
    return 0;
}

std::string LocalIp() {
#ifdef __SWITCH__
    struct in_addr a;
    a.s_addr = gethostid();
    unsigned char *b = (unsigned char *)&a.s_addr;
    char buf[32];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return buf;
#else
    // 바깥 주소로 UDP 를 "연결"만 해 보고 내 주소를 읽음 (패킷은 보내지 않음)
    fsock s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s == FBAD) return "?";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    inet_pton(AF_INET, "192.168.0.1", &a.sin_addr);
    std::string out = "?";
    if (connect(s, (sockaddr *)&a, sizeof(a)) == 0) {
        sockaddr_in me{};
        socklen_t len = sizeof(me);
        if (getsockname(s, (sockaddr *)&me, &len) == 0) {
            char buf[32];
            inet_ntop(AF_INET, &me.sin_addr, buf, sizeof(buf));
            out = buf;
        }
    }
    FCLOSE(s);
    return out;
#endif
}

}  // namespace

bool FtpServerStart(const std::string &home, int port, std::string &err) {
    if (gThread) return true;
    if (!gMx) gMx = SDL_CreateMutex();
#ifdef _WIN32
    static bool wsa = false;
    if (!wsa) { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); wsa = true; }
#endif
#ifdef __SWITCH__
    if (!gSockInit) gSockInit = R_SUCCEEDED(socketInitializeDefault());
    if (!gSockInit) { err = "네트워크를 쓸 수 없습니다"; return false; }
#endif
    gHome = home;
    gSt = FtpServerStatus();
    gListen = socket(AF_INET, SOCK_STREAM, 0);
    if (gListen == FBAD) { err = "socket"; return false; }
    int one = 1;
    setsockopt(gListen, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    int used = 0;
    for (int p : {port, port + 1, port + 2, 5021}) {  // 5000 을 다른 FTP(sys-ftpd 등)가 쓰고 있으면 다음 번호
        a.sin_port = htons((unsigned short)p);
        if (bind(gListen, (sockaddr *)&a, sizeof(a)) == 0) { used = p; break; }
    }
    if (!used || listen(gListen, 2) != 0) {
        FCLOSE(gListen);
        gListen = FBAD;
        err = "포트를 열지 못했습니다";
        return false;
    }
    gSt.running = true;
    gSt.port = used;
    gSt.ip = LocalIp();
    gQuit = false;
    gThread = SDL_CreateThread(Run, "ftpd", nullptr);
    if (!gThread) { FCLOSE(gListen); gListen = FBAD; gSt.running = false; err = "thread"; return false; }
    return true;
}

void FtpServerStop() {
    if (!gThread) return;
    gQuit = true;
    SDL_WaitThread(gThread, nullptr);
    gThread = nullptr;
    if (gListen != FBAD) FCLOSE(gListen);
    gListen = FBAD;
    gSt.running = false;
#ifdef __SWITCH__
    if (gSockInit) { socketExit(); gSockInit = false; }
#endif
}

FtpServerStatus FtpServerGet() {
    if (!gMx) return gSt;
    SDL_LockMutex(gMx);
    FtpServerStatus s = gSt;
    SDL_UnlockMutex(gMx);
    return s;
}
