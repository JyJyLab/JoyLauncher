// Joy Launcher 안드로이드 액티비티: SDL 화면 + 런처 본체(C++)가 부르는 기기 기능.
// 정적 함수들은 source/platform_android.cpp 가 JNI 로 부른다 (이름·인자를 바꾸면 그쪽도 함께).
package com.jyjylab.joylauncher;

import android.Manifest;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.StrictMode;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.text.Collator;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;

public class JoyActivity extends SDLActivity {
    private static final String TAG = "JoyLauncher";
    private static JoyActivity self;
    private static volatile boolean httpAbort = false;

    // 받는 동안 C++ 쪽 진행 표시 (false = 취소)
    private static native boolean nativeProgress(long got, long total);

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        self = this;
        // 옛 롬 모음의 실행 명령에는 file:// 주소가 들어 있기도 하다: 그대로 보낼 수 있게 검사를 끔
        StrictMode.setVmPolicy(new StrictMode.VmPolicy.Builder().build());
        JoyCrashLog.install(this);  // 꺼짐 기록 (/JoyLauncher/crash_log.txt)
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onStop() {
        // 런처가 화면에서 물러남: 영상 소리가 남지 않게 (런처 본체가 먼저 멈추지만 여기서 한 번 더)
        JoyVideo.stopForPause();
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // 프로세스가 남아 있다가 다시 시작되면 C++ 전역 상태가 그대로라 오동작한다 → 항상 새 프로세스로 시작
        System.exit(0);
    }

    // ------------------------------------------------------------ 글자
    public static String utf8(byte[] b) {
        return new String(b, StandardCharsets.UTF_8);
    }

    // ------------------------------------------------------------ 저장소
    public static String primaryStorage() {
        return Environment.getExternalStorageDirectory().getAbsolutePath();
    }

    // "경로<TAB>이름" (이름 = primary 또는 SD 카드 번호: SAF 문서 주소에 쓰는 것)
    public static String[] storageVolumes() {
        ArrayList<String> out = new ArrayList<>();
        ArrayList<String> seen = new ArrayList<>();
        try {
            StorageManager sm = (StorageManager) self.getSystemService(Context.STORAGE_SERVICE);
            for (StorageVolume v : sm.getStorageVolumes()) {
                String st = v.getState();
                if (!Environment.MEDIA_MOUNTED.equals(st) && !Environment.MEDIA_MOUNTED_READ_ONLY.equals(st)) continue;
                File dir = null;
                if (Build.VERSION.SDK_INT >= 30) {
                    dir = v.getDirectory();
                } else {
                    try {
                        dir = (File) StorageVolume.class.getMethod("getPathFile").invoke(v);
                    } catch (Throwable t) {
                        dir = null;
                    }
                }
                if (dir == null) continue;
                String id = v.isPrimary() ? "primary" : v.getUuid();
                if (id == null) id = dir.getName();
                out.add(dir.getAbsolutePath() + "\t" + id);
                seen.add(dir.getAbsolutePath());
            }
        } catch (Throwable t) {
            Log.w(TAG, "storageVolumes: " + t);
        }
        // 위에서 못 얻은 저장소: 앱 전용 폴더 경로(/storage/XXXX-XXXX/Android/data/…)에서 앞부분만
        try {
            for (File f : self.getExternalFilesDirs(null)) {
                if (f == null) continue;
                String p = f.getAbsolutePath();
                int at = p.indexOf("/Android/data/");
                if (at <= 0) continue;
                String root = p.substring(0, at);
                if (seen.contains(root)) continue;
                boolean primary = root.equals(primaryStorage());
                out.add(root + "\t" + (primary ? "primary" : new File(root).getName()));
                seen.add(root);
            }
        } catch (Throwable t) {
            Log.w(TAG, "getExternalFilesDirs: " + t);
        }
        return out.toArray(new String[0]);
    }

