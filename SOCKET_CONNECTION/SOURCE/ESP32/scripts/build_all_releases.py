import subprocess
import shutil
import sys
from pathlib import Path

ENV_MAP = [
    ("esp32", "ESP32"),
    ("esp32-wakenet", "ESP32_WakeNet"),
    ("esp32s3", "ESP32S3"),
    ("esp32s3-n8r2", "ESP32S3_N8R2"),
    ("esp32s3-n8r2-wakenet", "ESP32S3_N8R2_WakeNet"),
    ("esp32s3-n8r8", "ESP32S3_N8R8"),
    ("esp32s3-n8r8-wakenet", "ESP32S3_N8R8_WakeNet"),
    ("esp32s3-n16r8", "ESP32S3_N16R8"),
    ("esp32s3-n16r8-wakenet", "ESP32S3_N16R8_WakeNet"),
]

PROJECT_DIR = Path(r"C:\Users\PC-Tuyen\Desktop\esp32_vbot_client")
RELEASE_BASE = Path(r"C:\Users\PC-Tuyen\Desktop\VBot_Client_Offline\SOCKET_CONNECTION")
PIO_EXE = r"C:\Users\PC-Tuyen\.platformio\penv\Scripts\pio.exe"

def run_cmd(cmd, cwd=PROJECT_DIR):
    print(f"\n>>> Running: {' '.join(cmd)}")
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        print(f"FAILED with code {res.returncode}")
        sys.exit(res.returncode)

def main():
    # 1. Sync source files
    src_dest = RELEASE_BASE / "SOURCE" / "ESP32"
    src_dest.mkdir(parents=True, exist_ok=True)
    shutil.copy2(PROJECT_DIR / "src" / "main.cpp", src_dest / "main.cpp")
    shutil.copy2(PROJECT_DIR / "src" / "vbot_wakenet.cpp", src_dest / "vbot_wakenet.cpp")
    shutil.copy2(PROJECT_DIR / "src" / "vbot_wakenet.h", src_dest / "vbot_wakenet.h")
    shutil.copy2(PROJECT_DIR / "src" / "vbot_dlna.cpp", src_dest / "vbot_dlna.cpp")
    shutil.copy2(PROJECT_DIR / "src" / "vbot_dlna.h", src_dest / "vbot_dlna.h")
    shutil.copy2(PROJECT_DIR / "data" / "app.js", src_dest / "app.js")
    shutil.copy2(PROJECT_DIR / "data" / "index.html", src_dest / "index.html")
    print("Source files copied to release SOURCE/ESP32/")

    # 2. Build each environment
    for pio_env, rel_dir in ENV_MAP:
        print(f"\n=======================================================")
        print(f"BUILDING {pio_env} -> {rel_dir}")
        print(f"=======================================================")
        # Build firmware and filesystem
        run_cmd([PIO_EXE, "run", "-e", pio_env])
        run_cmd([PIO_EXE, "run", "-e", pio_env, "-t", "buildfs"])

        build_dir = PROJECT_DIR / ".pio" / "build" / pio_env
        firmware_bin = build_dir / "firmware.bin"
        littlefs_bin = build_dir / "littlefs.bin"

        dest_bin_dir = RELEASE_BASE / rel_dir / "bin"
        dest_bin_dir.mkdir(parents=True, exist_ok=True)

        if firmware_bin.exists():
            shutil.copy2(firmware_bin, dest_bin_dir / "firmware.bin")
            print(f"Copied firmware.bin -> {dest_bin_dir}")
        else:
            print(f"ERROR: {firmware_bin} does not exist!")
            sys.exit(1)

        if littlefs_bin.exists():
            shutil.copy2(littlefs_bin, dest_bin_dir / "littlefs.bin")
            print(f"Copied littlefs.bin -> {dest_bin_dir}")
        else:
            print(f"ERROR: {littlefs_bin} does not exist!")
            sys.exit(1)

    print("\nALL 9 ESP TARGETS BUILT AND SYNCED SUCCESSFULLY!")

if __name__ == "__main__":
    main()
