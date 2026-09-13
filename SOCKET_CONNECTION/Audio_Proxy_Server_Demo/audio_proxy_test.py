"""
Standalone audio_proxy test server for ESP32 VBot client.

Install:
  python -m pip install -r audio_proxy_test_requirements.txt

Run:
  python audio_proxy_test_server.py --host 0.0.0.0 --port 5000

ESP32 WebUI custom stream API URL:
  http://<PC-IP>:5000

Test:
  http://<PC-IP>:5000/audio_proxy?url=<youtube-or-zingmp3-or-direct-audio-url>

Notes:
  - YouTube/ZingMP3 page URLs are resolved through yt-dlp.
  - Direct mp3/aac/m4a/ogg/wav/flac URLs are proxied directly.
  - This file is for local testing only.
"""

from __future__ import annotations

import argparse
from collections import OrderedDict
import hashlib
import logging
import threading
import time
from typing import Any
from urllib.parse import urlparse

import requests
from flask import Flask, Response, jsonify, request, stream_with_context

try:
    import yt_dlp
except ImportError:  # pragma: no cover
    yt_dlp = None


APP = Flask(__name__)
LOG = logging.getLogger("audio_proxy_test")

DEFAULT_TIMEOUT = (10, 30)
CHUNK_SIZE = 16 * 1024
RESOLVE_CACHE_TTL = 20 * 60
PROXY_URL_TTL = 24 * 60 * 60
MAX_CACHE_ITEMS = 512
MAX_URL_LENGTH = 8192
MAX_CONCURRENT_STREAMS = 32
MAX_STREAMS_PER_IP = 4
MAX_CONCURRENT_RESOLVES = 4
DIRECT_AUDIO_EXTENSIONS = (".mp3", ".aac", ".m4a", ".ogg", ".oga", ".wav", ".flac", ".opus")

PROXY_URL_MAP: OrderedDict[str, dict[str, Any]] = OrderedDict()
RESOLVE_CACHE: OrderedDict[str, dict[str, Any]] = OrderedDict()
CACHE_LOCK = threading.RLock()
STREAM_LOCK = threading.Lock()
STREAM_COUNTS: dict[str, int] = {}
ACTIVE_STREAMS = 0
RESOLVE_SEMAPHORE = threading.BoundedSemaphore(MAX_CONCURRENT_RESOLVES)
# Striped locks prevent duplicate yt-dlp work for the same URL without retaining
# one lock forever for every URL ever requested.
RESOLVE_LOCKS = tuple(threading.Lock() for _ in range(32))


def is_http_url(url: str) -> bool:
    if not url or len(url) > MAX_URL_LENGTH:
        return False
    parsed = urlparse(url)
    return parsed.scheme in ("http", "https") and bool(parsed.netloc)


def is_direct_audio_url(url: str) -> bool:
    parsed = urlparse(url)
    path = parsed.path.lower()
    return path.endswith(DIRECT_AUDIO_EXTENSIONS)


def cache_get(source_url: str) -> dict[str, Any] | None:
    with CACHE_LOCK:
        item = RESOLVE_CACHE.get(source_url)
        if not item:
            return None
        if time.time() - float(item.get("created_at", 0)) > RESOLVE_CACHE_TTL:
            RESOLVE_CACHE.pop(source_url, None)
            return None
        RESOLVE_CACHE.move_to_end(source_url)
        return dict(item)


def cache_set(source_url: str, data: dict[str, Any]) -> dict[str, Any]:
    cached = dict(data)
    cached["created_at"] = time.time()
    with CACHE_LOCK:
        RESOLVE_CACHE[source_url] = cached
        RESOLVE_CACHE.move_to_end(source_url)
        while len(RESOLVE_CACHE) > MAX_CACHE_ITEMS:
            RESOLVE_CACHE.popitem(last=False)
    return dict(cached)


def proxy_map_get(proxy_id: str) -> str:
    now = time.time()
    with CACHE_LOCK:
        item = PROXY_URL_MAP.get(proxy_id)
        if not item:
            return ""
        if now - float(item.get("created_at", 0)) > PROXY_URL_TTL:
            PROXY_URL_MAP.pop(proxy_id, None)
            return ""
        PROXY_URL_MAP.move_to_end(proxy_id)
        return str(item.get("url") or "").strip()


def proxy_map_set(proxy_id: str, source_url: str) -> None:
    with CACHE_LOCK:
        PROXY_URL_MAP[proxy_id] = {"url": source_url, "created_at": time.time()}
        PROXY_URL_MAP.move_to_end(proxy_id)
        while len(PROXY_URL_MAP) > MAX_CACHE_ITEMS:
            PROXY_URL_MAP.popitem(last=False)


def acquire_stream(client_ip: str) -> bool:
    global ACTIVE_STREAMS
    with STREAM_LOCK:
        client_count = STREAM_COUNTS.get(client_ip, 0)
        if ACTIVE_STREAMS >= MAX_CONCURRENT_STREAMS or client_count >= MAX_STREAMS_PER_IP:
            return False
        ACTIVE_STREAMS += 1
        STREAM_COUNTS[client_ip] = client_count + 1
        return True


