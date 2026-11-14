import re
from pathlib import Path
import gzip
import shutil

Import("env")

PROJECT_DIR = Path(env["PROJECT_DIR"])
SOURCE_DATA_DIR = PROJECT_DIR / "data"
BUILD_DATA_DIR = PROJECT_DIR / ".pio" / "littlefs_data"
GZIP_EXTENSIONS = {".html", ".css", ".js"}
EXCLUDED_PREFIXES = {("vendor", "bootstrap")}
EXCLUDED_FILES = {Path("app.css"), Path("app.js")}


def gzip_bytes(source: Path) -> bytes:
    return gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)


def bundled_index_bytes(source: Path) -> bytes:
    html = source.read_text(encoding="utf-8")
    css = (SOURCE_DATA_DIR / "app.css").read_text(encoding="utf-8")
    javascript = (SOURCE_DATA_DIR / "app.js").read_text(encoding="utf-8")
    html = re.sub(
        r'<link\s+rel="stylesheet"\s+href="/webui/app\.css[^"]*">',
        lambda _: f"<style>\n{css}\n</style>",
        html,
    )
    html = re.sub(
        r'<script\s+src="/webui/app\.js[^"]*"></script>',
        lambda _: f"<script>\n{javascript}\n</script>",
        html,
    )
    if "/webui/app.css" in html or "/webui/app.js" in html:
        raise RuntimeError("Không thể nhúng app.css/app.js vào index.html")
    return gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)


def rebuild_littlefs_data() -> None:
    if BUILD_DATA_DIR.exists():
        shutil.rmtree(BUILD_DATA_DIR)
    BUILD_DATA_DIR.mkdir(parents=True, exist_ok=True)
    print(f"LittleFS staging: {BUILD_DATA_DIR}")

    for source in SOURCE_DATA_DIR.rglob("*"):
        if not source.is_file() or source.suffix.lower() == ".gz":
            continue
        relative = source.relative_to(SOURCE_DATA_DIR)
        if any(relative.parts[:len(prefix)] == prefix for prefix in EXCLUDED_PREFIXES):
            print(f"SKIP {relative} (loaded from CDN)")
            continue
        if relative in EXCLUDED_FILES:
            print(f"SKIP {relative} (bundled into index.html.gz)")
            continue
        target = BUILD_DATA_DIR / relative
        target.parent.mkdir(parents=True, exist_ok=True)

        if source.suffix.lower() in GZIP_EXTENSIONS:
            gz_target = target.with_name(target.name + ".gz")
            gz_target.write_bytes(bundled_index_bytes(source) if relative == Path("index.html") else gzip_bytes(source))
            print(f"GZIP {relative} -> {gz_target.relative_to(BUILD_DATA_DIR)}")
        else:
            shutil.copy2(source, target)


if SOURCE_DATA_DIR.exists():
    rebuild_littlefs_data()
