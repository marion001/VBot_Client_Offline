# VBot Streaming Socket Protocol

Tai lieu nay mo ta co che client ket noi toi VBot Streaming Server khi `connection_protocol` la `socket`.

File server lien quan: `Streaming.py`

## 1. Cau hinh server

Trong `Config.json`, `working_mode` khong nam trong muc `socket` nua. Moi client se tu khai bao `working_mode` khi ket noi thanh cong.

```json
{
  "api": {
    "streaming_server": {
      "active": true,
      "connection_protocol": "socket",
      "protocol": {
        "socket": {
          "port": 5003,
          "maximum_recording_time": 5,
          "maximum_client_connected": 3,
          "source_stt": "stt_ggcloud",
          "select_wakeup": "snowboy",
          "client_conversation_mode": true,
          "music_playback_on_client": true
        }
      }
    }
  }
}
```

Client ket noi toi:

```text
ws://<IP_VBOT>:<port>
```

Vi du:

```text
ws://192.168.1.10:5003
```

## 2. Kieu du lieu qua WebSocket

Server va client trao doi 2 kieu message:

| Kieu | Huong | Y nghia |
| --- | --- | --- |
| Text JSON | Client -> Server | Khai bao `session_id`, `working_mode` |
| Text command | Client -> Server | Lenh `start_recording`, `Skip_WakeUP`, `stop` |
| Binary | Client -> Server | PCM raw microphone gui len STT/wakeup |
| Text JSON | Server -> Client | Trang thai xu ly, transcript, metadata audio |
| Binary | Server -> Client | PCM raw audio de client phat realtime |

## 3. Ket noi va khai bao client

Khi WebSocket vua ket noi, server tra:

```json
{
  "vbot_client_id": "('192.168.1.20', 54321)",
  "message": "Da ket noi VBot Socket Server"
}
```

Sau do client nen gui JSON cau hinh:

```json
{
  "session_id": "client_phong_khach",
  "working_mode": "main_processing"
}
```

Server tra:

```json
{
  "vbot_client_id": "client_phong_khach",
  "working_mode": "main_processing",
  "message": "Da nhan cau hinh client"
}
```

`working_mode` ho tro:

| Mode | Y nghia |
| --- | --- |
| `main_processing` | Xu ly day du qua VBot. Neu assistant tra PCM raw, server stream `pcm_raw_audio` ve client. |
| `chatbot` | Gui transcript vao chatbot va tra text/audio ket qua. |
| `stt_to_tts` | Ten cu, hien tai chi tra text STT ve client, khong tao TTS. Alias `stt_to_text` cung duoc chap nhan. |

De tuong thich client cu, neu client gui text thuong khong phai lenh dac biet va khong phai JSON, server xem text do la `session_id`.

## 4. Lenh text client gui

### Bat dau ghi am khong can wake word

Client gui mot trong cac text:

```text
Skip_WakeUP
start
start_recording
```

Neu server ranh, server tra:

```json
{
  "processing_process": "wake_word_detected",
  "wake_word_detected": true,
  "status_audio": "http://192.168.1.10/assets/sound/default/ding.mp3",
  "message": "Da duoc danh thuc!"
}
```

Neu server dang xu ly client khac:

```json
{
  "processing_process": "waiting_to_wake_up",
  "waiting_to_wake_up": true,
  "status_audio": "http://192.168.1.10/assets/sound/default/dong.mp3",
  "message": "Yeu cau bi tu choi: Co client khac dang xu ly"
}
```

### Dung ghi am

Client gui:

```text
stop
```

Server ket thuc audio queue hien tai va dua audio da nhan sang STT.

`status_audio` la rieng cho ket noi socket. UDP khong them truong nay.

## 5. Binary audio client gui len

Client gui microphone bang WebSocket binary:

```text
PCM raw
Signed 16-bit little-endian
Mono
Khuyen nghi 16000 Hz
Frame wakeup: 512 samples = 1024 bytes
```

Khi chua recording, server chi xu ly binary frame dung `1024` bytes de chay wake word.

Khi da recording, server dua cac binary chunk vao STT. Chunk nen deu va nho de giam tre.

## 6. Luong hoat dong bo qua wake word

1. Client ket noi WebSocket.
2. Server tra message ket noi.
3. Client gui JSON cau hinh gom `session_id`, `working_mode`.
4. Client gui `start_recording`.
5. Server tra `wake_word_detected`.
6. Server tra `recording`.
7. Client gui binary PCM microphone.
8. Client gui `stop`, hoac server tu dung sau `maximum_recording_time`.
9. Server STT va tra `data_processing`.
10. Server tra ket qua theo `working_mode`.
11. Neu co PCM raw audio phan hoi, server gui realtime tung cap `pcm_raw_audio` metadata + binary PCM.

## 7. JSON server tra ve

### Dang ghi am

```json
{
  "processing_process": "recording",
  "recording_streaming": true,
  "message": "Dang thu am..."
}
```

### Khong co giong noi

