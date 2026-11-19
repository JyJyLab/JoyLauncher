// 안드로이드 플랫폼 층: 자바(com.jyjylab.joylauncher.JoyActivity)의 정적 함수를 JNI 로 부른다.
// 클래스·함수 번호는 Init() 때(메인 스레드) 찾아 두므로 다른 스레드(그림·영상 작업)에서도 부를 수 있다.
#ifdef __ANDROID__
#include "platform.h"
#include "video.h"

#include <SDL.h>
#include <android/log.h>
#include <jni.h>

#include <map>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "JoyLauncher", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "JoyLauncher", __VA_ARGS__)

namespace {
jclass gCls = nullptr;
std::map<std::string, jmethodID> gMethods;
const std::function<bool(long long, long long)> *gProgress = nullptr;

JNIEnv *Env() { return (JNIEnv *)SDL_AndroidGetJNIEnv(); }

jmethodID Method(JNIEnv *env, const char *name, const char *sig) {
    std::string key = std::string(name) + sig;
    auto it = gMethods.find(key);
    if (it != gMethods.end()) return it->second;
    jmethodID m = gCls ? env->GetStaticMethodID(gCls, name, sig) : nullptr;
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        m = nullptr;
    }
    if (!m) LOGE("java method not found: %s%s", name, sig);
    gMethods[key] = m;
    return m;
}

// 자바 예외가 났으면 지우고 true
bool Failed(JNIEnv *env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionDescribe();
    env->ExceptionClear();
    return true;
}

// 자바 문자열 → 표준 UTF-8. JNI 가 주는 글은 "변형 UTF-8" 이라 이모지 같은 4바이트 글자가
// 3바이트 두 개(대리 쌍)로 온다 → 합쳐서 4바이트로 (그대로 두면 글꼴이 깨진 글자로 그림)
std::string Str(JNIEnv *env, jstring s) {
    if (!s) return "";
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string in = c ? c : "", out;
    if (c) env->ReleaseStringUTFChars(s, c);
    env->DeleteLocalRef(s);
    out.reserve(in.size());
    const unsigned char *p = (const unsigned char *)in.data();
    size_t n = in.size();
    for (size_t i = 0; i < n;) {
        if (i + 5 < n && p[i] == 0xED && (p[i + 1] & 0xF0) == 0xA0 && p[i + 3] == 0xED && (p[i + 4] & 0xF0) == 0xB0) {
            unsigned hi = 0xD800 | ((p[i + 1] & 0x0F) << 6) | (p[i + 2] & 0x3F);
            unsigned lo = 0xDC00 | ((p[i + 4] & 0x0F) << 6) | (p[i + 5] & 0x3F);
            unsigned cp = 0x10000 + ((hi - 0xD800) << 10) + (lo - 0xDC00);
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
            i += 6;
        } else if (i + 1 < n && p[i] == 0xC0 && p[i + 1] == 0x80) {
            i += 2;  // 변형 UTF-8 의 NUL
        } else {
            out += (char)p[i++];
        }
    }
    return out;
}

// 표준 UTF-8 → 자바 문자열 (NewStringUTF 는 4바이트 글자(이모지)를 못 받으므로 바이트 배열로 넘겨 자바에서 바꿈)
jstring J(JNIEnv *env, const std::string &s) {
    jbyteArray arr = env->NewByteArray((jsize)s.size());
    env->SetByteArrayRegion(arr, 0, (jsize)s.size(), (const jbyte *)s.data());
    jmethodID m = Method(env, "utf8", "([B)Ljava/lang/String;");
    jstring out = m ? (jstring)env->CallStaticObjectMethod(gCls, m, arr) : nullptr;
    env->DeleteLocalRef(arr);
    if (Failed(env) || !out) return env->NewStringUTF("");
    return out;
}

