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
BT_PROJECT_DIR = Path(r"C:\Users\PC-Tuyen\Desktop\esp32_a2dp_master")
RELEASE_BASE = Path(r"C:\Users\PC-Tuyen\Desktop\VBot_Client_Offline\SOCKET_CONNECTION")
PIO_EXE = r"C:\Users\PC-Tuyen\.platformio\penv\Scripts\pio.exe"

def run_cmd(cmd, cwd=PROJECT_DIR):
    print(f"\n>>> Running: {' '.join(str(c) for c in cmd)} (in {cwd})")
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        print(f"FAILED with code {res.returncode}")
        sys.exit(res.returncode)

def sync_sources():
    print("\n--- Syncing source code to VBot_Client_Offline ---")
    src_dest = RELEASE_BASE / "SOURCE" / "ESP32"
    src_dest.mkdir(parents=True, exist_ok=True)
    (src_dest / "src").mkdir(parents=True, exist_ok=True)
    (src_dest / "data").mkdir(parents=True, exist_ok=True)

    # 1. Sync src/ files
    for f in (PROJECT_DIR / "src").iterdir():
        if f.is_file():
            shutil.copy2(f, src_dest / "src" / f.name)
            shutil.copy2(f, src_dest / f.name)

    # 2. Sync data/ files
    for f in (PROJECT_DIR / "data").iterdir():
        if f.is_file():
            shutil.copy2(f, src_dest / "data" / f.name)
            shutil.copy2(f, src_dest / f.name)

    # 3. Sync config files
    shutil.copy2(PROJECT_DIR / "platformio.ini", src_dest / "platformio.ini")
    for p in PROJECT_DIR.glob("partitions*.csv"):
        shutil.copy2(p, src_dest / p.name)

    # 4. Sync Bluetooth ESP32 receiver sources (nếu có thư mục cục bộ)
    if (BT_PROJECT_DIR / "src").exists():
        bt_dest = RELEASE_BASE / "Bluetooth_ESP32_RECEIVER"
        bt_src_dest = bt_dest / "src"
        bt_src_dest.mkdir(parents=True, exist_ok=True)
        for f in (BT_PROJECT_DIR / "src").iterdir():
            if f.is_file():
                shutil.copy2(f, bt_src_dest / f.name)
        if (BT_PROJECT_DIR / "platformio.ini").exists():
            shutil.copy2(BT_PROJECT_DIR / "platformio.ini", bt_dest / "platformio.ini")
    print("Source files synchronized successfully!")

def build_esp_clients():
    for pio_env, rel_dir in ENV_MAP:
        print(f"\n=======================================================")
        print(f"BUILDING {pio_env} -> {rel_dir}")
        print(f"=======================================================")
        run_cmd([PIO_EXE, "run", "-e", pio_env], cwd=PROJECT_DIR)
        run_cmd([PIO_EXE, "run", "-e", pio_env, "-t", "buildfs"], cwd=PROJECT_DIR)

        build_dir = PROJECT_DIR / ".pio" / "build" / pio_env
        firmware_bin = build_dir / "firmware.bin"
        littlefs_bin = build_dir / "littlefs.bin"

        dest_bin_dir = RELEASE_BASE / rel_dir / "bin"
        dest_bin_dir.mkdir(parents=True, exist_ok=True)

        if firmware_bin.exists():
            shutil.copy2(firmware_bin, dest_bin_dir / "firmware.bin")
            if rel_dir == "ESP32S3_N16R8_WakeNet":
                shutil.copy2(firmware_bin, RELEASE_BASE / rel_dir / "firmware.bin")
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

def update_version_jsons():
    import json
    print("\n--- Updating Version.json for all release targets ---")
    desc = (
        "Phiên bản 24/11/2026: Khắc phục triệt để các lỗi gây reset mạch trên ESP32/ESP32-S3 WakeNet "
        "khi đánh thức, phát nhạc và phát Bluetooth dài hạn: sửa lỗi Use-After-Free xuyên lõi con trỏ MP3, "
        "chống đói Watchdog (TWDT) cho micTask (Core 0), dacTask HTTPS và task Bluetooth I2S Slave (Core 1), "
        "tăng stack cho dacTask (12KB) và bt_slave (8KB), đồng bộ giải phóng I2S_NUM_0 giữa VBot/Bluetooth/AirPlay, "
        "tắt xuất xung MCLK ra chân GPIO0 (BOOT), bảo vệ Mutex cho AEC/NS/AGC WakeNet, "
        "và nâng cấp chuyển hiệu ứng đèn LED liền mạch không nháy tắt giữa các trạng thái Đánh thức -> Xử lý -> Trả lời."
    )
    for _, rel_dir in ENV_MAP:
        v_file = RELEASE_BASE / rel_dir / "Version.json"
        data = {
            "build_date": "24/11/2026",
            "description": desc
        }
        with open(v_file, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f"Updated {v_file}")

def build_bluetooth_receiver():
    print(f"\n=======================================================")
    print(f"BUILDING Bluetooth_ESP32_RECEIVER (esp32_a2dp_master)")
    print(f"=======================================================")
    run_cmd([PIO_EXE, "run", "-e", "esp32dev"], cwd=BT_PROJECT_DIR)

    build_dir = BT_PROJECT_DIR / ".pio" / "build" / "esp32dev"
    dest_bin_dir = RELEASE_BASE / "Bluetooth_ESP32_RECEIVER" / "bin"
    dest_bin_dir.mkdir(parents=True, exist_ok=True)

    for bin_name in ["firmware.bin", "bootloader.bin", "partitions.bin"]:
        p = build_dir / bin_name
        if p.exists():
            shutil.copy2(p, dest_bin_dir / bin_name)
            print(f"Copied {bin_name} -> {dest_bin_dir}")
        else:
            print(f"WARNING: {p} not found!")

def main():
    sync_sources()
    update_version_jsons()
    build_esp_clients()
    build_bluetooth_receiver()
    print("\n=======================================================")
    print("SUCCESS: ALL 10 ESP TARGETS COMPILED & SYNCED TO RELEASE!")
    print("=======================================================")

if __name__ == "__main__":
    main()
