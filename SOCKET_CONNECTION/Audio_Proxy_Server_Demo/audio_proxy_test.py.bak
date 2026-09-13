"""
Máy chủ kiểm thử xử lý audio_proxy độc lập dành cho Client ESP32 VBot.

Cài đặt:
    $:> python3 -m pip install -r audio_proxy_test_requirements.txt

Chạy:
    $:> python3 audio_proxy_test_server.py --host 0.0.0.0 --port 5000

URL API luồng tùy chỉnh của giao diện WebUI ESP32:
    http://<PC-IP>:5000

Kiểm tra:
    http://<PC-IP>:5000/audio_proxy?url=<youtube-or-zingmp3-or-direct-audio-url>

Lưu ý:
    - Các URL trang YouTube/ZingMP3 được phân giải theo người dùng dựng server proxy
    - Các URL mp3/aac/m4a/ogg/wav/flac trực tiếp được chuyển tiếp qua proxy
"""

from __future__ import annotations

import argparse
import hashlib
import logging
import time
from typing import Any
from urllib.parse import urlparse

import requests
from flask import Flask, Response, jsonify, request, stream_with_context

try:
    import yt_dlp
except ImportError:
    yt_dlp = None


APP = Flask(__name__)
LOG = logging.getLogger("audio_proxy_test")

DEFAULT_TIMEOUT = (10, 30)
CHUNK_SIZE = 16 * 1024
RESOLVE_CACHE_TTL = 20 * 60
DIRECT_AUDIO_EXTENSIONS = (".mp3", ".aac", ".m4a", ".ogg", ".oga", ".wav", ".flac", ".opus")

PROXY_URL_MAP: dict[str, dict[str, Any]] = {}
RESOLVE_CACHE: dict[str, dict[str, Any]] = {}


def is_http_url(url: str) -> bool:
    parsed = urlparse(url)
    return parsed.scheme in ("http", "https") and bool(parsed.netloc)


def is_direct_audio_url(url: str) -> bool:
    parsed = urlparse(url)
    path = parsed.path.lower()
    return path.endswith(DIRECT_AUDIO_EXTENSIONS)


def cache_get(source_url: str) -> dict[str, Any] | None:
    item = RESOLVE_CACHE.get(source_url)
    if not item:
        return None
    if time.time() - float(item.get("created_at", 0)) > RESOLVE_CACHE_TTL:
        RESOLVE_CACHE.pop(source_url, None)
        return None
    return item


def cache_set(source_url: str, data: dict[str, Any]) -> dict[str, Any]:
    data["created_at"] = time.time()
    RESOLVE_CACHE[source_url] = data
    return data


def pick_best_format(info: dict[str, Any]) -> dict[str, Any]:
    requested = info.get("requested_downloads") or []
    if requested and requested[0].get("url"):
        return requested[0]

    formats = [fmt for fmt in (info.get("formats") or []) if fmt.get("url")]
    audio_only = [fmt for fmt in formats if fmt.get("vcodec") == "none"]
    candidates = audio_only or formats
    if not candidates and info.get("url"):
        return info
    if not candidates:
        raise RuntimeError("Không tìm thấy định dạng âm thanh nào có thể phát được.")

    def score(fmt: dict[str, Any]) -> tuple[int, float, int]:
        ext = str(fmt.get("ext") or "").lower()
        protocol = str(fmt.get("protocol") or "").lower()
        abr = float(fmt.get("abr") or fmt.get("tbr") or 0)
        ext_score = {"mp3": 5, "m4a": 4, "aac": 4, "webm": 2, "opus": 2}.get(ext, 1)
        protocol_score = 0 if "m3u8" in protocol or "dash" in protocol else 3
        return (protocol_score, ext_score, int(abr))

    return sorted(candidates, key=score, reverse=True)[0]


def resolve_with_ytdlp(source_url: str) -> dict[str, Any]:
    if yt_dlp is None:
        raise RuntimeError("Thiếu yt-dlp. Cài đặt: python3 -m pip install yt-dlp")

    cached = cache_get(source_url)
    if cached:
        return cached

    ydl_opts = {
        "quiet": True,
        "no_warnings": True,
        "skip_download": True,
        "format": "bestaudio[ext=mp3]/bestaudio[ext=m4a]/bestaudio/best",
        "noplaylist": True,
        "socket_timeout": 15,
        "extractor_args": {
            "youtube": {
                "player_client": ["android", "web"],
            }
        },
    }

    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(source_url, download=False)
        if info and info.get("_type") == "playlist":
            entries = [entry for entry in (info.get("entries") or []) if entry]
            if not entries:
                raise RuntimeError("Danh sách phát không có mục nào có thể phát được")
            info = entries[0]
        if not info:
            raise RuntimeError("Không thể phân giải URL âm thanh")

    best = pick_best_format(info)
    resolved_url = str(best.get("url") or "").strip()
    if not is_http_url(resolved_url):
        raise RuntimeError("URL âm thanh đã được giải quyết không hợp lệ")

    data = {
        "source_url": source_url,
        "stream_url": resolved_url,
        "title": info.get("title") or "",
        "id": info.get("id") or "",
        "ext": best.get("ext") or info.get("ext") or "",
        "headers": best.get("http_headers") or info.get("http_headers") or {},
    }
    return cache_set(source_url, data)


