// 원인 파악용 동작 기록: /JoyLauncher/debug_log.txt (백로그 #26)
// 런처 → 다른 홈브루(ftpd 등) → 런처 복귀 → 다른 앱 실행 사이에 생기는 오류 종료를 추적하려고 남긴다.
//  START  : 같은 hbloader 프로세스로 돌아왔는지(pid), 앞 앱이 남긴 스레드·메모리,
//           지난번 이후 새로 생긴 Atmosphère 오류 기록(crash/fatal/erpt_reports) 요약
//  LAUNCH : 예약한 다음 nro
//  EXIT   : 다음에 올라갈 것, 그때 남아서 넘어가는 스레드·메모리
//  CRASH  : 런처가 올라와 있는 동안 어느 스레드든 CPU 예외가 나면 레지스터·스택·스레드·메모리 지도를 쓰고
//           예전과 같이 시스템에 넘긴다(0xF801 → 오류 창, creport 기록). 동작은 바꾸지 않음
// userAppInit 은 C++ 정적 초기화보다 먼저 불리므로 이 파일의 전역은 생성자 없는 것만 쓴다.
// 예외 처리 안에서는 malloc·stdio 를 쓰지 않고 fs 서비스로 바로 쓴다(앞 앱이 남긴 스레드일 수도 있음).
#include "debuglog.h"

#ifdef __SWITCH__
#include <switch.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <strings.h>

#ifndef APP_VERSION_STR
#define APP_VERSION_STR "0.0.0-dev"
#endif

extern "C" u32 __nx_applet_exit_mode;  // main.cpp

extern "C" {
// 아래 예외 진입점(어셈블리)이 쓰는 저장 칸과 스택
alignas(16) u64 joy_exc_save[88];  // x9~x28, x29·진입 sp, fpcr·fpsr, q0~q31
alignas(16) u8 joy_exc_stack[0x8000];
void JoyExceptionLog(u32 desc, const ThreadExceptionFrameA64 *f, const u64 *hi);
}
static_assert(sizeof joy_exc_stack == 0x8000, "어셈블리의 스택 끝 주소와 맞출 것");