std::vector<std::string> StrArray(JNIEnv *env, jobjectArray arr) {
    std::vector<std::string> out;
    if (!arr) return out;
    jsize n = env->GetArrayLength(arr);
    for (jsize i = 0; i < n; i++) out.push_back(Str(env, (jstring)env->GetObjectArrayElement(arr, i)));
    env->DeleteLocalRef(arr);
    return out;
}

jobjectArray JArray(JNIEnv *env, const std::vector<std::string> &v) {
    jclass sc = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray((jsize)v.size(), sc, nullptr);
    for (size_t i = 0; i < v.size(); i++) {
        jstring s = J(env, v[i]);
        env->SetObjectArrayElement(arr, (jsize)i, s);
        env->DeleteLocalRef(s);
    }
    env->DeleteLocalRef(sc);
    return arr;
}

std::string CallStr(const char *name) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, name, "()Ljava/lang/String;");
    if (!m) return "";
    jstring s = (jstring)env->CallStaticObjectMethod(gCls, m);
    if (Failed(env)) return "";
    return Str(env, s);
}
}  // namespace

extern "C" JNIEXPORT jboolean JNICALL Java_com_jyjylab_joylauncher_JoyActivity_nativeProgress(JNIEnv *, jclass, jlong got, jlong total) {
    if (gProgress && *gProgress) return (*gProgress)((long long)got, (long long)total) ? JNI_TRUE : JNI_FALSE;
    return JNI_TRUE;
}

// ---- 미리보기 영상: 자바(JoyVideo)의 영상 스레드가 부른다
extern "C" JNIEXPORT void JNICALL Java_com_jyjylab_joylauncher_JoyActivity_nativeVideoState(JNIEnv *, jclass, jint token, jint state, jint audio) {
    VideoSinkState((int)token, (int)state, audio != 0);
}

// ImageReader 의 YUV_420_888 그림. 판마다 버퍼가 따로 오고, 보이는 부분(left, top, w, h)만 쓴다
extern "C" JNIEXPORT void JNICALL Java_com_jyjylab_joylauncher_JoyActivity_nativeVideoYuv(JNIEnv *env, jclass, jint token, jobject yb, jobject ub,
                                                                                         jobject vb, jint yStride, jint uvStride, jint uvPix,
                                                                                         jint left, jint top, jint w, jint h) {
    if (!yb || !ub || !vb) return;
    const unsigned char *y = (const unsigned char *)env->GetDirectBufferAddress(yb);
    const unsigned char *u = (const unsigned char *)env->GetDirectBufferAddress(ub);
    const unsigned char *v = (const unsigned char *)env->GetDirectBufferAddress(vb);
    long long yCap = env->GetDirectBufferCapacity(yb), uCap = env->GetDirectBufferCapacity(ub), vCap = env->GetDirectBufferCapacity(vb);
    if (!y || !u || !v || w <= 0 || h <= 0 || left < 0 || top < 0 || yStride <= 0 || uvStride <= 0 || uvPix <= 0) return;
    // 버퍼가 알려 준 크기보다 작으면 읽지 않는다 (홀수 크기는 짝수로 줄여 한 번 더 확인)
    for (int pass = 0; pass < 2; pass++) {
        int cw = (w + 1) / 2, ch = (h + 1) / 2;
        long long yOff = (long long)top * yStride + left;
        long long cOff = (long long)(top / 2) * uvStride + (long long)(left / 2) * uvPix;
        long long yNeed = yOff + (long long)(h - 1) * yStride + w;
        long long cNeed = cOff + (long long)(ch - 1) * uvStride + (long long)(cw - 1) * uvPix + 1;
        if (yNeed <= yCap && cNeed <= uCap && cNeed <= vCap) {
            VideoSinkYuv((int)token, w, h, y + yOff, yStride, u + cOff, v + cOff, uvStride, uvPix);
            return;
        }
        w &= ~1;
        h &= ~1;
        if (w <= 0 || h <= 0) return;
    }
}

