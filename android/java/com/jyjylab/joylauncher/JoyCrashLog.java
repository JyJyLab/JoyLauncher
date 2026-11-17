// Joy Launcher (Android) — 꺼짐 기록: /JoyLauncher/crash_log.txt
// 스위치판의 debug_log.txt 처럼, 런처가 갑자기 꺼졌을 때 ADB 없이도 원인을 볼 수 있게 남긴다.
//  - 자바 예외로 꺼질 때: 그 자리에서 예외와 호출 순서를 적는다
//  - 네이티브 꺼짐(SIGSEGV·SIGABRT 등)·응답 없음: 다음에 켤 때 안드로이드가 보관한 종료 기록
//    (ApplicationExitInfo, 안드로이드 11 이상)과 그 프로세스의 마지막 로그를 적는다
package com.jyjylab.joylauncher;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;
import android.os.Environment;
import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.PrintWriter;
import java.io.RandomAccessFile;
import java.io.StringWriter;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

final class JoyCrashLog {
    private static final String TAG = "JoyLauncher";
    private static final long kMaxBytes = 256 * 1024;  // 넘으면 앞쪽을 버림
    private static String version = "?";

    private JoyCrashLog() {
    }

    static File file() {
        return new File(Environment.getExternalStorageDirectory(), "JoyLauncher/crash_log.txt");
    }

