// 테마 빌더: 스위치 FTP(ftpd·sys-ftpd)로 테마 폴더 보내기 — 최소 FTP 클라이언트 (패시브 모드, 이진 전송)
#pragma once
#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

inline void NetInit() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        done = true;
    }
#endif
}

inline sock_t TcpConnect(const std::string &host, int port, std::string &err) {
    NetInit();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        err = "주소를 찾지 못함: " + host;
        return INVALID_SOCKET;
    }
    sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) { freeaddrinfo(res); err = "socket"; return INVALID_SOCKET; }
#ifdef _WIN32
    DWORD tv = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));
#else
    timeval tv{10, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    if (connect(s, res->ai_addr, (int)res->ai_addrlen) != 0) {
        freeaddrinfo(res);
        CLOSESOCK(s);
        err = "연결 실패: " + host + ":" + std::to_string(port);
        return INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return s;
}

struct Ftp {
    sock_t c = INVALID_SOCKET;
    std::string host, buf, last;
    ~Ftp() { Close(); }
    void Close() {
        if (c != INVALID_SOCKET) CLOSESOCK(c);
        c = INVALID_SOCKET;
    }
    bool SendAll(sock_t s, const char *d, size_t n) {
        while (n > 0) {
            int w = send(s, d, (int)std::min<size_t>(n, 65536), 0);
            if (w <= 0) return false;
            d += w;
            n -= (size_t)w;
        }
        return true;
    }
    // 응답 한 개 (여러 줄 응답 "123-" … "123 " 까지). 실패 -1
    int Reply() {
        last.clear();
        int code = -1;
        while (true) {
            size_t nl;
            while ((nl = buf.find('\n')) == std::string::npos) {
                char tmp[1024];
                int r = recv(c, tmp, sizeof(tmp), 0);
                if (r <= 0) return -1;
                buf.append(tmp, r);
            }
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            last += line + "\n";
            if (line.size() >= 4 && isdigit((unsigned char)line[0]) && isdigit((unsigned char)line[1]) && isdigit((unsigned char)line[2])) {
                int cd = atoi(line.substr(0, 3).c_str());
                if (code < 0) code = cd;
                if (line[3] == ' ' && cd == code) return code;
            }
        }
    }
    int Cmd(const std::string &cmd) {
        std::string l = cmd + "\r\n";
        if (!SendAll(c, l.data(), l.size())) return -1;
        return Reply();
    }
    bool Login(const std::string &h, int port, const std::string &user, const std::string &pass, std::string &err) {
        host = h;
        c = TcpConnect(h, port, err);
        if (c == INVALID_SOCKET) return false;
        if (Reply() / 100 != 2) { err = "환영 응답 없음: " + last; return false; }
        int r = Cmd("USER " + (user.empty() ? std::string("anonymous") : user));
        if (r == 331) r = Cmd("PASS " + (pass.empty() ? std::string("joy@") : pass));
        if (r / 100 != 2) { err = "로그인 실패: " + last; return false; }
        if (Cmd("TYPE I") / 100 != 2) { err = "TYPE I 실패: " + last; return false; }
        return true;
    }
    sock_t Pasv(std::string &err) {
        if (Cmd("PASV") != 227) { err = "PASV 실패: " + last; return INVALID_SOCKET; }
        size_t a = last.find('(');
        int h[6] = {0};
        if (a == std::string::npos || sscanf(last.c_str() + a + 1, "%d,%d,%d,%d,%d,%d", &h[0], &h[1], &h[2], &h[3], &h[4], &h[5]) != 6) {
            err = "PASV 응답: " + last;
            return INVALID_SOCKET;
        }
        // 서버가 알려 준 주소 대신 연결한 주소 사용 (0.0.0.0 을 알려 주는 서버 대비)
        return TcpConnect(host, h[4] * 256 + h[5], err);
    }
    long long Size(const std::string &path) {
        if (Cmd("SIZE " + path) != 213) return -1;
        return atoll(last.c_str() + 4);
    }
    void Mkd(const std::string &path) { Cmd("MKD " + path); }
    bool Stor(const std::string &path, const std::string &data, std::string &err) {
        sock_t d = Pasv(err);
        if (d == INVALID_SOCKET) return false;
        int r = Cmd("STOR " + path);
        if (r != 150 && r != 125) { CLOSESOCK(d); err = "STOR 거부: " + last; return false; }
        bool ok = SendAll(d, data.data(), data.size());
#ifdef _WIN32
        shutdown(d, SD_SEND);
#else
        shutdown(d, SHUT_WR);
#endif
        CLOSESOCK(d);
        r = Reply();
        if (!ok || r / 100 != 2) { err = "보내기 실패: " + path + " " + last; return false; }
        return true;
    }
};