```json
{
  "processing_process": "waiting_to_wake_up",
  "waiting_to_wake_up": true,
  "status_audio": "http://192.168.1.10/assets/sound/default/dong.mp3",
  "message": "Khong co giong noi duoc truyen vao, dang cho duoc danh thuc"
}
```

### Da co transcript, bat dau xu ly

```json
{
  "processing_process": "data_processing",
  "transcript_normed": "bat den phong khach",
  "message": "Dang xu ly du lieu"
}
```

### Ket qua `main_processing`, tiep tuc hoi thoai

```json
{
  "processing_process": "continue_wake_up",
  "tts_audio": "http://192.168.1.10/assets/sound/TTS_Audio/file.mp3",
  "assistant_text": "Da bat den phong khach",
  "response_text": "Da bat den phong khach",
  "message": "Da xu ly xong du lieu, tiep tuc duoc danh thuc"
}
```

### Ket qua `main_processing`, quay ve cho wake word

```json
{
  "processing_process": "waiting_to_wake_up",
  "status_audio": "http://192.168.1.10/assets/sound/default/dong.mp3",
  "tts_audio": "http://192.168.1.10/assets/sound/TTS_Audio/file.mp3",
  "assistant_text": "Da bat den phong khach",
  "response_text": "Da bat den phong khach",
  "message": "Da xu ly xong du lieu, dang cho duoc danh thuc"
}
```

### Ket qua `chatbot`

```json
{
  "processing_process": "chatbot_response",
  "tts_audio": "http://192.168.1.10/assets/sound/TTS_Audio/file.mp3",
  "assistant_text": "Noi dung chatbot tra ve",
  "response_text": "Noi dung chatbot tra ve",
  "message": "Da xu ly xong du lieu chatbot"
}
```

### Ket qua `stt_to_tts`

Mode nay chi tra text STT, khong tao TTS:

```json
{
  "processing_process": "stt_to_tts_response",
  "tts_audio": null,
  "assistant_text": "noi dung stt",
  "response_text": "noi dung stt",
  "message": "Da chuyen doi STT sang text"
}
```

### Loi

```json
{
  "processing_process": "error",
  "message": "Loi xu ly du lieu tren server"
}
```

## 8. Realtime `pcm_raw_audio`

Khi `working_mode` la `main_processing`, neu assistant tra ve audio PCM raw, server se stream realtime ve client. Moi chunk gom 2 message lien tiep:

1. Text JSON metadata:

```json
{
  "processing_process": "pcm_raw_audio",
  "audio_format": "pcm_s16le",
  "sample_rate": 16000,
  "channels": 1,
  "sample_width": 2,
  "audio_bytes": 1920,
  "streaming": true,
  "message": "Du lieu am thanh PCM raw"
}
```

2. Binary frame ngay sau do, do dai bang `audio_bytes`.

Binary frame la:

```text
PCM raw
Signed 16-bit little-endian
Mono
Sample rate theo metadata, mac dinh 16000 Hz
```

Client nen xu ly nhu sau:

1. Khi nhan JSON co `processing_process == "pcm_raw_audio"`, luu metadata nay.
2. Binary frame tiep theo la audio cua metadata vua nhan.
3. Kiem tra cau hinh client co muon phat audio hay khong.
4. Neu co, phat PCM theo `sample_rate`, `channels`, `sample_width`.
5. Neu khong, bo qua binary frame nhung van nen log `audio_bytes`.

## 9. Tham so client gui

### JSON cau hinh client

| Field | Kieu | Bat buoc | Y nghia |
| --- | --- | --- | --- |
| `session_id` | string | Khuyen nghi | Ten dinh danh client. |
| `working_mode` | string | Khuyen nghi | `main_processing`, `chatbot`, `stt_to_tts` hoac `stt_to_text`. |
| `vbot_client_id` | string | Khong | Alias cua `session_id`. |
| `client_id` | string | Khong | Alias cua `session_id`. |

### Text command

| Command | Y nghia |
| --- | --- |
| `Skip_WakeUP` | Bat dau recording khong can wake word. |
| `start` | Bat dau recording khong can wake word. |
| `start_recording` | Bat dau recording khong can wake word. |
| `stop` | Ket thuc recording hien tai. |

### Binary microphone

| Tham so | Gia tri |
| --- | --- |
| Format | PCM raw |
| Encoding | signed int16 little-endian |
| Channels | 1 |
| Sample rate | Nen khop STT, thuong la 16000 |
| Wake frame | 512 samples, 1024 bytes |

## 10. Tham so server tra ve