namespace {
const char *const kLog = "/JoyLauncher/debug_log.txt";
const char *const kLogOld = "/JoyLauncher/debug_log_old.txt";
const char *const kState = "/JoyLauncher/debug_state.txt";
const s64 kLogMax = 512 * 1024;   // 시작할 때 이보다 크면 _old 로 돌림
const u32 kTvMagic = 0x21545624;  // libnx ThreadVars 표식 (TLS 칸 + 0x1E0, 다음 4바이트 = 스레드 핸들)
const int kMaxRegions = 320, kMaxThreads = 64;
const char *const kReportDirs[] = {"crash_reports", "fatal_reports", "erpt_reports"};

FsFileSystem *g_fs;          // fsdev 의 sdmc
volatile bool g_crashReady;  // 예외 기록 가능 (userAppInit ~ userAppExit)
u64 g_pid, g_mainTid;
u64 g_firstTid;              // 이번 런처가 만드는 스레드 ID 시작값. 이보다 작은 ID(메인 제외) = 그 전부터 있던 스레드
u64 g_imgLo, g_imgHi;        // 런처 nro 이미지 (코드 ~ bss)
u64 g_heapLo, g_heapHi;      // hbloader 가 넘긴 힙
u64 g_backLo;                // [g_backLo, g_heapLo) = hbloader 가 런처 이미지를 읽어 둔 원래 자리 (잠긴 게 정상)
u64 g_stackLo, g_stackHi;    // 메인 스레드 스택
u64 g_startTick;
s64 g_startLocal;            // 시작 시각 (지역 시각, 1970-01-01 기준 초)
int g_starts;                // 이 프로세스에서 런처가 시작된 횟수
char g_next[640];            // 예약한 다음 nro
char *g_seen;                // 이미 본 Atmosphère 기록 ("seen=폴더/이름\n" 줄들, malloc)
char g_text[32768];          // START·LAUNCH·EXIT 글 (메인 스레드)
alignas(16) u8 g_dummyStack[0x1000];

// 고정 버퍼에 글 쌓기 (malloc 없음: 예외 처리 안에서도 씀)
struct Out {
    char *p;
    size_t cap, n;
    void C(char c) {
        if (n + 1 < cap) p[n++] = c;
        p[n] = 0;
    }
    void S(const char *s) {
        while (s && *s) C(*s++);
    }
    void S(const char *s, size_t len) {
        for (size_t i = 0; i < len && s[i]; i++) C(s[i]);
    }
    void Hex(u64 v, int width = 1) {  // 0x + 16진수 (width 자리까지 0 채움)
        char t[16];
        int k = 0;
        do {
            t[k++] = "0123456789abcdef"[v & 15];
            v >>= 4;
        } while (v && k < 16);
        while (k < width && k < 16) t[k++] = '0';
        S("0x");
        while (k) C(t[--k]);
    }
    void Dec(u64 v, int width = 1) {
        char t[20];
        int k = 0;
        do {
            t[k++] = char('0' + v % 10);
            v /= 10;
        } while (v && k < 20);
        while (k < width && k < 20) t[k++] = '0';
        while (k) C(t[--k]);
    }
    void Size(u64 v) {  // 1.5 GiB / 1.5 MiB / 64 KiB / 512 B
        if (v >= (1ull << 30)) { Dec(v >> 30); C('.'); Dec(((v & ((1ull << 30) - 1)) * 10) >> 30); S(" GiB"); }
        else if (v >= (1ull << 20)) { Dec(v >> 20); C('.'); Dec(((v & 0xFFFFF) * 10) >> 20); S(" MiB"); }
        else if (v >= 1024) { Dec(v >> 10); S(" KiB"); }
        else { Dec(v); S(" B"); }
    }
    void Secs(u64 ticks) {  // 19.2 MHz 틱 → 12.345 s
        u64 ms = ticks / 19200;
        Dec(ms / 1000);
        C('.');
        Dec(ms % 1000, 3);
        S(" s");
    }
    void Time(s64 t) {  // 1970 기준 초 → YYYY-MM-DD HH:MM:SS
        s64 days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
        s64 sec = t - days * 86400;
        days += 719468;
        s64 era = (days >= 0 ? days : days - 146096) / 146097;
        u64 doe = (u64)(days - era * 146097);
        u64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        u64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        u64 mp = (5 * doy + 2) / 153;
        u64 d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
        s64 y = (s64)yoe + era * 400 + (m <= 2);
        Dec((u64)y, 4); C('-'); Dec(m, 2); C('-'); Dec(d, 2); C(' ');
        Dec((u64)sec / 3600, 2); C(':'); Dec((u64)sec / 60 % 60, 2); C(':'); Dec((u64)sec % 60, 2);
    }
};

s64 DaysFromCivil(s64 y, int m, int d) {
    y -= m <= 2;
    s64 era = (y >= 0 ? y : y - 399) / 400;
    s64 yoe = y - era * 400;
    s64 doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    s64 doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

s64 Now() { return g_startLocal + (s64)((armGetSystemTick() - g_startTick) / 19200000); }

// ---- 파일 (fs 서비스 직접) ----
bool Append(const char *path, const char *data, size_t len) {
    if (!g_fs || !len) return false;
    fsFsCreateFile(g_fs, path, 0, 0);  // 이미 있으면 실패 → 무시
    FsFile f;
    if (R_FAILED(fsFsOpenFile(g_fs, path, FsOpenMode_Write | FsOpenMode_Append, &f))) return false;
    s64 size = 0;
    fsFileGetSize(&f, &size);
    Result rc = fsFileWrite(&f, size, data, len, FsWriteOption_Flush);
    fsFileClose(&f);
    return R_SUCCEEDED(rc);
}

bool WriteAll(const char *path, const char *a, size_t alen, const char *b, size_t blen) {
    fsFsDeleteFile(g_fs, path);
    if (R_FAILED(fsFsCreateFile(g_fs, path, 0, 0))) return false;
    FsFile f;
    if (R_FAILED(fsFsOpenFile(g_fs, path, FsOpenMode_Write | FsOpenMode_Append, &f))) return false;
    Result rc = fsFileWrite(&f, 0, a, alen, FsWriteOption_None);
    if (R_SUCCEEDED(rc) && blen) rc = fsFileWrite(&f, alen, b, blen, FsWriteOption_None);
    fsFileFlush(&f);
    fsFileClose(&f);
    return R_SUCCEEDED(rc);
}

// 파일 앞부분(max 바이트까지)을 malloc 해서 읽음, 끝에 0. 실패하면 nullptr
char *ReadAll(const char *path, size_t max, size_t *outLen) {
    FsFile f;
    if (R_FAILED(fsFsOpenFile(g_fs, path, FsOpenMode_Read, &f))) return nullptr;
    s64 size = 0;
    fsFileGetSize(&f, &size);
    if (size < 0) size = 0;
    if ((u64)size > max) size = (s64)max;
    char *p = (char *)malloc((size_t)size + 1);
    u64 got = 0;
    if (p && R_FAILED(fsFileRead(&f, 0, p, (u64)size, FsReadOption_None, &got))) got = 0;
    fsFileClose(&f);
    if (!p) return nullptr;
    p[got] = 0;
    if (outLen) *outLen = got;
    return p;
}

// ---- 메모리·스레드 ----
bool Query(u64 addr, MemoryInfo &mi) {
    u32 pi;
    return R_SUCCEEDED(svcQueryMemory(&mi, &pi, addr));
}

// addr ~ addr+len 이 읽을 수 있는 한 구역 안인지 (예외 처리 중에 잘못 읽어 또 죽지 않게)
bool Readable(u64 addr, u64 len) {
    MemoryInfo mi;
    if (!Query(addr, mi) || !(mi.perm & Perm_R)) return false;
    u32 t = mi.type & 0xFF;
    if (t == MemType_Unmapped || t == MemType_Io) return false;
    return addr + len > addr && addr + len <= mi.addr + mi.size;
}

const char *TypeName(u32 t) {
    static const char *const kNames[] = {
        "Unmapped", "Io", "Normal", "Code", "CodeData", "Heap", "SharedMem", "Alias", "ModuleCode", "ModuleCodeData",
        "IpcBuffer0", "Stack", "ThreadLocal", "TransferMemIsolated", "TransferMem", "ProcessMem", "Reserved",
        "IpcBuffer1", "IpcBuffer3", "KernelStack", "CodeReadOnly", "CodeWritable", "Coverage", "Insecure"};
    return t < sizeof kNames / sizeof *kNames ? kNames[t] : "?";
}

void PermAttr(Out &o, u32 perm, u32 attr) {
    o.C(perm & Perm_R ? 'r' : '-');
    o.C(perm & Perm_W ? 'w' : '-');
    o.C(perm & Perm_X ? 'x' : '-');
    if (attr & MemAttr_IsBorrowed) o.S(" locked");
    if (attr & MemAttr_IsIpcMapped) o.S(" ipc");
    if (attr & MemAttr_IsDeviceMapped) o.S(" device");
    if (attr & MemAttr_IsUncached) o.S(" uncached");
}

// 주소 뒤에 붙이는 설명: [joy+0x1234 ModuleCode r-x] (joy+ 오프셋은 addr2line -e joylauncher.elf 로 확인)
void Region(Out &o, u64 a) {
    MemoryInfo mi;
    o.S(" [");
    if (a >= g_imgLo && a < g_imgHi) { o.S("joy+"); o.Hex(a - g_imgLo); o.C(' '); }
    else if (a >= g_heapLo && a < g_heapHi) { o.S("heap+"); o.Hex(a - g_heapLo); o.C(' '); }
    if (Query(a, mi)) {
        o.S(TypeName(mi.type & 0xFF));
        o.C(' ');
        PermAttr(o, mi.perm, mi.attr);
    } else {
        o.C('?');
    }
    o.C(']');
}

const char *Who(u64 tid) {
    if (tid == g_mainTid) return "main";
    if (!g_firstTid) return "?";
    return tid < g_firstTid ? "EARLIER-APP" : "joy";
}

struct Th {
    u64 tls, tid, pc, lr, sp;
    Handle h;
    int st;    // 1 실행·대기 중, 2 끝났는데 핸들이 안 닫힘, 3 알 수 없음 (tid 0 = 핸들이 닫힌 옛 칸)
    bool ctx;  // pc·lr·sp 읽음
};

// libnx 스레드 찾기: TLS 페이지의 0x200 칸마다 ThreadVars 표식 → 핸들 → 살아 있는지.
// ctx: 살아 있는 다른 스레드는 잠깐 멈춰 pc·lr·sp 를 읽고 바로 다시 돌림 (예외 처리 안에서는 안 함)
int ScanThreads(Th *out, int max, bool ctx) {
    int n = 0;
    for (u64 addr = 0; n < max;) {
        MemoryInfo mi;
        if (!Query(addr, mi)) break;
        if ((mi.type & 0xFF) == MemType_ThreadLocal && (mi.perm & Perm_R)) {
            for (u64 s = mi.addr; s + 0x200 <= mi.addr + mi.size && n < max; s += 0x200) {
                const u32 *tv = (const u32 *)(s + 0x1E0);
                if (tv[0] != kTvMagic) continue;
                Th t = {};
                t.tls = s;
                t.h = tv[1];
                if (R_FAILED(svcGetThreadId(&t.tid, t.h))) {
                    t.tid = 0;
                } else {
                    s32 idx;
                    Result rc = svcWaitSynchronization(&idx, &t.h, 1, 0);
                    t.st = R_SUCCEEDED(rc) ? 2 : R_VALUE(rc) == KERNELRESULT(TimedOut) ? 1 : 3;
                }
                bool dup = false;
                for (int k = 0; k < n && t.tid; k++) dup |= out[k].tid == t.tid;
                if (dup) continue;
                if (ctx && t.st == 1 && t.tid != g_mainTid &&
                    R_SUCCEEDED(svcSetThreadActivity(t.h, ThreadActivity_Paused))) {
                    ThreadContext c;
                    if (R_SUCCEEDED(svcGetThreadContext3(&c, t.h))) {
                        t.ctx = true;
                        t.pc = c.pc.x;
                        t.lr = c.lr;
                        t.sp = c.sp;
                    }
                    svcSetThreadActivity(t.h, ThreadActivity_Runnable);
                }
                out[n++] = t;
            }
        }
        u64 next = mi.addr + mi.size;
        if (next <= addr) break;
        addr = next;
    }
    return n;
}

void ThreadsOut(Out &o, const Th *t, int n, u64 selfTls) {
    static const char *const kSt[] = {"", " running/waiting", " ended (handle not closed)", " state?"};
    int stale = 0;
    for (int i = 0; i < n; i++) {
        if (!t[i].tid) { stale++; continue; }
        o.S("  tid ");
        o.Hex(t[i].tid);
        o.C(' ');
        o.S(Who(t[i].tid));
        o.S(kSt[t[i].st & 3]);
        if (selfTls && t[i].tls == selfTls) o.S("  <== this thread");
        if (t[i].ctx) {
            o.S("\n      pc "); o.Hex(t[i].pc, 10); Region(o, t[i].pc);
            o.S("\n      lr "); o.Hex(t[i].lr, 10); Region(o, t[i].lr);
            o.S("\n      sp "); o.Hex(t[i].sp, 10); Region(o, t[i].sp);
        }
        o.C('\n');
    }
    if (stale) { o.S("  ("); o.Dec(stale); o.S(" TLS slot(s) of closed threads)\n"); }
}

struct Rg {
    u64 addr, size;
    u32 type, attr, perm;
};

int ScanMap(Rg *out, int max) {
    int n = 0;
    for (u64 addr = 0; n < max;) {
        MemoryInfo mi;
        if (!Query(addr, mi)) break;
        if ((mi.type & 0xFF) != MemType_Unmapped) out[n++] = {mi.addr, mi.size, mi.type & 0xFF, mi.attr, mi.perm};
        u64 next = mi.addr + mi.size;
        if (next <= addr) break;
        addr = next;
    }
    return n;
}

bool ImageSource(const Rg &r) {  // hbloader 가 런처 이미지를 읽어 코드로 옮긴 원래 힙 자리
    return r.type == MemType_Heap && g_heapLo &&
           (r.addr + r.size == g_heapLo || (r.addr >= g_backLo && r.addr + r.size <= g_heapLo));
}

// 남은 것으로 볼 구역: 스레드 스택 미러, 잠기거나 장치·IPC 에 물린 힙, 남의 전송 메모리. nullptr = 정상
const char *Leftover(const Rg &r) {
    if (r.type == MemType_MappedMemory) {
        if (r.addr <= g_stackLo && g_stackHi <= r.addr + r.size) return nullptr;  // 메인 스레드 스택
        return "thread stack (mirror)";
    }
    if (r.type == MemType_TransferMemIsolated || r.type == MemType_TransferMem) return "mapped transfer memory";
    if (r.type != MemType_Heap || ImageSource(r)) return nullptr;
    if (r.attr & MemAttr_IsDeviceMapped) return "heap mapped to a device (GPU etc.)";
    if (r.attr & MemAttr_IsBorrowed) return "locked heap (thread stack or transfer memory source)";
    if (r.attr & MemAttr_IsIpcMapped) return "heap in use as IPC buffer";
    if (r.perm != Perm_Rw) return "heap with changed permission";
    return nullptr;
}

void RegionLine(Out &o, const Rg &r, const char *why) {
    o.S("  ");
    o.Hex(r.addr, 10);
    o.C(' ');
    o.Size(r.size);
    o.C(' ');
    o.S(TypeName(r.type));
    o.C(' ');
    PermAttr(o, r.perm, r.attr);
    if (why) { o.S("  "); o.S(why); }
    o.C('\n');
}

void MemOut(Out &o) {
    u64 tot = 0, used = 0;
    svcGetInfo(&tot, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    o.S("memory: used "); o.Size(used); o.S(" of "); o.Size(tot);
    o.S("; heap "); o.Hex(g_heapLo, 10); o.C(' '); o.Size(g_heapHi - g_heapLo);
    o.S("; joy image "); o.Hex(g_imgLo, 10); o.C(' '); o.Size(g_imgHi - g_imgLo); o.C('\n');
    u64 h = 0;
    if (R_FAILED(svcGetInfo(&h, InfoType_ResourceLimit, INVALID_HANDLE, 0)) || !h) return;
    static const char *const kRes[] = {"memory", "threads", "events", "transfer-mem", "sessions"};
    o.S("limits (used/max):");
    for (int i = 0; i < 5; i++) {
        s64 cur = 0, lim = 0;
        if (R_FAILED(svcGetResourceLimitCurrentValue(&cur, (Handle)h, (LimitableResource)i)) ||
            R_FAILED(svcGetResourceLimitLimitValue(&lim, (Handle)h, (LimitableResource)i)))
            continue;
        o.C(' ');
        o.S(kRes[i]);
        o.C(' ');
        if (i == 0) { o.Size((u64)cur); o.C('/'); o.Size((u64)lim); }
        else { o.Dec((u64)cur); o.C('/'); o.Dec((u64)lim); }
    }
    o.C('\n');
    svcCloseHandle((Handle)h);
}

// START·EXIT 공통 점검: 스레드와 남은 메모리. atStart = 런처 스레드가 아직 없을 때(앞 앱 것만 보임)
void CheckOut(Out &o, bool atStart) {
    static Th th[kMaxThreads];
    static Rg rg[kMaxRegions];
    int n = ScanThreads(th, kMaxThreads, true);
    int alive = 0, codeGone = 0, ended = 0, regions = 0;
    u64 bytes = 0;
    o.S("threads (libnx):\n");
    ThreadsOut(o, th, n, 0);
    for (int i = 0; i < n; i++) {
        if (!th[i].tid || th[i].tid == g_mainTid) continue;
        if (th[i].st == 1) {
            alive++;
            MemoryInfo mi;
            if (th[i].ctx && (!Query(th[i].pc, mi) || !(mi.perm & Perm_X))) codeGone++;
        } else if (th[i].st == 2) {
            ended++;
        }
    }
    int m = ScanMap(rg, kMaxRegions);
    o.S("leftover memory:\n");
    for (int i = 0; i < m; i++) {
        Rg q = rg[i];
        // 런처 이미지 원래 자리(잠김)와 성질이 같아 하나로 합쳐 보이는 구역은 힙 시작 뒤쪽만 셈
        if (q.type == MemType_Heap && g_heapLo && q.addr >= g_backLo && q.addr < g_heapLo && q.addr + q.size > g_heapLo) {
            q.size = q.addr + q.size - g_heapLo;
            q.addr = g_heapLo;
        }
        const char *why = Leftover(q);
        if (!why) continue;
        if (++regions <= 40) RegionLine(o, q, why);
        bytes += q.size;
    }
    if (!regions) o.S("  none\n");
    else if (regions > 40) { o.S("  ... "); o.Dec(regions - 40); o.S(" more\n"); }
    o.S("check: ");
    if (!alive && !ended && !regions) {
        o.S(atStart ? "OK - nothing left by earlier apps\n" : "OK - nothing left for the next app\n");
        return;
    }
    o.S(atStart ? "LEFTOVER from earlier app(s) -" : "LEFTOVER handed to the next app -");
    if (alive) {
        o.C(' '); o.Dec(alive); o.S(" thread(s) still alive");
        if (codeGone) { o.S(" ("); o.Dec(codeGone); o.S(" in unloaded code: crash when it runs)"); }
        o.C(';');
    }
    if (ended) { o.C(' '); o.Dec(ended); o.S(" ended thread(s) never closed;"); }
    if (regions) { o.C(' '); o.Dec(regions); o.S(" memory range(s), "); o.Size(bytes); }
    o.C('\n');
}

// ---- 상태 파일 (/JoyLauncher/debug_state.txt) ----
struct Prev {
    bool valid;
    u64 pid;
    int starts;
    char event[16], target[640];
};

void CopyField(const char *s, const char *key, char *out, size_t cap) {
    out[0] = 0;
    size_t kl = strlen(key);
    for (const char *p = s; p && *p;) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t len = strcspn(v, "\r\n");
            if (len >= cap) len = cap - 1;
            memcpy(out, v, len);
            out[len] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
}

void SaveState(const char *event, const char *target) {
    char head[1024];
    int len = snprintf(head, sizeof head, "# Joy Launcher debug_log.txt state\npid=0x%llx\nstarts=%d\nevent=%s\ntarget=%s\n",
                       (unsigned long long)g_pid, g_starts, event, target);
    if (len < 0) return;
    if (len >= (int)sizeof head) len = sizeof head - 1;
    WriteAll(kState, head, (size_t)len, g_seen, g_seen ? strlen(g_seen) : 0);
}

const char *AppletName(AppletType t) {
    switch (t) {
    case AppletType_Application: return "Application";
    case AppletType_SystemApplication: return "SystemApplication";
    case AppletType_LibraryApplet: return "LibraryApplet (album)";
    case AppletType_SystemApplet: return "SystemApplet";
    case AppletType_OverlayApplet: return "OverlayApplet";
    default: return "?";
    }
}

void PrevOut(Out &o, const Prev &p) {
    o.S("previous: ");
    if (!p.valid) { o.S("no record (first start with this log)\n"); return; }
    bool exited = !strcmp(p.event, "exit");
    if (p.pid == g_pid) {
        o.S("same process, Joy start #"); o.Dec((u64)g_starts); o.S(" here - ");
        if (exited) { o.S("returned from "); o.S(p.target[0] ? p.target : "?"); }
        else { o.S("last record was "); o.S(p.event); }
        o.S(", loader result "); o.Hex(envGetLastLoadResult()); o.C('\n');
        return;
    }
    o.S("new process (was pid "); o.Hex(p.pid); o.S(") - ");
    if (exited && !strcmp(p.target, "HOME")) o.S("Joy had exited to HOME");
    else if (exited) { o.S("that process ended after Joy handed over to "); o.S(p.target); o.S(" (it crashed or aborted, or the software was closed from HOME)"); }
    else if (!strcmp(p.event, "launch")) { o.S("that process ended after Joy reserved "); o.S(p.target); o.S(" but before Joy finished exiting"); }
    else o.S("that process ended while Joy was running (Joy crashed, or the software was closed from HOME)");
    o.C('\n');
}

// ---- Atmosphère 오류 기록 ----
struct Names {
    char **v;
    int n, cap;
};

void Push(Names &a, const char *s) {
    if (a.n == a.cap) {
        int cap = a.cap ? a.cap * 2 : 64;
        char **v = (char **)realloc(a.v, (size_t)cap * sizeof *v);
        if (!v) return;
        a.v = v;
        a.cap = cap;
    }
    if (char *d = strdup(s)) a.v[a.n++] = d;
}

void ListReports(const char *dir, Names &out) {
    static FsDirectoryEntry ents[16];
    char path[FS_MAX_PATH];
    snprintf(path, sizeof path, "/atmosphere/%s", dir);
    FsDir d;
    if (R_FAILED(fsFsOpenDirectory(g_fs, path, FsDirOpenMode_ReadFiles | FsDirOpenMode_NoFileSize, &d))) return;
    for (;;) {
        s64 got = 0;
        if (R_FAILED(fsDirRead(&d, &got, 16, ents)) || got <= 0) break;
        for (s64 i = 0; i < got; i++) {
            size_t len = strnlen(ents[i].name, sizeof ents[i].name);
            const bool erpt = !strcmp(dir, "erpt_reports");  // erpt 는 확장자 없는 UUID 이름
            if (!len || (!erpt && (len < 5 || strcasecmp(ents[i].name + len - 4, ".log")))) continue;
            snprintf(path, sizeof path, "%s/%.*s", dir, (int)len, ents[i].name);
            Push(out, path);
        }
    }
    fsDirClose(&d);
}

bool Seen(const char *state, const char *name) {
    if (!state) return false;
    for (const char *p = state; (p = strstr(p, "seen=")); p += 5) {
        size_t len = strlen(name);
        if ((p == state || p[-1] == '\n') && !strncmp(p + 5, name, len) && (p[5 + len] == '\n' || p[5 + len] == '\r'))
            return true;
    }
    return false;
}

bool Starts(const char *s, size_t n, const char *k) {
    size_t kl = strlen(k);
    return n >= kl && !memcmp(s, k, kl);
}

// creport·fatal 보고서(글자) 앞부분: Result·프로세스·예외·죽은 스레드 pc/lr·스택 추적·모듈. 레지스터 x 와 메모리 덤프는 뺌
void SummText(Out &o, const char *txt) {
    int lines = 0;
    for (const char *p = txt; *p && lines < 48;) {
        size_t len = strcspn(p, "\n");
        const char *s = p, *e = p + len;
        p = *e ? e + 1 : e;
        while (s < e && (*s == ' ' || *s == '\t')) s++;
        while (e > s && (e[-1] == '\r' || e[-1] == ' ')) e--;
        size_t n = (size_t)(e - s);
        if (!n) continue;
        if (Starts(s, n, "Thread Report:")) break;
        if (Starts(s, n, "X[") || (s[0] == 'X' && n > 1 && s[1] >= '0' && s[1] <= '9') || Starts(s, n, "Module Id:") ||
            Starts(s, n, "Stack Dump:") || Starts(s, n, "TLS Dump:") || Starts(s, n, "TLS Address:"))
            continue;
        bool bin = false;  // 이진 데이터가 섞인 줄 (fatal 보고서 끝 등)
        for (size_t i = 0; i < n && !bin; i++) bin = (unsigned char)s[i] < 0x20 && s[i] != '\t';
        if (bin) continue;
        bool dump = n > 13 && s[12] == ' ';  // "002ae4cacbe0 70 cc ..." 덤프 줄
        for (int i = 0; dump && i < 12; i++) dump = strchr("0123456789abcdefABCDEF", s[i]) != nullptr;
        if (dump) continue;
        o.S("    ");
        for (size_t i = 0; i < n; i++)  // 칸 맞춤 공백은 하나로
            if (s[i] != ' ' || (i + 1 < n && s[i + 1] != ' ')) o.C(s[i]);
        o.C('\n');
        lines++;
    }
}

// erpt 보고서(msgpack): 문자열 칸 key 의 값
bool MsgStr(const u8 *d, size_t n, const char *key, char *out, size_t cap) {
    size_t kl = strlen(key);
    for (size_t i = 1; i + kl < n; i++) {
        if (d[i - 1] != (0xA0 | kl) || memcmp(d + i, key, kl)) continue;
        size_t j = i + kl, len;
        u8 b = d[j++];
        if ((b & 0xE0) == 0xA0) len = b & 0x1F;
        else if (b == 0xD9 && j < n) len = d[j++];
        else if (b == 0xDA && j + 1 < n) { len = (size_t)d[j] << 8 | d[j + 1]; j += 2; }
        else continue;
        if (j + len > n) return false;
        if (len >= cap) len = cap - 1;
        for (size_t k = 0; k < len; k++) out[k] = d[j + k] >= 0x20 && d[j + k] < 0x7F ? (char)d[j + k] : '?';
        out[len] = 0;
        return true;
    }
    return false;
}

void SummErpt(Out &o, const u8 *d, size_t n) {
    static const char *const kKeys[] = {"ErrorCode", "ProgramId"};
    char v[64];
    o.S("    ");
    for (const char *k : kKeys) {
        if (!MsgStr(d, n, k, v, sizeof v)) continue;
        o.S(k); o.C(' '); o.S(v); o.S("  ");
    }
    o.C('\n');
}

// 지난번 START 뒤로 새로 생긴 Atmosphère 오류 기록을 요약해 쓰고, 본 목록(g_seen)을 갱신.
// 처음이면(상태 파일 없음) 종류마다 가장 최근 것 몇 개를 보여 줌
void WriteReports(const char *oldState) {
    static char rbuf[12288];
    Names all = {};
    for (const char *d : kReportDirs) ListReports(d, all);
    qsort(all.v, (size_t)all.n, sizeof *all.v,
          [](const void *a, const void *b) { return strcmp(*(char *const *)b, *(char *const *)a); });  // 최근 것 먼저
    size_t cap = 1;
    for (int i = 0; i < all.n; i++) cap += strlen(all.v[i]) + 6;
    free(g_seen);
    g_seen = (char *)malloc(cap);
    if (g_seen) {
        g_seen[0] = 0;
        char *w = g_seen;
        for (int i = 0; i < all.n; i++) w += sprintf(w, "seen=%s\n", all.v[i]);
    }
    const bool first = !oldState;
    int fresh = 0;
    for (int i = 0; i < all.n; i++) fresh += !Seen(oldState, all.v[i]);
    Out o{g_text, sizeof g_text, 0};
    o.S("atmosphere reports: ");
    if (first) { o.Dec((u64)all.n); o.S(" on SD (no earlier record: newest shown)\n"); }
    else if (!fresh) o.S("no new ones\n");
    else { o.Dec((u64)fresh); o.S(" new since last start\n"); }
    Append(kLog, o.p, o.n);
    int shown[3] = {}, listed = 0;
    for (int i = 0; i < all.n; i++) {
        if (!first && Seen(oldState, all.v[i])) continue;
        int kind = 0;
        while (kind < 2 && strncmp(all.v[i], kReportDirs[kind], strlen(kReportDirs[kind]))) kind++;
        Out r{rbuf, sizeof rbuf, 0};
        if (first && kind == 2) continue;  // erpt 이름(UUID)은 시간 순이 아니라 처음에는 개수만
        if (shown[kind] >= 3) {  // 나머지는 이름만 (처음이면 생략)
            if (!first && listed++ < 10) { r.S("  "); r.S(all.v[i]); r.C('\n'); Append(kLog, r.p, r.n); }
            continue;
        }
        shown[kind]++;
        r.S("  ");
        r.S(all.v[i]);
        r.C('\n');
        char path[FS_MAX_PATH];
        snprintf(path, sizeof path, "/atmosphere/%s", all.v[i]);
        size_t len = 0;
        if (char *data = ReadAll(path, 96 * 1024, &len)) {
            if (kind == 2) SummErpt(r, (const u8 *)data, len);
            else SummText(r, data);
            free(data);
        }
        Append(kLog, r.p, r.n);
    }
    for (int i = 0; i < all.n; i++) free(all.v[i]);
    free(all.v);
}

void DummyThread(void *) { svcExitThread(); }

// ---- 크래시 ----
const char *DescName(u32 d) {
    switch (d) {
    case ThreadExceptionDesc_InstructionAbort: return "instruction abort";
    case ThreadExceptionDesc_MisalignedPC: return "misaligned pc";
    case ThreadExceptionDesc_MisalignedSP: return "misaligned sp";
    case ThreadExceptionDesc_SError: return "SError";
    case ThreadExceptionDesc_BadSVC: return "bad svc";
    case ThreadExceptionDesc_Trap: return "trap";
    case ThreadExceptionDesc_Other: return "other (data abort etc.)";
    default: return "?";
    }
}

void EsrOut(Out &o, u32 esr) {
    const u32 ec = esr >> 26, iss = esr & 0x1FFFFFF;
    switch (ec) {
    case 0x00: o.S("undefined instruction"); return;
    case 0x0E: o.S("illegal execution state"); return;
    case 0x15: o.S("svc"); return;
    case 0x18: o.S("system register access"); return;
    case 0x20: case 0x21: o.S("instruction abort"); break;
    case 0x22: o.S("pc alignment"); return;
    case 0x24: case 0x25: o.S("data abort"); break;
    case 0x26: o.S("sp alignment"); return;
    case 0x2C: o.S("floating point"); return;
    case 0x2F: o.S("SError"); return;
    case 0x3C: o.S("brk instruction"); return;
    default: o.S("EC "); o.Hex(ec); return;
    }
    const u32 fsc = iss & 0x3F;
    if (fsc >= 0x04 && fsc <= 0x07) o.S(", translation fault (address not mapped)");
    else if (fsc >= 0x09 && fsc <= 0x0B) o.S(", access flag fault");
    else if (fsc >= 0x0D && fsc <= 0x0F) o.S(", permission fault (mapped but not allowed)");
    else if (fsc == 0x21) o.S(", alignment fault");
    else { o.S(", fsc "); o.Hex(fsc); }
    if (ec >= 0x24) o.S(iss & (1u << 6) ? ", write" : ", read");
}
}  // namespace

// 예외 진입점: libnx 의 약한 __libnx_exception_entry 대신. 커널 → hbloader → 지금 올라온 nro(런처) 진입점 → 여기.
// 들어올 때 x0 = 예외 종류, x1 = 커널이 저장한 x0~x8·lr·sp·pc·esr·far, x9~x29 = 그 순간 값, sp = 아주 작은 임시 스택.
// 우리 스택으로 바꿔 기록한 뒤 x9~x29·NEON 을 되돌리고 0xF801(처리 안 함)을 돌려준다 → 커널이 예전과 똑같이 처리.
// 커널은 한 프로세스에서 예외 처리를 한 스레드씩만 들여보내므로 여러 스레드가 함께 죽어도 기록이 섞이지 않는다.
__asm__(R"(
    .pushsection .text.__libnx_exception_entry, "ax", %progbits
    .align 2
    .global __libnx_exception_entry
    .type __libnx_exception_entry, %function
__libnx_exception_entry:
    cbz     x1, 1f
    adrp    x2, joy_exc_save
    add     x2, x2, :lo12:joy_exc_save
    stp     x9, x10, [x2, #0x00]
    stp     x11, x12, [x2, #0x10]
    stp     x13, x14, [x2, #0x20]
    stp     x15, x16, [x2, #0x30]
    stp     x17, x18, [x2, #0x40]
    stp     x19, x20, [x2, #0x50]
    stp     x21, x22, [x2, #0x60]
    stp     x23, x24, [x2, #0x70]
    stp     x25, x26, [x2, #0x80]
    stp     x27, x28, [x2, #0x90]
    mov     x3, sp
    stp     x29, x3, [x2, #0xa0]
    mrs     x3, fpcr
    mrs     x4, fpsr
    stp     x3, x4, [x2, #0xb0]
    add     x3, x2, #0xc0
    stp     q0, q1, [x3, #0x000]
    stp     q2, q3, [x3, #0x020]
    stp     q4, q5, [x3, #0x040]
    stp     q6, q7, [x3, #0x060]
    stp     q8, q9, [x3, #0x080]
    stp     q10, q11, [x3, #0x0a0]
    stp     q12, q13, [x3, #0x0c0]
    stp     q14, q15, [x3, #0x0e0]
    stp     q16, q17, [x3, #0x100]
    stp     q18, q19, [x3, #0x120]
    stp     q20, q21, [x3, #0x140]
    stp     q22, q23, [x3, #0x160]
    stp     q24, q25, [x3, #0x180]
    stp     q26, q27, [x3, #0x1a0]
    stp     q28, q29, [x3, #0x1c0]
    stp     q30, q31, [x3, #0x1e0]
    adrp    x3, joy_exc_stack + 0x8000
    add     x3, x3, :lo12:joy_exc_stack + 0x8000
    mov     sp, x3
    mov     x29, xzr
    bl      JoyExceptionLog
    adrp    x2, joy_exc_save
    add     x2, x2, :lo12:joy_exc_save
    ldp     x3, x4, [x2, #0xb0]
    msr     fpcr, x3
    msr     fpsr, x4
    add     x3, x2, #0xc0
    ldp     q0, q1, [x3, #0x000]
    ldp     q2, q3, [x3, #0x020]
    ldp     q4, q5, [x3, #0x040]
    ldp     q6, q7, [x3, #0x060]
    ldp     q8, q9, [x3, #0x080]
    ldp     q10, q11, [x3, #0x0a0]
    ldp     q12, q13, [x3, #0x0c0]
    ldp     q14, q15, [x3, #0x0e0]
    ldp     q16, q17, [x3, #0x100]
    ldp     q18, q19, [x3, #0x120]
    ldp     q20, q21, [x3, #0x140]
    ldp     q22, q23, [x3, #0x160]
    ldp     q24, q25, [x3, #0x180]
    ldp     q26, q27, [x3, #0x1a0]
    ldp     q28, q29, [x3, #0x1c0]
    ldp     q30, q31, [x3, #0x1e0]
    ldp     x29, x3, [x2, #0xa0]
    mov     sp, x3
    ldp     x9, x10, [x2, #0x00]
    ldp     x11, x12, [x2, #0x10]
    ldp     x13, x14, [x2, #0x20]
    ldp     x15, x16, [x2, #0x30]
    ldp     x17, x18, [x2, #0x40]
    ldp     x19, x20, [x2, #0x50]
    ldp     x21, x22, [x2, #0x60]
    ldp     x23, x24, [x2, #0x70]
    ldp     x25, x26, [x2, #0x80]
    ldp     x27, x28, [x2, #0x90]
1:
    mov     w0, #0xf801
    svc     0x28
    b       .
    .size __libnx_exception_entry, . - __libnx_exception_entry
    .popsection
)");

// 예외 기록 (커널 예외 처리 상태, 위 진입점의 전용 스택 위). hi = x9~x29
extern "C" void JoyExceptionLog(u32 desc, const ThreadExceptionFrameA64 *f, const u64 *hi) {
    if (!g_crashReady || !g_fs) return;
    static char buf[48 * 1024];
    static Th th[kMaxThreads];
    static Rg rg[kMaxRegions];
    Out o{buf, sizeof buf, 0};
    const u64 now = armGetSystemTick();
    o.S("\n#### CRASH ");
    o.Time(g_startLocal + (s64)((now - g_startTick) / 19200000));
    o.S("  (Joy start + "); o.Secs(now - g_startTick); o.S(")\n");
    o.S("exception: "); o.S(DescName(desc)); o.S(" (desc "); o.Hex(desc); o.S(")  ESR "); o.Hex(f->esr);
    o.S(": "); EsrOut(o, f->esr); o.C('\n');
    u64 tid = 0;
    svcGetThreadId(&tid, CUR_THREAD_HANDLE);
    const u64 tls = (u64)armGetTls();
    o.S("thread: tid "); o.Hex(tid); o.C(' '); o.S(Who(tid));
    o.S(((const u32 *)(tls + 0x1E0))[0] == kTvMagic ? " (libnx thread)" : " (no libnx thread vars)");
    o.S("  [main "); o.Hex(g_mainTid); o.S(", this Joy's threads from "); o.Hex(g_firstTid); o.S("]\n");
    const char *const kKey[] = {"pc ", "lr ", "sp ", "far"};
    const u64 kv[] = {f->elr_el1, f->lr, f->sp, f->far};
    for (int i = 0; i < 4; i++) { o.S(kKey[i]); o.C(' '); o.Hex(kv[i], 10); Region(o, kv[i]); o.C('\n'); }
    u64 x[30];
    for (int i = 0; i < 9; i++) x[i] = f->cpu_gprs[i];
    for (int i = 9; i < 30; i++) x[i] = hi[i - 9];
    for (int i = 0; i < 30; i++) {
        o.S(i < 10 ? "x0" : "x"); o.Dec((u64)i); o.C(' '); o.Hex(x[i], 16);
        o.S(i % 4 == 3 || i == 29 ? "\n" : "  ");
    }
    o.S("backtrace (frame pointers):\n");
    u64 fp = x[29];
    for (int i = 0; i < 32 && fp; i++) {
        if ((fp & 7) || !Readable(fp, 16)) { o.S("  stop: fp "); o.Hex(fp, 10); o.S(" not readable\n"); break; }
        const u64 *fr = (const u64 *)fp;
        o.S("  #"); o.Dec((u64)i); o.C(' '); o.Hex(fr[1], 10); Region(o, fr[1]); o.C('\n');
        if (fr[0] <= fp) break;
        fp = fr[0];
    }
    if (Readable(f->sp, 128)) {
        o.S("stack at sp:\n");
        const u64 *sp = (const u64 *)f->sp;
        for (int i = 0; i < 16; i++) { o.S(i % 4 ? " " : "  "); o.Hex(sp[i], 16); if (i % 4 == 3) o.C('\n'); }
    }
    o.S("threads (libnx):\n");
    ThreadsOut(o, th, ScanThreads(th, kMaxThreads, false), tls);
    o.S("memory map:\n");
    int m = ScanMap(rg, kMaxRegions);
    for (int i = 0; i < m; i++) {
        const char *tag = nullptr;
        if (rg[i].addr >= g_imgLo && rg[i].addr < g_imgHi) tag = "joy image";
        else if (ImageSource(rg[i])) tag = "joy image source (locked by hbloader)";
        else if (rg[i].addr <= g_stackLo && g_stackHi <= rg[i].addr + rg[i].size) tag = "main thread stack";
        else if (f->sp >= rg[i].addr && f->sp - rg[i].addr < rg[i].size) tag = "crashed thread stack";
        else tag = Leftover(rg[i]);
        RegionLine(o, rg[i], tag);
    }
    o.S("#### END CRASH (the system error screen follows; creport also writes /atmosphere/crash_reports)\n");
    if (!Append(kLog, o.p, o.n)) Append("/JoyLauncher/debug_crash.txt", o.p, o.n);  // 기록 중에 죽어 파일이 열려 있을 때
    fsFsCommit(g_fs);
}

extern "C" void userAppInit(void) {
    g_startTick = armGetSystemTick();
    svcGetProcessId(&g_pid, CUR_PROCESS_HANDLE);
    svcGetThreadId(&g_mainTid, CUR_THREAD_HANDLE);
    // 시작하지 않는 스레드를 하나 만들어 ID 기준값을 얻음 (스레드 ID 는 커널 전역에서 늘어나는 값)
    Handle h;
    if (R_SUCCEEDED(svcCreateThread(&h, (void *)DummyThread, nullptr, g_dummyStack + sizeof g_dummyStack, 0x2C, -2))) {
        svcGetThreadId(&g_firstTid, h);
        svcCloseHandle(h);
    }
    MemoryInfo mi;
    if (Query((u64)&userAppInit, mi)) {
        g_imgLo = mi.addr;
        for (u64 p = mi.addr; Query(p, mi) && mi.addr == p; p = mi.addr + mi.size) {
            const u32 t = mi.type & 0xFF;
            if (t != MemType_ModuleCodeStatic && t != MemType_ModuleCodeMutable) break;
            g_imgHi = mi.addr + mi.size;
        }
    }
    if (envHasHeapOverride()) {
        g_heapLo = (u64)envGetHeapOverrideAddr();
        g_heapHi = g_heapLo + envGetHeapOverrideSize();
        g_backLo = g_imgHi > g_imgLo ? g_heapLo - (g_imgHi - g_imgLo) : g_heapLo;
    }
    if (Query((u64)__builtin_frame_address(0), mi)) {
        g_stackLo = mi.addr;
        g_stackHi = mi.addr + mi.size;
    }
    time_t now = time(nullptr);
    struct tm lt;
    if (localtime_r(&now, &lt))
        g_startLocal = DaysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 + lt.tm_hour * 3600 +
                       lt.tm_min * 60 + lt.tm_sec;
    g_fs = fsdevGetDeviceFileSystem("sdmc");
    if (!g_fs) return;
    fsFsCreateDirectory(g_fs, "/JoyLauncher");
    g_crashReady = true;

    FsFile f;
    s64 size = 0;
    if (R_SUCCEEDED(fsFsOpenFile(g_fs, kLog, FsOpenMode_Read, &f))) {
        fsFileGetSize(&f, &size);
        fsFileClose(&f);
    }
    if (size > kLogMax) {
        fsFsDeleteFile(g_fs, kLogOld);
        fsFsRenameFile(g_fs, kLog, kLogOld);
    }
    char *state = ReadAll(kState, 256 * 1024, nullptr);
    Prev p = {};
    if (state) {
        char v[32];
        CopyField(state, "pid", v, sizeof v);
        p.valid = v[0] != 0;
        p.pid = strtoull(v, nullptr, 0);
        CopyField(state, "starts", v, sizeof v);
        p.starts = atoi(v);
        CopyField(state, "event", p.event, sizeof p.event);
        CopyField(state, "target", p.target, sizeof p.target);
    }
    g_starts = p.valid && p.pid == g_pid ? p.starts + 1 : 1;

    Out o{g_text, sizeof g_text, 0};
    u64 prog = 0;
    svcGetInfo(&prog, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0);
    o.S("\n==== START "); o.Time(g_startLocal); o.S("  Joy " APP_VERSION_STR "  pid "); o.Hex(g_pid);
    o.S("  program "); o.Hex(prog, 16); o.S("  applet "); o.S(AppletName(appletGetAppletType())); o.C('\n');
    if (const char *a = (const char *)envGetArgv()) { o.S("argv: "); o.S(a, strnlen(a, 300)); o.C('\n'); }
    PrevOut(o, p);
    MemOut(o);
    CheckOut(o, true);
    Append(kLog, o.p, o.n);
    WriteReports(state);
    free(state);
    SaveState("start", "");
}

// libnx 가 main 이 끝난 뒤(전역 소멸자 다음), sdmc 를 내리기 전에 부름
extern "C" void userAppExit(void) {
    if (!g_fs) return;
    const char *target = g_next[0] ? g_next : __nx_applet_exit_mode == 1 ? "HOME" : "hbloader default (/hbmenu.nro)";
    Out o{g_text, sizeof g_text, 0};
    o.S("---- EXIT "); o.Time(Now()); o.S("  (Joy ran "); o.Secs(armGetSystemTick() - g_startTick); o.S(") -> ");
    o.S(target); o.C('\n');
    MemOut(o);
    CheckOut(o, false);
    Append(kLog, o.p, o.n);
    SaveState("exit", target);
    g_crashReady = false;  // 이 뒤로는 sdmc 가 내려감
    fsFsCommit(g_fs);
}

void DebugLogLaunch(const char *nro, const char *argv) {
    if (!g_fs) return;
    strncpy(g_next, nro, sizeof g_next - 1);
    Out o{g_text, sizeof g_text, 0};
    o.S("-- LAUNCH "); o.Time(Now()); o.C(' '); o.S(nro);
    if (argv) { o.S("  argv: "); o.S(argv, strnlen(argv, 400)); }
    o.C('\n');
    Append(kLog, o.p, o.n);
    SaveState("launch", nro);
}
#else
void DebugLogLaunch(const char *, const char *) {}
#endif