def resolve_audio_url(source_url: str) -> dict[str, Any]:
    source_url = str(source_url or "").strip()
    if not is_http_url(source_url):
        raise ValueError("URL âm thanh không hợp lệ")

    if is_direct_audio_url(source_url):
        return {
            "source_url": source_url,
            "stream_url": source_url,
            "title": "",
            "id": "",
            "ext": source_url.rsplit(".", 1)[-1].lower(),
            "headers": {},
        }

    return resolve_with_ytdlp(source_url)


def mimetype_for_url(url: str, fallback_ext: str = "") -> str:
    parsed_path = urlparse(url).path.lower()
    ext = (fallback_ext or "").lower()
    if parsed_path.endswith(".mp3") or ext == "mp3":
        return "audio/mpeg"
    if parsed_path.endswith(".m4a") or ext in ("m4a", "aac"):
        return "audio/mp4"
    if parsed_path.endswith(".ogg") or parsed_path.endswith(".oga") or ext in ("ogg", "oga", "opus"):
        return "audio/ogg"
    if parsed_path.endswith(".wav") or ext == "wav":
        return "audio/wav"
    if parsed_path.endswith(".flac") or ext == "flac":
        return "audio/flac"
    return "audio/mpeg"


def source_url_from_request() -> str:
    proxy_id = str(request.args.get("id") or "").strip()
    if proxy_id:
        item = PROXY_URL_MAP.get(proxy_id) or {}
        return str(item.get("url") or "").strip()
    return str(request.args.get("url") or "").strip()


@APP.get("/health")
def health() -> Response:
    return jsonify(
        {
            "success": True,
            "message": "audio_proxy Máy chủ thử nghiệm đang chạy",
            "yt_dlp": yt_dlp is not None,
            "cache_items": len(RESOLVE_CACHE),
        }
    )


@APP.get("/resolve")
def resolve_endpoint() -> Response:
    source_url = source_url_from_request()
    try:
        data = resolve_audio_url(source_url)
        return jsonify(
            {
                "success": True,
                "source_url": data["source_url"],
                "stream_url": data["stream_url"],
                "title": data.get("title") or "",
                "ext": data.get("ext") or "",
            }
        )
    except Exception as exc:
        LOG.exception("Giải quyết thất bại")
        return jsonify({"success": False, "message": str(exc)}), 400


@APP.get("/register")
def register_endpoint() -> Response:
    source_url = str(request.args.get("url") or "").strip()
    if not is_http_url(source_url):
        return jsonify({"success": False, "message": "Invalid URL"}), 400
    proxy_id = hashlib.sha1(source_url.encode("utf-8")).hexdigest()
    PROXY_URL_MAP[proxy_id] = {"url": source_url, "created_at": time.time()}
    return jsonify(
        {
            "success": True,
            "id": proxy_id,
            "proxy_url": f"{request.host_url.rstrip('/')}/audio_proxy?id={proxy_id}",
        }
    )


@APP.get("/audio_proxy")
def audio_proxy() -> Response:
    source_url = source_url_from_request()
    try:
        resolved = resolve_audio_url(source_url)
    except Exception as exc:
        LOG.exception("Không thể phân giải URL")
        return jsonify({"success": False, "message": str(exc)}), 400

    stream_url = resolved["stream_url"]
    upstream_headers = {
        "User-Agent": "Mozilla/5.0",
        "Accept": "*/*",
        "Connection": "close",
    }
    upstream_headers.update(resolved.get("headers") or {})
    if request.headers.get("Range"):
        upstream_headers["Range"] = request.headers["Range"]

    def generate():
        upstream = None
        try:
            LOG.info("Âm thanh trực tuyến: %s", source_url)
            upstream = requests.get(
                stream_url,
                headers=upstream_headers,
                stream=True,
                timeout=DEFAULT_TIMEOUT,
                allow_redirects=True,
            )
            upstream.raise_for_status()
            for chunk in upstream.iter_content(chunk_size=CHUNK_SIZE):
                if chunk:
                    yield chunk
        except Exception as exc:
            LOG.error("Lỗi luồng: %s", exc)
        finally:
            if upstream is not None:
                upstream.close()

    headers = {
        "Cache-Control": "no-store",
        "Connection": "close",
        "X-Accel-Buffering": "no",
    }
    return Response(
        stream_with_context(generate()),
        headers=headers,
        mimetype=mimetype_for_url(stream_url, str(resolved.get("ext") or "")),
        direct_passthrough=True,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Máy chủ thử nghiệm audio_proxy VBot")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--debug", action="store_true")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO, format="[%(levelname)s] %(message)s")
    APP.run(host=args.host, port=args.port, debug=args.debug, threaded=True)