| Field | Co trong | Y nghia |
| --- | --- | --- |
| `processing_process` | Hau het JSON | Trang thai hien tai cua server. |
| `recording_streaming` | `recording` | Server dang nhan audio recording. |
| `wake_word_detected` | `wake_word_detected` | Server da bat dau recording. |
| `waiting_to_wake_up` | `waiting_to_wake_up` | Server quay ve trang thai cho. |
| `transcript_normed` | `data_processing` | Text STT da nhan. |
| `tts_audio` | Ket qua xu ly | URL/path audio neu co. Co the la `null` hoac `"None"`. |
| `status_audio` | Trang thai wake/wait | URL day du cua am thanh trang thai, vi du ding/dong. |
| `assistant_text` | Ket qua xu ly | Text assistant tra ve. |
| `response_text` | Ket qua xu ly | Text response de client hien thi. |
| `audio_format` | `pcm_raw_audio` | Dinh dang PCM raw, hien tai `pcm_s16le`. |
| `sample_rate` | `pcm_raw_audio` | Sample rate cua binary frame tiep theo. |
| `channels` | `pcm_raw_audio` | So kenh audio. |
| `sample_width` | `pcm_raw_audio` | So byte moi sample. |
| `audio_bytes` | `pcm_raw_audio` | Do dai binary frame tiep theo. |
| `streaming` | `pcm_raw_audio` | `true` neu audio duoc gui realtime theo chunk. |

Neu `smart_config.smart_wakeup.wakeup_reply.active = true`, `status_audio` cua `wake_word_detected` se la mot file ngau nhien trong danh sach `wakeup_reply.sound_file` dang active. Neu tat `wakeup_reply`, server dung am thanh mac dinh `Lib.Sound_Start`.

Ngoai le: neu client chu dong gui `Skip_WakeUP`, `status_audio` luon la am thanh mac dinh `Lib.Sound_Start` (`ding.mp3`), khong dung wakeup_reply.

## 11. Vi du client JavaScript nhan `pcm_raw_audio`

```javascript
let pendingPcmMeta = null;
let audioContext = null;
let nextPlayTime = 0;

async function playPcm16(arrayBuffer, meta, shouldPlay) {
  if (!shouldPlay) return;

  const sampleRate = Number(meta.sample_rate) || 16000;
  const channels = Number(meta.channels) || 1;
  audioContext ||= new AudioContext({ sampleRate });

  if (audioContext.state === "suspended") {
    await audioContext.resume();
  }

  const samples = new Int16Array(arrayBuffer);
  const frames = Math.floor(samples.length / channels);
  const audioBuffer = audioContext.createBuffer(channels, frames, sampleRate);

  for (let channel = 0; channel < channels; channel++) {
    const output = audioBuffer.getChannelData(channel);
    for (let i = 0; i < frames; i++) {
      output[i] = samples[i * channels + channel] / 32768;
    }
  }

  const source = audioContext.createBufferSource();
  source.buffer = audioBuffer;
  source.connect(audioContext.destination);

  const startAt = Math.max(audioContext.currentTime + 0.02, nextPlayTime || 0);
  source.start(startAt);
  nextPlayTime = startAt + audioBuffer.duration;
}

socket.onmessage = async (event) => {
  if (typeof event.data === "string") {
    const data = JSON.parse(event.data);
    if (data.processing_process === "pcm_raw_audio") {
      pendingPcmMeta = data;
      console.log("pcm_raw_audio meta", data);
    }
    return;
  }

  if (pendingPcmMeta) {
    const shouldPlay = document.getElementById("playPcmAudio").checked;
    console.log("pcm_raw_audio binary bytes", event.data.byteLength, "play", shouldPlay);
    await playPcm16(event.data, pendingPcmMeta, shouldPlay);
    pendingPcmMeta = null;
  }
};
```

## 12. Luu y

- Client phai dung WebSocket, khong phai TCP raw socket.
- Client nen gui JSON cau hinh ngay sau khi ket noi.
- `working_mode` la theo tung client, khong con cau hinh trong `protocol.socket`.
- Neu nhan `pcm_raw_audio`, binary frame ngay sau do la audio PCM tuong ung.
- Neu client khong muon phat audio, van nen doc binary frame va bo qua de giu dung thu tu message.
- WebSocket server dung ping `ping_interval=20`, `ping_timeout=10`; client nen dung thu vien WebSocket chuan de tu dong pong.

## 13. Cau hinh lien quan

| Key | Y nghia |
| --- | --- |
| `port` | Cong WebSocket server. |
| `maximum_recording_time` | Thoi gian ghi am toi da cho mot luot noi. |
| `maximum_client_connected` | So client WebSocket toi da. |
| `source_stt` | Nguon STT: `stt_ggcloud`, `stt_ggcloud_v2`, `stt_default`. |
| `select_wakeup` | Engine wake word: `snowboy` hoac `porcupine`. |
| `client_conversation_mode` | Cho phep client tiep tuc hoi thoai sau khi xu ly xong. |
| `music_playback_on_client` | Cho phep gui URL media ve client khi xu ly lenh phat nhac. |
| `audio_queue_maxsize` | Tuy chon, kich thuoc queue audio. |
| `ws_open_timeout` | Tuy chon, timeout mo ket noi toi STT websocket phu. |
| `ws_recv_timeout` | Tuy chon, timeout nhan phan hoi tu STT websocket phu. |
| `thread_join_timeout` | Tuy chon, timeout join thread STT. |

`time_remove_inactive_clients` chi dung cho UDP, khong ap dung cho che do socket.
