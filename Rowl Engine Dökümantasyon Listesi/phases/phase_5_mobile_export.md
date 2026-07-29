# 🚀 PHASE 5 EXECUTION PLAN: MOBILE EXPORT (ANDROID & IOS)

> **Phase Objective:** Complete the "Write Once, Run Everywhere" pipeline by integrating Android (APK/AAB) and iOS (IPA) cross-compilation, touch input abstraction, responsive mobile UI, and automated packaging toolchains.

---

## 🏗️ 1. DIRECTORY STRUCTURE (MOBILE TOOLCHAINS)

```text
Node-Oyun-Motoru/
├── engine/
│   ├── include/rowl/
│   │   ├── platform/                 # OS-specific abstraction layers
│   │   │   ├── android_input.hpp     # Touch, accelerometer, haptics
│   │   │   ├── ios_input.hpp
│   │   │   └── mobile_window.hpp     # EGL/OpenGL ES surface creation
│   │   └── mobile/
│   │       └── aspect_guardian.hpp   # Letterbox/pillarbox math
│   └── src/
│       ├── platform/
│       │   ├── android_input.cpp
│       │   └── ios_input.cpp
│       └── mobile/
│           └── aspect_guardian.cpp
├── packaging/                        # Automated build scripts
│   ├── android/
│   │   ├── build.sh                  # Bash script to invoke Gradle/CMake
│   │   ├── AndroidManifest.xml       # Permissions, activities
│   │   └── gradle-wrapper.properties # Gradle version lock
│   └── ios/
│       ├── build.sh                  # Xcodebuild CMake invocation
│       └── Info.plist                # iOS app metadata
└── tools/
    └── export_game.py                # One-click export to .apk/.ipa/.exe
```

---

## 🤖 2. ANDROID TOOLCHAIN INTEGRATION

### A. Cross-Compilation Setup (SDL3 + CMake)

Android builds require the Android NDK (Native Development Kit) to compile the C++20 engine as a native shared library (`librowl_engine.so`) loaded by a lightweight Java/Kotlin activity.

```bash
# Set Android NDK path
export ANDROID_NDK_HOME=/path/to/android-ndk-r27

# Configure CMake for ARM64-v8a (primary target)
cmake -B build/android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-21 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/android --parallel 8
```

### B. Java/Kotlin Activity Bridge (`packaging/android/app/src/main/java/com/rowlengine/EngineActivity.kt`)

```kotlin
package com.rowlengine

import android.os.Bundle
import org.libsdl.app.SDLActivity

class EngineActivity : SDLActivity() {
    // SDL3 handles EGL context creation, touch events, and lifecycle hooks
    // The native C++ engine runs in-process via System.loadLibrary("rowl_engine")
}
```

### C. Android Manifest Permissions (`packaging/android/app/src/main/AndroidManifest.xml`)

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    <uses-permission android:name="android.permission.VIBRATE" />
    <application android:label="Rowl Engine Game" android:theme="@android:style/Theme.NoTitleBar">
        <activity android:name=".EngineActivity"
                  android:exported="true"
                  android:screenOrientation="sensorLandscape|sensorPortrait">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

---

## 🍎 3. IOS TOOLCHAIN INTEGRATION

iOS uses Xcode as the build host. CMake generates an Xcode project that compiles the C++ engine into a static library embedded into the iOS app bundle.

```bash
# Configure CMake for iOS (ARM64)
cmake -B build/ios \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DARCHS=arm64 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build/ios --parallel 8
```

### Info.plist Configuration (`packaging/ios/Info.plist`)

```xml
<key>UIInterfaceOrientation</key>
<array>
    <string>UIInterfaceOrientationLandscapeLeft</string>
    <string>UIInterfaceOrientationPortrait</string>
</array>
```

---

## 📱 4. TOUCH INPUT ABSTRACTION (SDL3 UNIFIED EVENTS)