// YUV 로 읽을 수 없는 기기: RGBA_8888 그림 (한 점 4바이트)
extern "C" JNIEXPORT void JNICALL Java_com_jyjylab_joylauncher_JoyActivity_nativeVideoRgba(JNIEnv *env, jclass, jint token, jobject pb, jint stride,
                                                                                          jint pix, jint left, jint top, jint w, jint h) {
    if (!pb) return;
    const unsigned char *p = (const unsigned char *)env->GetDirectBufferAddress(pb);
    long long cap = env->GetDirectBufferCapacity(pb);
    if (!p || pix != 4 || w <= 0 || h <= 0 || left < 0 || top < 0 || stride <= 0) return;
    long long off = (long long)top * stride + (long long)left * 4;
    if (off + (long long)(h - 1) * stride + (long long)w * 4 > cap) return;
    VideoSinkRgbx((int)token, w, h, p + off, stride);
}

namespace plat {

void Init() {
    JNIEnv *env = Env();
    jobject act = (jobject)SDL_AndroidGetActivity();
    if (!env || !act) {
        LOGE("no activity");
        return;
    }
    jclass local = env->GetObjectClass(act);
    gCls = (jclass)env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    env->DeleteLocalRef(act);
    // 다른 스레드가 부르기 전에 함수 번호를 모두 찾아 둔다 (이후 gMethods 는 읽기만)
    static const char *const all[][2] = {
        {"utf8", "([B)Ljava/lang/String;"},
        {"primaryStorage", "()Ljava/lang/String;"},
        {"storageVolumes", "()[Ljava/lang/String;"},
        {"hasStorageAccess", "()Z"},
        {"requestStorageAccess", "()V"},
        {"internalDataRoot", "()Ljava/lang/String;"},
        {"packageInstalled", "(Ljava/lang/String;Ljava/lang/String;)Z"},
        {"launchableApps", "()[Ljava/lang/String;"},
        {"saveAppIcon", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Z"},
        {"startAm", "([Ljava/lang/String;)Ljava/lang/String;"},
        {"startApp", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
        {"battery", "()I"},
        {"systemLanguage", "()Ljava/lang/String;"},
        {"decodeImage", "(Ljava/lang/String;I)[I"},
        {"videoStart", "(Ljava/lang/String;ZI)V"},
        {"videoStop", "()V"},
        {"httpGet", "(Ljava/lang/String;)[Ljava/lang/String;"},
        {"httpDownload", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;"},
        {"httpAbort", "(Z)V"},
        {"finishApp", "()V"},
    };
    for (const auto &m : all) Method(env, m[0], m[1]);
}

std::string PrimaryStorage() {
    static std::string p;
    if (p.empty()) p = CallStr("primaryStorage");
    return p.empty() ? "/storage/emulated/0" : p;
}

std::vector<Volume> Volumes() {
    std::vector<Volume> out;
    JNIEnv *env = Env();
    jmethodID m = Method(env, "storageVolumes", "()[Ljava/lang/String;");
    if (m) {
        jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(gCls, m);
        if (!Failed(env))
            for (const std::string &line : StrArray(env, arr)) {  // "경로<TAB>이름"
                size_t t = line.find('\t');
                if (t == std::string::npos) continue;
                Volume v;
                v.path = line.substr(0, t);
                v.id = line.substr(t + 1);
                v.primary = v.id == "primary";
                out.push_back(v);
            }
    }
    if (out.empty()) {
        Volume v;
        v.path = PrimaryStorage();
        v.id = "primary";
        v.primary = true;
        out.push_back(v);
    }
    return out;
}

bool HasStorageAccess() {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "hasStorageAccess", "()Z");
    if (!m) return false;
    jboolean r = env->CallStaticBooleanMethod(gCls, m);
    return !Failed(env) && r;
}

void RequestStorageAccess() {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "requestStorageAccess", "()V");
    if (!m) return;
    env->CallStaticVoidMethod(gCls, m);
    Failed(env);
}

std::string PrivateDir() {
    const char *p = SDL_AndroidGetInternalStoragePath();
    return p ? p : "/data/local/tmp";
}

std::string InternalDataRoot() {
    static std::string p;
    if (p.empty()) p = CallStr("internalDataRoot");
    return p.empty() ? "/data/user/0" : p;
}

bool ReadAsset(const std::string &name, std::vector<char> &out) {
    SDL_RWops *rw = SDL_RWFromFile(name.c_str(), "rb");
    if (!rw) return false;
    Sint64 n = SDL_RWsize(rw);
    out.resize(n > 0 ? (size_t)n : 0);
    size_t got = 0;
    while (n > 0 && got < (size_t)n) {
        size_t r = SDL_RWread(rw, out.data() + got, 1, (size_t)n - got);
        if (r == 0) break;
        got += r;
    }
    SDL_RWclose(rw);
    return n > 0 && got == (size_t)n;
}

bool PackageInstalled(const std::string &package, const std::string &activity) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "packageInstalled", "(Ljava/lang/String;Ljava/lang/String;)Z");
    if (!m) return false;
    jstring p = J(env, package), a = J(env, activity);
    jboolean r = env->CallStaticBooleanMethod(gCls, m, p, a);
    env->DeleteLocalRef(p);
    env->DeleteLocalRef(a);
    return !Failed(env) && r;
}