    /** 시작할 때 한 번: 자바 예외 기록 + 지난번 비정상 종료 기록 (뒤쪽은 다른 스레드에서) */
    static void install(final Context ctx) {
        try {
            version = ctx.getPackageManager().getPackageInfo(ctx.getPackageName(), 0).versionName;
        } catch (Throwable t) {
            // 모르면 ?
        }
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override
            public void uncaughtException(Thread t, Throwable e) {
                try {
                    StringWriter sw = new StringWriter();
                    e.printStackTrace(new PrintWriter(sw));
                    append("자바 예외로 꺼짐 / Java exception (thread " + t.getName() + ")\n" + sw);
                } catch (Throwable ignore) {
                    // 기록 실패는 무시
                }
                if (prev != null) prev.uncaughtException(t, e);
            }
        });
        if (Build.VERSION.SDK_INT < 30) return;
        final Context app = ctx.getApplicationContext();
        new Thread(new Runnable() {
            @Override
            public void run() {
                try {
                    recordPreviousExits(app);
                } catch (Throwable t) {
                    Log.w(TAG, "crash log: " + t);
                }
            }
        }, "JoyCrashLog").start();
    }

    // 지난번까지의 비정상 종료 중 아직 적지 않은 것
    private static void recordPreviousExits(Context ctx) throws Exception {
        if (Build.VERSION.SDK_INT < 30) return;
        ActivityManager am = (ActivityManager) ctx.getSystemService(Context.ACTIVITY_SERVICE);
        SharedPreferences sp = ctx.getSharedPreferences("crashlog", Context.MODE_PRIVATE);
        long last = sp.getLong("last", 0), newest = last;
        List<ApplicationExitInfo> list = am.getHistoricalProcessExitReasons(ctx.getPackageName(), 0, 10);
        List<ApplicationExitInfo> todo = new ArrayList<>();
        for (ApplicationExitInfo e : list) {
            if (e.getTimestamp() <= last) continue;
            newest = Math.max(newest, e.getTimestamp());
            int r = e.getReason();
            if (r == ApplicationExitInfo.REASON_CRASH || r == ApplicationExitInfo.REASON_CRASH_NATIVE || r == ApplicationExitInfo.REASON_ANR
                    || r == ApplicationExitInfo.REASON_SIGNALED || r == ApplicationExitInfo.REASON_INITIALIZATION_FAILURE
                    || r == ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE || r == ApplicationExitInfo.REASON_LOW_MEMORY)
                todo.add(e);
        }
        // 오래된 것부터 적는다
        for (int i = todo.size() - 1; i >= 0; i--) {
            ApplicationExitInfo e = todo.get(i);
            StringBuilder b = new StringBuilder();
            b.append("비정상 종료 / Abnormal exit: ").append(reasonName(e.getReason()))
                    .append("  (").append(stamp(e.getTimestamp())).append(", pid ").append(e.getPid()).append(")\n");
            if (e.getDescription() != null) b.append("설명 / description: ").append(e.getDescription()).append('\n');
            if (e.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE || e.getReason() == ApplicationExitInfo.REASON_SIGNALED)
                b.append("신호 / signal: ").append(e.getStatus()).append('\n');
            String log = processLog(e.getPid());
            if (!log.isEmpty()) b.append("마지막 로그 / last log lines:\n").append(log);
            append(b.toString());
        }
        if (newest > last) sp.edit().putLong("last", newest).apply();
    }

    private static String reasonName(int r) {
        switch (r) {
            case ApplicationExitInfo.REASON_CRASH: return "자바 예외 (CRASH)";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "네이티브 꺼짐 (CRASH_NATIVE)";
            case ApplicationExitInfo.REASON_ANR: return "응답 없음 (ANR)";
            case ApplicationExitInfo.REASON_SIGNALED: return "신호로 끝남 (SIGNALED)";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "시작 실패 (INITIALIZATION_FAILURE)";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "자원 과다 사용 (EXCESSIVE_RESOURCE_USAGE)";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "메모리 부족 (LOW_MEMORY)";
            default: return "reason " + r;
        }
    }

    // 그 프로세스의 마지막 로그 (경고 이상, 최대 60줄). 앱은 자기 프로세스의 로그만 읽을 수 있다
    private static String processLog(int pid) {
        StringBuilder out = new StringBuilder();
        Process p = null;
        try {
            p = Runtime.getRuntime().exec(new String[] {"logcat", "-d", "-v", "time", "--pid=" + pid, "*:W"});
            BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream(), StandardCharsets.UTF_8));
            ArrayList<String> lines = new ArrayList<>();
            String l;
            while ((l = r.readLine()) != null) {
                lines.add(l);
                if (lines.size() > 60) lines.remove(0);
            }
            for (String s : lines) out.append("  ").append(s).append('\n');
        } catch (Throwable t) {
            // 로그를 못 읽어도 종료 기록은 남긴다
        } finally {
            if (p != null) p.destroy();
        }
        return out.toString();
    }

    private static String stamp(long ms) {
        return new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US).format(new Date(ms));
    }

    private static synchronized void append(String entry) {
        File f = file();
        File dir = f.getParentFile();
        if (dir == null || (!dir.isDirectory() && !dir.mkdirs())) return;  // 저장소 권한이 아직 없음
        try {
            boolean fresh = !f.exists();
            FileOutputStream o = new FileOutputStream(f, true);
            if (fresh)
                o.write(("# Joy Launcher 꺼짐 기록 / crash log — 문제를 알릴 때 이 파일을 함께 보내 주세요 (newest at the bottom)\n")
                        .getBytes(StandardCharsets.UTF_8));
            o.write(("\n[" + stamp(System.currentTimeMillis()) + "] v" + version + " · Android " + Build.VERSION.RELEASE
                    + " · " + Build.MANUFACTURER + " " + Build.MODEL + "\n" + entry).getBytes(StandardCharsets.UTF_8));
            o.close();
            trim(f);
        } catch (Throwable t) {
            Log.w(TAG, "crash log write: " + t);
        }
    }

    // 너무 커지면 뒤쪽 kMaxBytes 만 남김
    private static void trim(File f) throws Exception {
        long n = f.length();
        if (n <= kMaxBytes) return;
        byte[] tail = new byte[(int) kMaxBytes];
        RandomAccessFile r = new RandomAccessFile(f, "r");
        r.seek(n - kMaxBytes);
        r.readFully(tail);
        r.close();
        int start = 0;
        while (start < tail.length && tail[start] != '\n') start++;  // 줄 중간부터 시작하지 않게
        FileOutputStream o = new FileOutputStream(f, false);
        o.write(tail, Math.min(start + 1, tail.length), tail.length - Math.min(start + 1, tail.length));
        o.close();
    }
}
