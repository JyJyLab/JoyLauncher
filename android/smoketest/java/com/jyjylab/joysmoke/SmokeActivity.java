package com.jyjylab.joysmoke;

import org.libsdl.app.SDLActivity;

// 고칠 것이 없다: android/CMakeLists.txt 가 libSDL2.so + libmain.so 를 만들고, 이것이
// SDLActivity.getLibraries() 의 기본값 {"SDL2", "main"} 그대로다. libc++_shared.so 는 두
// 라이브러리가 필요로 하므로 시스템이 알아서 함께 읽는다.
public class SmokeActivity extends SDLActivity {
}