std::vector<App> LaunchableApps() {
    std::vector<App> out;
    JNIEnv *env = Env();
    jmethodID m = Method(env, "launchableApps", "()[Ljava/lang/String;");
    if (!m) return out;
    jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(gCls, m);
    if (Failed(env)) return out;
    for (const std::string &line : StrArray(env, arr)) {  // "이름<TAB>패키지<TAB>액티비티"
        size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        App a;
        a.label = line.substr(0, t1);
        a.package = line.substr(t1 + 1, t2 - t1 - 1);
        a.activity = line.substr(t2 + 1);
        out.push_back(a);
    }
    return out;
}

bool SaveAppIcon(const std::string &package, const std::string &activity, const std::string &outPng, int size) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "saveAppIcon", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;I)Z");
    if (!m) return false;
    jstring p = J(env, package), a = J(env, activity), o = J(env, outPng);
    jboolean r = env->CallStaticBooleanMethod(gCls, m, p, a, o, (jint)size);
    env->DeleteLocalRef(p);
    env->DeleteLocalRef(a);
    env->DeleteLocalRef(o);
    return !Failed(env) && r;
}

bool StartActivity(const std::vector<std::string> &amArgs, std::string &err) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "startAm", "([Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { err = "startAm"; return false; }
    jobjectArray arr = JArray(env, amArgs);
    jstring r = (jstring)env->CallStaticObjectMethod(gCls, m, arr);
    env->DeleteLocalRef(arr);
    if (Failed(env)) { err = "java exception"; return false; }
    err = Str(env, r);  // null(빈 글) = 성공
    return err.empty();
}

bool StartApp(const std::string &package, const std::string &activity, std::string &err) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "startApp", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { err = "startApp"; return false; }
    jstring p = J(env, package), a = J(env, activity);
    jstring r = (jstring)env->CallStaticObjectMethod(gCls, m, p, a);
    env->DeleteLocalRef(p);
    env->DeleteLocalRef(a);
    if (Failed(env)) { err = "java exception"; return false; }
    err = Str(env, r);
    return err.empty();
}

int Battery(bool &charging) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "battery", "()I");
    if (!m) return -1;
    jint v = env->CallStaticIntMethod(gCls, m);  // 잔량 + (충전 중이면 1000), 모르면 -1
    if (Failed(env) || v < 0) return -1;
    charging = v >= 1000;
    return v % 1000;
}

bool SystemKorean() { return CallStr("systemLanguage") == "ko"; }