def release_stream(client_ip: str) -> None:
    global ACTIVE_STREAMS
    with STREAM_LOCK:
        count = STREAM_COUNTS.get(client_ip, 0)
        if count <= 1:
            STREAM_COUNTS.pop(client_ip, None)
        else:
            STREAM_COUNTS[client_ip] = count - 1
        ACTIVE_STREAMS = max(0, ACTIVE_STREAMS - 1)


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
        raise RuntimeError("No playable audio format found")

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
        raise RuntimeError("Missing yt-dlp. Install: python -m pip install yt-dlp")

    cached = cache_get(source_url)
    if cached:
        return cached

    resolve_lock = RESOLVE_LOCKS[int(hashlib.sha1(source_url.encode("utf-8")).hexdigest(), 16) % len(RESOLVE_LOCKS)]
    with resolve_lock:
        cached = cache_get(source_url)
        if cached:
            return cached
        return _resolve_with_ytdlp_uncached(source_url)


def _resolve_with_ytdlp_uncached(source_url: str) -> dict[str, Any]:

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

    if not RESOLVE_SEMAPHORE.acquire(timeout=DEFAULT_TIMEOUT[1]):
        raise TimeoutError("Server is busy resolving media URLs")
    try:
        with yt_dlp.YoutubeDL(ydl_opts) as ydl:
            info = ydl.extract_info(source_url, download=False)
    finally:
        RESOLVE_SEMAPHORE.release()
        if info and info.get("_type") == "playlist":
            entries = [entry for entry in (info.get("entries") or []) if entry]
            if not entries:
                raise RuntimeError("Playlist has no playable entries")
            info = entries[0]
        if not info:
            raise RuntimeError("Cannot resolve audio URL")

    best = pick_best_format(info)
    resolved_url = str(best.get("url") or "").strip()
    if not is_http_url(resolved_url):
        raise RuntimeError("Resolved audio URL is invalid")

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
        raise ValueError("Invalid audio URL")

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
        return proxy_map_get(proxy_id)
    return str(request.args.get("url") or "").strip()


@APP.get("/health")
def health() -> Response:
    with CACHE_LOCK:
        cache_items = len(RESOLVE_CACHE)
        registered_urls = len(PROXY_URL_MAP)
    with STREAM_LOCK:
        active_streams = ACTIVE_STREAMS
    return jsonify(
        {
            "success": True,
            "message": "audio_proxy test server is running",
            "yt_dlp": yt_dlp is not None,
            "cache_items": cache_items,
            "registered_urls": registered_urls,
            "active_streams": active_streams,
            "max_concurrent_streams": MAX_CONCURRENT_STREAMS,
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
        LOG.exception("Resolve failed")
        return jsonify({"success": False, "message": str(exc)}), 400


@APP.get("/register")
def register_endpoint() -> Response:
    source_url = str(request.args.get("url") or "").strip()
    if not is_http_url(source_url):
        return jsonify({"success": False, "message": "Invalid URL"}), 400
    proxy_id = hashlib.sha1(source_url.encode("utf-8")).hexdigest()
    proxy_map_set(proxy_id, source_url)
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
    client_ip = str(request.remote_addr or "unknown")
    if not acquire_stream(client_ip):
        return jsonify({"success": False, "message": "Too many concurrent audio streams"}), 429

    try:
        resolved = resolve_audio_url(source_url)
    except Exception as exc:
        release_stream(client_ip)
        LOG.exception("Cannot resolve URL")
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

    try:
        upstream = requests.get(
            stream_url,
            headers=upstream_headers,
            stream=True,
            timeout=DEFAULT_TIMEOUT,
            allow_redirects=True,
        )
        upstream.raise_for_status()
    except requests.Timeout as exc:
        release_stream(client_ip)
        LOG.warning("Upstream timeout for %s: %s", source_url, exc)
        return jsonify({"success": False, "message": "Upstream audio timeout"}), 504
    except requests.RequestException as exc:
        release_stream(client_ip)
        LOG.warning("Upstream request failed for %s: %s", source_url, exc)
        return jsonify({"success": False, "message": "Upstream audio request failed"}), 502

    released = False
    release_lock = threading.Lock()

    def close_stream() -> None:
        nonlocal released
        with release_lock:
            if released:
                return
            released = True
        try:
            upstream.close()
        finally:
            release_stream(client_ip)

    def generate():
        try:
            LOG.info("Streaming audio: %s", source_url)
            for chunk in upstream.iter_content(chunk_size=CHUNK_SIZE):
                if chunk:
                    yield chunk
        except GeneratorExit:
            return
        except Exception as exc:
            LOG.error("Stream error: %s", exc)
        finally:
            close_stream()

    headers = {
        "Cache-Control": "no-store",
        "X-Accel-Buffering": "no",
    }

    for name in ("Content-Range", "Content-Length", "Accept-Ranges", "ETag", "Last-Modified"):
        if upstream.headers.get(name):
            headers[name] = upstream.headers[name]
    response = Response(
        stream_with_context(generate()),
        status=upstream.status_code,
        headers=headers,
        content_type=upstream.headers.get("Content-Type")
        or mimetype_for_url(stream_url, str(resolved.get("ext") or "")),
        direct_passthrough=True,
    )
    response.call_on_close(close_stream)
    return response


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ESP32 VBot audio_proxy test server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--dev-server", action="store_true", help="Use Flask development server")
    parser.add_argument("--threads", type=int, default=16, help="Waitress worker threads")
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO, format="[%(levelname)s] %(message)s")
    if args.dev_server or args.debug:
        APP.run(host=args.host, port=args.port, debug=args.debug, threaded=True)
    else:
        try:
            from waitress import serve
        except ImportError as exc:
            raise SystemExit("Missing waitress. Install: python -m pip install -r audio_proxy_test_requirements.txt") from exc
        serve(APP, host=args.host, port=args.port, threads=max(4, args.threads), channel_timeout=45)
