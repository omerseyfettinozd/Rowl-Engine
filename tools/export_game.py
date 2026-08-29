#!/usr/bin/env python3
import sys
import os
import subprocess
import shutil

def export_pc():
    print("[Export Tool] Packaging project for PC Desktop (Windows/Linux/macOS)...")
    out_dir = "build/export_pc"
    os.makedirs(out_dir, exist_ok=True)
    if os.path.exists("build/bin/rowl_engine"):
        shutil.copy("build/bin/rowl_engine", os.path.join(out_dir, "rowl_engine"))
    if os.path.exists("build/lib/libRowlEngineCore.so"):
        shutil.copy("build/lib/libRowlEngineCore.so", os.path.join(out_dir, "libRowlEngineCore.so"))
    if os.path.exists("Assets"):
        assets_out = os.path.join(out_dir, "Assets")
        if os.path.exists(assets_out):
            shutil.rmtree(assets_out)
        shutil.copytree("Assets", assets_out)
    print(f"[Export Tool] PC Export successful! Package folder: '{out_dir}'")

def export_android():
    print("[Export Tool] Packaging project for Android Mobile (APK/AAB)...")
    subprocess.run(["bash", "packaging/android/build.sh", "--asset-path", "data/game_data.rowlpkg"])
    print("[Export Tool] Android Export successful! Package: 'build/outputs/apk/rowl-release.apk'")

def export_ios():
    print("[Export Tool] Packaging project for iOS Mobile (IPA)...")
    subprocess.run(["bash", "packaging/ios/build.sh", "--asset-path", "data/game_data.rowlpkg"])
    print("[Export Tool] iOS Export successful! Package: 'build/RowlGame.ipa'")

if __name__ == '__main__':
    target = sys.argv[1] if len(sys.argv) > 1 else "pc"
    print(f"==================================================")
    print(f"Rowl Engine One-Click Export Tool — Target: [{target.upper()}]")
    print(f"==================================================")

    if target == "android":
        export_android()
    elif target == "ios":
        export_ios()
    elif target == "pc":
        export_pc()
    else:
        print(f"[Export Tool] Unknown target '{target}'. Choose: pc, android, ios")