SDL_Surface *DecodeImage(const std::string &path, int maxSide) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "decodeImage", "(Ljava/lang/String;I)[I");
    if (!m) return nullptr;
    jstring p = J(env, path);
    jintArray arr = (jintArray)env->CallStaticObjectMethod(gCls, m, p, (jint)maxSide);
    env->DeleteLocalRef(p);
    if (Failed(env) || !arr) return nullptr;
    SDL_Surface *s = nullptr;
    jsize n = env->GetArrayLength(arr);
    if (n > 2) {
        jint wh[2];
        env->GetIntArrayRegion(arr, 0, 2, wh);
        // 자바의 int 하나 = 0xAARRGGBB (알파 미리 곱하지 않음) = SDL_PIXELFORMAT_ARGB8888
        if (wh[0] > 0 && wh[1] > 0 && (long long)wh[0] * wh[1] + 2 == n) {
            s = SDL_CreateRGBSurfaceWithFormat(0, wh[0], wh[1], 32, SDL_PIXELFORMAT_ARGB8888);
            if (s) {
                for (int y = 0; y < wh[1]; y++)
                    env->GetIntArrayRegion(arr, 2 + y * wh[0], wh[0], (jint *)((Uint8 *)s->pixels + (size_t)y * s->pitch));
            }
        }
    }
    env->DeleteLocalRef(arr);
    return s;
}

void VideoOpen(const std::string &path, bool sound, int token) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "videoStart", "(Ljava/lang/String;ZI)V");
    if (!m) { VideoSinkState(token, -1, false); return; }
    jstring p = J(env, path);
    env->CallStaticVoidMethod(gCls, m, p, (jboolean)sound, (jint)token);
    env->DeleteLocalRef(p);
    if (Failed(env)) VideoSinkState(token, -1, false);
}

void VideoClose() {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "videoStop", "()V");
    if (!m) return;
    env->CallStaticVoidMethod(gCls, m);
    Failed(env);
}

bool HttpGet(const std::string &url, std::string &out, std::string &err) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "httpGet", "(Ljava/lang/String;)[Ljava/lang/String;");
    if (!m) { err = "httpGet"; return false; }
    jstring u = J(env, url);
    jobjectArray arr = (jobjectArray)env->CallStaticObjectMethod(gCls, m, u);
    env->DeleteLocalRef(u);
    if (Failed(env)) { err = "java exception"; return false; }
    std::vector<std::string> r = StrArray(env, arr);  // [오류("" = 성공), 내용]
    if (r.size() < 2) { err = "httpGet"; return false; }
    err = r[0];
    out = r[1];
    return err.empty();
}

bool HttpDownload(const std::string &url, const std::string &path, std::string &err,
                  const std::function<bool(long long, long long)> &progress) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "httpDownload", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
    if (!m) { err = "httpDownload"; return false; }
    jstring u = J(env, url), p = J(env, path);
    gProgress = &progress;  // 자바가 받는 동안 같은 스레드에서 nativeProgress 를 부른다
    jstring r = (jstring)env->CallStaticObjectMethod(gCls, m, u, p);
    gProgress = nullptr;
    env->DeleteLocalRef(u);
    env->DeleteLocalRef(p);
    if (Failed(env)) { err = "java exception"; return false; }
    err = Str(env, r);
    return err.empty();
}

void HttpAbort(bool on) {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "httpAbort", "(Z)V");
    if (!m) return;
    env->CallStaticVoidMethod(gCls, m, (jboolean)on);
    Failed(env);
}

void Finish() {
    JNIEnv *env = Env();
    jmethodID m = Method(env, "finishApp", "()V");
    if (!m) return;
    env->CallStaticVoidMethod(gCls, m);
    Failed(env);
}

const std::vector<std::string> &LastStart() {
    static const std::vector<std::string> none;
    return none;
}

}  // namespace plat
#endif  // __ANDROID__