On mobile, creators must not hand-code touch logic. SDL3 unifies inputs into cross-platform event types:

```cpp
// engine/src/platform/mobile_input.cpp
void Rowl::Platform::process_input_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_FINGER_DOWN:
                // Unified tap event
                handle_tap(event.button.x, event.button.y);
                break;
            
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_FINGER_UP:
                // Unified release event
                handle_release();
                break;
            
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_FINGER_MOTION:
                // Unified drag/swipe event
                handle_drag(event.motion.x, event.motion.y);
                break;
        }
    }
}
```

### Touch-Specific Optimizations:
- **Tap Target Minimum Size:** All interactive UI elements have a minimum touch target of **48x48 dp** (density-independent pixels) to prevent mis-taps.
- **Haptic Feedback:** Trigger short vibration (`SDL_HapticEffect`) when a choice button is tapped.

---

## 📐 5. ASPECT RATIO GUARDIAN (MOBILE LETTERBOXING)

The engine renders at virtual 1920x1080 resolution but adapts the physical viewport to mobile screens:

```cpp
// engine/src/mobile/aspect_guardian.cpp
struct LetterboxMetrics {
    int viewport_x, viewport_y;
    int viewport_width, viewport_height;
};

LetterboxMetrics Rowl::Mobile::calculate_viewport(int physical_width, int physical_height) {
    const float VIRTUAL_ASPECT = 1920.0f / 1080.0f;
    const float PHYSICAL_ASPECT = (float)physical_width / (float)physical_height;

    LetterboxMetrics result = {0, 0, physical_width, physical_height};

    if (PHYSICAL_ASPECT > VIRTUAL_ASPECT) {
        // Wide screen: Pillarbox (side bars)
        result.viewport_width = (int)(physical_height * VIRTUAL_ASPECT);
        result.viewport_x = (physical_width - result.viewport_width) / 2;
    } else {
        // Tall screen: Letterbox (top/bottom bars)
        result.viewport_height = (int)(physical_width / VIRTUAL_ASPECT);
        result.viewport_y = (physical_height - result.viewport_height) / 2;
    }

    return result;
}
```

---

## 📦 6. ONE-CLICK EXPORT TOOL (`tools/export_game.py`)

A Python automation script that packages the current project into a distributable format:

```python
import sys
import os
import subprocess
import shutil

def export_android():
    """Run CMake + Gradle to produce a signed .apk or .aab"""
    print("[Export] Building Android APK...")
    subprocess.run([
        "bash", "packaging/android/build.sh",
        "--asset-path", "data/game_data.rowlpkg",
        "--signing-key", "release.jks"
    ])
    print("[Export] Output: build/android/game-release.apk")

def export_ios():
    """Run Xcodebuild to produce a signed .ipa"""
    print("[Export] Building iOS IPA...")
    subprocess.run([
        "bash", "packaging/ios/build.sh",
        "--asset-path", "data/game_data.rowlpkg",
        "--signing-cert", "iPhone Distribution"
    ])
    print("[Export] Output: build/ios/game-release.ipa")

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "pc"
    if target == "android": export_android()
    elif target == "ios": export_ios()
    else:
        print(f"[Export] Unknown target: {target}")
```

---

## ✅ PHASE 5 ACCEPTANCE CRITERIA
- [ ] C++ engine compiles to a shared `.so` library for Android ARM64-v8a and loads inside the SDL3 Kotlin activity without crashes.
- [ ] C++ engine compiles to an iOS static library, links into Xcode project, and runs on a physical iPhone/iPad device.
- [ ] Tap targets on mobile dialogue choices are comfortably tappable (>48dp) with no visual glitches.
- [ ] Letterboxing/pillarboxing math adapts correctly to tall phones (9:16), wide tablets (16:9), and ultrawide monitors (21:9).
- [ ] `export_game.py` produces a valid APK for Android and IPA for iOS containing the packed `.rowlpkg` asset archive.
