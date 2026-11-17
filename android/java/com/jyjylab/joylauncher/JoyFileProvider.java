// 게임 파일을 에뮬레이터에 넘길 때 쓰는 파일 제공자 (페가수스의 {file.uri}, ES-DE 의 %ROMPROVIDER%).
// content://com.jyjylab.joylauncher.files/root/<절대 경로> → 그 파일을 읽기 전용으로 연다.
// 밖에 공개하지 않고(exported=false) 실행하는 인텐트마다 그 주소에만 읽기 권한을 준다.
package com.jyjylab.joylauncher;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.webkit.MimeTypeMap;

import java.io.File;
import java.io.FileNotFoundException;

public class JoyFileProvider extends ContentProvider {
    public static final String AUTHORITY = "com.jyjylab.joylauncher.files";
    private static final String ROOT = "/root";

    public static Uri uriFor(File f) {
        return new Uri.Builder().scheme("content").authority(AUTHORITY).path(ROOT + f.getAbsolutePath()).build();
    }

    private static File fileFor(Uri uri) throws FileNotFoundException {
        String p = uri.getPath();
        if (p == null || !p.startsWith(ROOT + "/")) throw new FileNotFoundException("bad uri " + uri);
        return new File(p.substring(ROOT.length()));
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (mode != null && !"r".equals(mode)) throw new FileNotFoundException("read only");
        return ParcelFileDescriptor.open(fileFor(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        File f;
        try {
            f = fileFor(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        if (projection == null) projection = new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        Object[] row = new Object[projection.length];
        for (int i = 0; i < projection.length; i++) {
            if (OpenableColumns.DISPLAY_NAME.equals(projection[i])) row[i] = f.getName();
            else if (OpenableColumns.SIZE.equals(projection[i])) row[i] = f.length();
            else if ("_data".equals(projection[i])) row[i] = f.getAbsolutePath();  // 경로를 직접 여는 옛 에뮬레이터용
            else row[i] = null;
        }
        MatrixCursor c = new MatrixCursor(projection, 1);
        c.addRow(row);
        return c;
    }

    @Override
    public String getType(Uri uri) {
        String p = uri.getPath();
        int dot = p == null ? -1 : p.lastIndexOf('.');
        if (dot >= 0) {
            String t = MimeTypeMap.getSingleton().getMimeTypeFromExtension(p.substring(dot + 1).toLowerCase());
            if (t != null) return t;
        }
        return "application/octet-stream";
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
}