    public static boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return self.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    public static void requestStorageAccess() {
        self.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    if (Build.VERSION.SDK_INT >= 30) {
                        try {
                            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                            i.setData(Uri.parse("package:" + self.getPackageName()));
                            self.startActivity(i);
                        } catch (Exception e) {  // 일부 기기: 앱별 화면이 없음 → 전체 목록 화면
                            self.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                        }
                    } else {
                        self.requestPermissions(
                                new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.READ_EXTERNAL_STORAGE}, 1);
                    }
                } catch (Throwable t) {
                    Log.w(TAG, "requestStorageAccess: " + t);
                }
            }
        });
    }

    // ES-DE 의 %INTERNALDATA% (/data/user/0): RetroArch 코어 경로를 만들 때 씀
    public static String internalDataRoot() {
        File parent = new File(self.getApplicationInfo().dataDir).getParentFile();
        return parent != null ? parent.getAbsolutePath() : "/data/user/0";
    }

    // ------------------------------------------------------------ 앱
    private static ComponentName component(String pkg, String activity) {
        return new ComponentName(pkg, activity.startsWith(".") ? pkg + activity : activity);
    }

    public static boolean packageInstalled(String pkg, String activity) {
        try {
            PackageManager pm = self.getPackageManager();
            if (activity == null || activity.isEmpty()) pm.getPackageInfo(pkg, 0);
            else pm.getActivityInfo(component(pkg, activity), 0);
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    // "이름<TAB>패키지<TAB>액티비티", 이름순 (런처 자신 제외)
    public static String[] launchableApps() {
        final PackageManager pm = self.getPackageManager();
        Intent i = new Intent(Intent.ACTION_MAIN, null);
        i.addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> list = pm.queryIntentActivities(i, 0);
        ArrayList<String[]> rows = new ArrayList<>();
        for (ResolveInfo ri : list) {
            if (ri.activityInfo == null || self.getPackageName().equals(ri.activityInfo.packageName)) continue;
            CharSequence label = ri.loadLabel(pm);
            String name = label == null ? ri.activityInfo.packageName : label.toString().replace('\t', ' ').replace('\n', ' ').trim();
            if (name.isEmpty()) name = ri.activityInfo.packageName;
            rows.add(new String[] {name, ri.activityInfo.packageName, ri.activityInfo.name});
        }
        final Collator col = Collator.getInstance();
        Collections.sort(rows, new Comparator<String[]>() {
            @Override
            public int compare(String[] a, String[] b) {
                int c = col.compare(a[0], b[0]);
                return c != 0 ? c : a[1].compareTo(b[1]);
            }
        });
        String[] out = new String[rows.size()];
        for (int k = 0; k < out.length; k++) out[k] = rows.get(k)[0] + "\t" + rows.get(k)[1] + "\t" + rows.get(k)[2];
        return out;
    }

    public static boolean saveAppIcon(String pkg, String activity, String outPng, int size) {
        try {
            PackageManager pm = self.getPackageManager();
            Drawable d = null;
            if (activity != null && !activity.isEmpty()) {
                try {
                    d = pm.getActivityIcon(component(pkg, activity));
                } catch (Throwable t) {
                    d = null;
                }
            }
            if (d == null) d = pm.getApplicationIcon(pkg);
            Bitmap bmp = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
            Canvas c = new Canvas(bmp);
            d.setBounds(0, 0, size, size);
            d.draw(c);
            File tmp = new File(outPng + ".tmp"), dst = new File(outPng);
            FileOutputStream fo = new FileOutputStream(tmp);
            boolean ok = bmp.compress(Bitmap.CompressFormat.PNG, 100, fo);
            fo.close();
            bmp.recycle();
            if (!ok) {
                tmp.delete();
                return false;
            }
            dst.delete();
            return tmp.renameTo(dst);
        } catch (Throwable t) {
            Log.w(TAG, "saveAppIcon " + pkg + ": " + t);
            return false;
        }
    }

    // am start 의 인자(start 다음부터)로 인텐트를 만든다. 안드로이드의 am 과 같은 옵션 이름
    private static Intent parseAm(String[] a) throws Exception {
        Intent intent = new Intent();
        Uri data = null;
        String type = null;
        int i = 0;
        while (i < a.length) {
            String o = a[i++];
            switch (o) {
                case "-a": intent.setAction(a[i++]); break;
                case "-d": data = Uri.parse(a[i++]); break;
                case "-t": type = a[i++]; break;
                case "-i": i++; break;
                case "-c": intent.addCategory(a[i++]); break;
                case "-e":
                case "--es": { String k = a[i++]; intent.putExtra(k, a[i++]); break; }
                case "--esn": intent.putExtra(a[i++], (String) null); break;
                case "--ei": { String k = a[i++]; intent.putExtra(k, Integer.decode(a[i++]).intValue()); break; }
                case "--eu": { String k = a[i++]; intent.putExtra(k, Uri.parse(a[i++])); break; }
                case "--ecn": {
                    String k = a[i++];
                    ComponentName cn = ComponentName.unflattenFromString(a[i++]);
                    if (cn == null) throw new IllegalArgumentException("Bad component name");
                    intent.putExtra(k, cn);
                    break;
                }
                case "--eia": {
                    String k = a[i++];
                    String[] s = a[i++].split(",");
                    int[] v = new int[s.length];
                    for (int n = 0; n < s.length; n++) v[n] = Integer.decode(s[n].trim());
                    intent.putExtra(k, v);
                    break;
                }
                case "--eial": {
                    String k = a[i++];
                    ArrayList<Integer> v = new ArrayList<>();
                    for (String s : a[i++].split(",")) v.add(Integer.decode(s.trim()));
                    intent.putExtra(k, v);
                    break;
                }
                case "--el": { String k = a[i++]; intent.putExtra(k, Long.valueOf(a[i++]).longValue()); break; }
                case "--ela": {
                    String k = a[i++];
                    String[] s = a[i++].split(",");
                    long[] v = new long[s.length];
                    for (int n = 0; n < s.length; n++) v[n] = Long.valueOf(s[n].trim());
                    intent.putExtra(k, v);
                    break;
                }
                case "--elal": {
                    String k = a[i++];
                    ArrayList<Long> v = new ArrayList<>();
                    for (String s : a[i++].split(",")) v.add(Long.valueOf(s.trim()));
                    intent.putExtra(k, v);
                    break;
                }
                case "--ef": { String k = a[i++]; intent.putExtra(k, Float.valueOf(a[i++]).floatValue()); break; }
                case "--efa": {
                    String k = a[i++];
                    String[] s = a[i++].split(",");
                    float[] v = new float[s.length];
                    for (int n = 0; n < s.length; n++) v[n] = Float.valueOf(s[n].trim());
                    intent.putExtra(k, v);
                    break;
                }
                case "--efal": {
                    String k = a[i++];
                    ArrayList<Float> v = new ArrayList<>();
                    for (String s : a[i++].split(",")) v.add(Float.valueOf(s.trim()));
                    intent.putExtra(k, v);
                    break;
                }
                case "--esa": {  // 쉼표로 나눔 (\, 는 쉼표 글자)
                    String k = a[i++];
                    String[] v = a[i++].split("(?<!\\\\),");
                    for (int n = 0; n < v.length; n++) v[n] = v[n].replace("\\,", ",");
                    intent.putExtra(k, v);
                    break;
                }
                case "--esal": {
                    String k = a[i++];
                    ArrayList<String> v = new ArrayList<>();
                    for (String s : a[i++].split("(?<!\\\\),")) v.add(s.replace("\\,", ","));
                    intent.putExtra(k, v);
                    break;
                }
                case "--ez": {
                    String k = a[i++];
                    String v = a[i++].toLowerCase(Locale.ROOT);
                    boolean b;
                    if ("true".equals(v) || "t".equals(v)) b = true;
                    else if ("false".equals(v) || "f".equals(v)) b = false;
                    else b = Integer.decode(v) != 0;
                    intent.putExtra(k, b);
                    break;
                }
                case "-n": {
                    ComponentName cn = ComponentName.unflattenFromString(a[i++]);
                    if (cn == null) throw new IllegalArgumentException("Bad component name");
                    intent.setComponent(cn);
                    break;
                }
                case "-p": intent.setPackage(a[i++]); break;
                case "-f": intent.setFlags(Integer.decode(a[i++]).intValue()); break;
                case "--grant-read-uri-permission": intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION); break;
                case "--grant-write-uri-permission": intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION); break;
                case "--grant-persistable-uri-permission": intent.addFlags(Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION); break;
                case "--grant-prefix-uri-permission": intent.addFlags(Intent.FLAG_GRANT_PREFIX_URI_PERMISSION); break;
                case "--exclude-stopped-packages": intent.addFlags(Intent.FLAG_EXCLUDE_STOPPED_PACKAGES); break;
                case "--include-stopped-packages": intent.addFlags(Intent.FLAG_INCLUDE_STOPPED_PACKAGES); break;
                case "--debug-log-resolution": intent.addFlags(Intent.FLAG_DEBUG_LOG_RESOLUTION); break;
                case "--activity-brought-to-front": intent.addFlags(Intent.FLAG_ACTIVITY_BROUGHT_TO_FRONT); break;
                case "--activity-clear-top": intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP); break;
                case "--activity-clear-when-task-reset": intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_WHEN_TASK_RESET); break;
                case "--activity-exclude-from-recents": intent.addFlags(Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS); break;
                case "--activity-launched-from-history": intent.addFlags(Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY); break;
                case "--activity-multiple-task": intent.addFlags(Intent.FLAG_ACTIVITY_MULTIPLE_TASK); break;
                case "--activity-no-animation": intent.addFlags(Intent.FLAG_ACTIVITY_NO_ANIMATION); break;
                case "--activity-no-history": intent.addFlags(Intent.FLAG_ACTIVITY_NO_HISTORY); break;
                case "--activity-no-user-action": intent.addFlags(Intent.FLAG_ACTIVITY_NO_USER_ACTION); break;
                case "--activity-previous-is-top": intent.addFlags(Intent.FLAG_ACTIVITY_PREVIOUS_IS_TOP); break;
                case "--activity-reorder-to-front": intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT); break;
                case "--activity-reset-task-if-needed": intent.addFlags(Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED); break;
                case "--activity-single-top": intent.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP); break;
                case "--activity-clear-task": intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TASK); break;
                case "--activity-task-on-home": intent.addFlags(Intent.FLAG_ACTIVITY_TASK_ON_HOME); break;
                case "--activity-match-external": break;
                case "--receiver-registered-only":
                case "--receiver-replace-pending":
                case "--receiver-foreground":
                case "--receiver-no-abort":
                case "--receiver-include-background":
                    break;
                // am start 자체의 옵션 (인텐트와 무관): 값이 없는 것
                case "-D": case "-N": case "-W": case "-S": case "--streaming": case "--track-allocation":
                case "--task-overlay": case "--lock-task": case "--allow-background-activity-starts":
                    break;
                // 값이 하나 따르는 것
                case "-P": case "--start-profiler": case "--sampling": case "--attach-agent": case "--attach-agent-bind":
                case "-R": case "--user": case "--receiver-permission": case "--display": case "--windowingMode":
                case "--activityType": case "--task":
                    i++;
                    break;
                default:
                    if (o.startsWith("-")) throw new IllegalArgumentException("Unknown option: " + o);
                    // 옵션이 아닌 말: 주소 | 패키지/액티비티 | 패키지
                    if (o.indexOf(':') >= 0) {
                        data = Uri.parse(o);
                        if (intent.getAction() == null) intent.setAction(Intent.ACTION_VIEW);
                    } else if (o.indexOf('/') >= 0) {
                        intent.setAction(Intent.ACTION_MAIN);
                        intent.addCategory(Intent.CATEGORY_LAUNCHER);
                        intent.setComponent(ComponentName.unflattenFromString(o));
                    } else {
                        intent.setAction(Intent.ACTION_MAIN);
                        intent.addCategory(Intent.CATEGORY_LAUNCHER);
                        intent.setPackage(o);
                    }
                    break;
            }
        }
        if (data != null || type != null) intent.setDataAndType(data, type);
        return intent;
    }

    // 런처의 파일 제공자 주소가 인텐트의 글자 값(Extra)에 들어 있으면 대상 앱에 읽기 권한을 준다
    // (data 주소는 FLAG_GRANT_READ_URI_PERMISSION 으로 자동으로 넘어가지만 Extra 는 아님)
    private static void grantProviderUris(Intent intent) {
        String target = intent.getComponent() != null ? intent.getComponent().getPackageName() : intent.getPackage();
        if (target == null) {
            ResolveInfo ri = self.getPackageManager().resolveActivity(intent, 0);
            if (ri != null && ri.activityInfo != null) target = ri.activityInfo.packageName;
        }
        if (target == null) return;
        String prefix = "content://" + JoyFileProvider.AUTHORITY + "/";
        ArrayList<String> uris = new ArrayList<>();
        if (intent.getData() != null && intent.getData().toString().startsWith(prefix)) uris.add(intent.getData().toString());
        Bundle ex = intent.getExtras();
        if (ex != null)
            for (String k : ex.keySet()) {
                Object v = ex.get(k);
                if (v instanceof String && ((String) v).startsWith(prefix)) uris.add((String) v);
                else if (v instanceof Uri && v.toString().startsWith(prefix)) uris.add(v.toString());
            }
        for (String u : uris) self.grantUriPermission(target, Uri.parse(u), Intent.FLAG_GRANT_READ_URI_PERMISSION);
    }

    // null = 실행함, 아니면 오류 글
    public static String startAm(String[] args) {
        try {
            Intent intent = parseAm(args);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
            // 패키지만 적은 경우: 그 앱의 기본 실행 화면
            if (intent.getComponent() == null && intent.getPackage() != null && intent.getData() == null &&
                    (intent.getAction() == null || Intent.ACTION_MAIN.equals(intent.getAction()))) {
                Intent li = self.getPackageManager().getLaunchIntentForPackage(intent.getPackage());
                if (li == null) return "No launch activity: " + intent.getPackage();
                intent.setComponent(li.getComponent());
                if (intent.getAction() == null) intent.setAction(Intent.ACTION_MAIN);
            }
            grantProviderUris(intent);
            Log.i(TAG, "start " + intent + " extras=" + intent.getExtras());
            self.startActivity(intent);
            return null;
        } catch (Throwable t) {
            Log.w(TAG, "startAm: " + t);
            return t.getClass().getSimpleName() + (t.getMessage() != null ? ": " + t.getMessage() : "");
        }
    }

    public static String startApp(String pkg, String activity) {
        try {
            Intent i;
            if (activity != null && !activity.isEmpty()) {
                i = new Intent(Intent.ACTION_MAIN);
                i.addCategory(Intent.CATEGORY_LAUNCHER);
                i.setComponent(component(pkg, activity));
            } else {
                i = self.getPackageManager().getLaunchIntentForPackage(pkg);
                if (i == null) return "No launch activity: " + pkg;
            }
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED);
            self.startActivity(i);
            return null;
        } catch (Throwable t) {
            Log.w(TAG, "startApp: " + t);
            return t.getClass().getSimpleName() + (t.getMessage() != null ? ": " + t.getMessage() : "");
        }
    }

    // ------------------------------------------------------------ 기기 상태
    // 잔량(0~100) + 충전 중이면 1000. 모르면 -1
    public static int battery() {
        try {
            Intent b = self.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));
            if (b == null || !b.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true)) return -1;
            int level = b.getIntExtra(BatteryManager.EXTRA_LEVEL, -1), scale = b.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
            if (level < 0 || scale <= 0) return -1;
            int status = b.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
            boolean charging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL ||
                    b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) > 0;
            return Math.min(100, level * 100 / scale) + (charging ? 1000 : 0);
        } catch (Throwable t) {
            return -1;
        }
    }

    public static String systemLanguage() {
        return Locale.getDefault().getLanguage();
    }

    // ------------------------------------------------------------ 그림 해석 (SDL2_image 가 못 읽는 webp·avif·heif 등)
    // 돌려주는 값: [너비, 높이, 점들(0xAARRGGBB, 알파 미리 곱하지 않음)…]. 긴 변이 maxSide 를 넘으면 2의 배수로 줄여 읽음
    public static int[] decodeImage(String path, int maxSide) {
        try {
            android.graphics.BitmapFactory.Options o = new android.graphics.BitmapFactory.Options();
            o.inJustDecodeBounds = true;
            android.graphics.BitmapFactory.decodeFile(path, o);
            if (o.outWidth <= 0 || o.outHeight <= 0) return null;
            int sample = 1;
            while (maxSide > 0 && Math.max(o.outWidth, o.outHeight) / sample > maxSide) sample *= 2;
            o = new android.graphics.BitmapFactory.Options();
            o.inSampleSize = sample;
            o.inPreferredConfig = Bitmap.Config.ARGB_8888;
            Bitmap b = android.graphics.BitmapFactory.decodeFile(path, o);
            if (b == null) return null;
            if (b.getConfig() != Bitmap.Config.ARGB_8888) {  // 하드웨어 비트맵 등
                Bitmap c = b.copy(Bitmap.Config.ARGB_8888, false);
                b.recycle();
                if (c == null) return null;
                b = c;
            }
            int w = b.getWidth(), h = b.getHeight();
            int[] px = new int[w * h + 2];
            px[0] = w;
            px[1] = h;
            b.getPixels(px, 2, w, 0, 0, w, h);
            b.recycle();
            return px;
        } catch (Throwable t) {
            Log.w(TAG, "decodeImage " + path + ": " + t);
            return null;
        }
    }

    // ------------------------------------------------------------ 미리보기 영상 (JoyVideo)
    // 풀린 장면과 상태는 영상 스레드에서 아래 native 함수로 넘어간다
    public static void videoStart(String path, boolean sound, int token) {
        JoyVideo.start(path, sound, token);
    }

    public static void videoStop() {
        JoyVideo.stop();
    }

    static native void nativeVideoState(int token, int state, int audio);

    static native void nativeVideoYuv(int token, ByteBuffer y, ByteBuffer u, ByteBuffer v, int yStride, int uvStride, int uvPixelStride,
                                      int left, int top, int w, int h);

    static native void nativeVideoRgba(int token, ByteBuffer p, int stride, int pixelStride, int left, int top, int w, int h);

    // ------------------------------------------------------------ 네트워크 (테마 내려받기·업데이트 확인)
    private static HttpURLConnection open(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        c.setConnectTimeout(8000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "JoyLauncher-Updater");
        return c;
    }

    // [오류("" = 성공), 내용]
    public static String[] httpGet(String url) {
        HttpURLConnection c = null;
        try {
            c = open(url);
            c.setRequestProperty("Accept", "application/vnd.github+json");
            int code = c.getResponseCode();
            if (code != 200) return new String[] {"HTTP " + code, ""};
            InputStream in = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) {
                if (httpAbort) return new String[] {"cancel", ""};
                bo.write(buf, 0, n);
                if (bo.size() > 8 * 1024 * 1024) return new String[] {"too large", ""};
            }
            return new String[] {"", new String(bo.toByteArray(), StandardCharsets.UTF_8)};
        } catch (Throwable t) {
            return new String[] {t.getClass().getSimpleName() + (t.getMessage() != null ? ": " + t.getMessage() : ""), ""};
        } finally {
            if (c != null) c.disconnect();
        }
    }

    // null = 성공, "cancel" = 사용자가 멈춤, 그 밖 = 오류 글
    public static String httpDownload(String url, String path) {
        HttpURLConnection c = null;
        File f = new File(path);
        try {
            c = open(url);
            int code = c.getResponseCode();
            if (code != 200) return "HTTP " + code;
            long total = c.getContentLength();
            InputStream in = c.getInputStream();
            FileOutputStream fo = new FileOutputStream(f);
            byte[] buf = new byte[65536];
            long got = 0, last = 0;
            int n;
            boolean cancel = false;
            while ((n = in.read(buf)) > 0) {
                fo.write(buf, 0, n);
                got += n;
                if (httpAbort) { cancel = true; break; }
                if (got - last >= 131072) {
                    last = got;
                    if (!nativeProgress(got, total)) { cancel = true; break; }
                }
            }
            fo.close();
            if (cancel) {
                f.delete();
                return "cancel";
            }
            nativeProgress(got, total > 0 ? total : got);
            return null;
        } catch (Throwable t) {
            f.delete();
            return t.getClass().getSimpleName() + (t.getMessage() != null ? ": " + t.getMessage() : "");
        } finally {
            if (c != null) c.disconnect();
        }
    }

    public static void httpAbort(boolean on) {
        httpAbort = on;
    }

    public static void finishApp() {
        self.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                self.finish();
            }
        });
    }
}
