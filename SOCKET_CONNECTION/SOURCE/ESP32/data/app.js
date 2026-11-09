/*
Code By: Vũ Tuyển
GitHub VBot: https://github.com/marion001/VBot_Offline.git
Facebook Group: https://www.facebook.com/groups/1148385343358824
Facebook: https://www.facebook.com/TWFyaW9uMDAx
Mail: VBot.Assistant@gmail.com
*/
const st=document.getElementById('ajaxStatus');
const msg=document.getElementById('ajaxMessage');
const modalMsg=document.getElementById('modalMessage');
let state=null;
let bootInProgress=false;

const modes=[
  ['main_processing','Luồng chính Máy chủ VBot xử lý'],
  ['chatbot','Luồng chatbot xử lý'],
  ['stt_to_tts','Chỉ chuyển đổi âm thanh sang văn bản text']
];
const startupModes=[
  ['vbot_server','Kết nối tới máy chủ VBot'],
  ['standalone','Không sử dụng']
];
const inputModes=[
  ['button','Nút nhấn thường'],
  ['rotary','Nút nhấn dạng xoay (Rotary Encoder)']
];
const buttonActiveLevels=[
  ['low','Chạm Mức Thấp (GPIO chạm GND)'],
  ['high','Chạm Mức Cao (GPIO chạm 3.3V)']
];
const wakeActions=[
  ['restart','Khởi động lại ESP'],
  ['clean_all','Đặt lại dữ liệu cấu hình Config và WiFi'],
  ['reset_wifi','Đặt lại cấu hình WiFi'],
  ['reset_nvs','Đặt lại cấu hình Config'],
  ['stop_audio','Dừng phát âm thanh'],
  ['toggle_mic','Bật / Tắt Mic']
];
const ledEffects=['LED_SPEAK','LED_THINK','LED_LOADING','LED_MUTE','LED_ERROR','LED_SOCKET_ERROR','LED_STARTUP','LED_PAUSE','LED_VOLUME','LED_OFF'];

function esc(x){return String(x??'').replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function setBusy(v,t){st.classList.toggle('busy',v);if(t!==undefined)msg.innerHTML=t;}
function showModal(t){
  modalMsg.innerHTML=esc(t);
  modalMsg.hidden=false;
}
function showModalHtml(h){
  modalMsg.innerHTML=h;
  modalMsg.hidden=false;
}
function section(t){return `<tr><th colspan="2" class="table-light text-center text-danger">${esc(t)}</th></tr>`;}
function tableBlock(t,rows,extra='table-striped table-hover'){return `<table class="table table-bordered ${extra} align-middle bg-white shadow-sm mb-3">${section(t)}${rows.join('')}</table>`;}
function input(n,l,v,type='text',extra='',placeholder=''){const b=n==='path'?'border-danger':'border-success';const ph=placeholder?`placeholder="${esc(placeholder)}"`:'';return `<tr><td>${esc(l)}</td><td><input class="form-control ${b}" name="${n}" type="${type}" value="${esc(v)}" ${ph} ${extra}></td></tr>`;}
function note(t){return `<tr><td></td><td><small class="text-danger">${esc(t)}</small></td></tr>`;}
function check(n,l,v){return `<tr><td>${esc(l)}</td><td><div class="form-check form-switch"><input class="form-check-input" name="${n}" type="checkbox" value="1" ${v?'checked':''}></div></td></tr>`;}
function select(n,l,opts,val){return `<tr><td>${esc(l)}</td><td><select class="form-select border-success" name="${n}">${opts.map(o=>`<option value="${esc(o[0])}" ${String(o[0])===String(val)?'selected':''} ${o[2]?'disabled':''}>${esc(o[1])}</option>`).join('')}</select></td></tr>`;}
function color(n,l,v){return `<tr><td>${esc(l)}</td><td><div class="color-row"><input class="form-control form-control-color border-success" name="${n}_picker" type="color" value="${esc(v)}" data-sync="${n}"><input class="form-control border-success" name="${n}" type="text" value="${esc(v)}" maxlength="7" pattern="#[0-9A-Fa-f]{6}"></div></td></tr>`;}
function fmtUptime(ms){let s=Math.floor(ms/1000),d=Math.floor(s/86400);s%=86400;let h=Math.floor(s/3600);s%=3600;let m=Math.floor(s/60);s%=60;return `${d?d+' ngày ':''}${h} giờ ${m} phút ${s} giây`;}
function fmtBytes(b){return `${b} bytes (${(b/1024).toFixed(1)} KB / ${(b/1048576).toFixed(2)} MB)`;}
function actionUrl(path){return new URL(path, location.href).href;}

function loadServerAssets(host){
  if(!host)return;
  const icon=document.createElement('link');icon.rel='icon';icon.href=`http://${host}/assets/img/favicon.png`;document.head.appendChild(icon);
}

function renderStatus(){
  const s=state.status;
  const hasMetric=v=>typeof v==='number'&&Number.isFinite(v)&&v>0;
  const firmwareMetricsReady=hasMetric(s.flash_size)&&hasMetric(s.sketch_size);
  const littleFsMetricsReady=hasMetric(s.littlefs_total)&&typeof s.littlefs_used==='number';
  const gpioStatusKnown=typeof s.hardware_gpio_valid==='boolean';
  const taskRows=Object.entries(s.tasks||{}).map(([name,task])=>`<tr><td>Task ${esc(name)}</td><td>Stack cấu hình: ${esc(task.stack_configured||0)}; high-water còn lại: ${esc(task.stack_high_water||0)}</td></tr>`).join('');
  const flashPct=s.flash_size?Math.min(100,Math.round((s.sketch_size||0)*100/s.flash_size)):0;
  const fsPct=s.littlefs_total?Math.min(100,Math.round((s.littlefs_used||0)*100/s.littlefs_total)):0;
  const heapPct=Math.min(100,Math.round((s.free_heap||0)*100/(200*1024)));
  const card=(label,value,meta,ok)=>`<div class="status-card ${ok===true?'ok':ok===false?'bad':''}"><div class="status-label">${esc(label)}</div><div class="status-value"><span class="dot"></span>${esc(value)}</div><div class="status-meta">${esc(meta||'')}</div></div>`;
  const resource=(label,value,pct,warnHigh=true)=>`<div class="resource-card"><div class="resource-head"><strong>${esc(label)}</strong><span>${esc(value)}</span></div><div class="progress"><div class="progress-bar ${warnHigh&&pct>85?'warn':''}" style="width:${pct}%"></div></div></div>`;
  document.title=`${s.client_name} - VBot Client`;
  document.getElementById('pageTitle').textContent=s.client_name;
  const mqttOk=!s.mqtt_enabled||s.mqtt_connected;
  document.getElementById('statusMount').innerHTML=`
    <div class="dashboard-grid">
      ${card('WiFi',s.wifi_connected?'Đã kết nối':'Mất kết nối',s.wifi_connected?`${s.wifi_ssid||'-'} · ${s.rssi} dBm`:'Chưa có mạng',s.wifi_connected)}
      ${card('WebSocket',s.websocket_connected?'Đã kết nối':'Mất kết nối',s.startup_mode==='standalone'?'Chế độ độc lập':s.socket,s.websocket_connected||s.startup_mode==='standalone')}
      ${card('MQTT',s.mqtt_enabled?(s.mqtt_connected?'Đã kết nối':'Mất kết nối'):'Đang tắt',s.mqtt_enabled?'MQTT Broker':'Không sử dụng',mqttOk)}
      ${card('GPIO',gpioStatusKnown?(s.hardware_gpio_valid?'Hợp lệ':'Safe mode'):'Chưa có dữ liệu',gpioStatusKnown?(s.hardware_gpio_valid?'Phần cứng sẵn sàng':'Cần sửa cấu hình'):'Cần nạp firmware cùng phiên bản WebUI',gpioStatusKnown?s.hardware_gpio_valid:null)}
    </div>
    <div class="resource-grid">
      ${resource('Firmware',firmwareMetricsReady?`${(s.sketch_size/1048576).toFixed(2)} / ${(s.flash_size/1048576).toFixed(1)} MB`:'Chưa có dữ liệu',firmwareMetricsReady?flashPct:0)}
      ${resource('LittleFS',littleFsMetricsReady?`${(s.littlefs_used/1024).toFixed(0)} / ${(s.littlefs_total/1024).toFixed(0)} KB`:'Chưa có dữ liệu',littleFsMetricsReady?fsPct:0)}
      ${resource('Heap còn trống',`${((s.free_heap||0)/1024).toFixed(1)} KB`,heapPct,false)}
    </div>
    ${!firmwareMetricsReady||!littleFsMetricsReady||!gpioStatusKnown?'<div class="alert alert-warning gpio-alert"><strong>Firmware và WebUI không đồng bộ.</strong> Hãy nạp firmware.bin và littlefs.bin được tạo từ cùng một lần build.</div>':''}
    ${s.hardware_gpio_errors?`<div class="alert alert-danger gpio-alert"><strong>Lỗi cấu hình GPIO</strong>\n${esc(s.hardware_gpio_errors)}</div>`:''}
    <details class="detail-card"><summary>Thông tin thiết bị và trạng thái task</summary><div class="detail-body"><table class="table table-sm table-bordered mb-0">
      <tr><td>Thiết bị</td><td>${esc(s.chip_model)} rev ${esc(s.chip_revision)} · ${esc(s.chip_cores)} cores · ${esc(s.chip_target)}</td></tr>
      <tr><td>Firmware</td><td>${esc(s.version)}</td></tr><tr><td>Địa chỉ IP</td><td>${esc(s.ip)}</td></tr>
      <tr><td>Chế độ khởi chạy</td><td>${esc(s.startup_mode==='standalone'?'Độc lập':'Kết nối máy chủ VBot')}</td></tr>
      <tr><td>Uptime</td><td>${fmtUptime(s.uptime_ms)}</td></tr><tr><td>Min free heap</td><td>${fmtBytes(s.min_free_heap)}</td></tr>${taskRows}
    </table></div></details>`;
}

function renderConfig(){
  const c=state.config;
  const isS3=String(state.status?.chip_target||'').startsWith('ESP32S3');
  const gpioDefaults=isS3
    ? {mbclk:5,mws:4,mdin:6,dbclk:15,dlrc:16,ddout:7,pled:48,bmic:41,bup:40,bdn:39,bwake:42,rclk:41,rdt:40,rsw:42}
    : {mbclk:14,mws:13,mdin:34,dbclk:27,dlrc:26,ddout:25,pled:23,bmic:32,bup:33,bdn:18,bwake:19,rclk:32,rdt:33,rsw:19};
  const h=[];
  h.push(tableBlock('Cấu Hình Máy Chủ Socket VBot',[
    select('startupmode','Chế độ khởi chạy',startupModes,c.client.startup_mode||'vbot_server'),
    note('Không sử dụng: ESP32 không khởi tạo Mic và không kết nối WebSocket; WebUI, MQTT, mDNS, phát âm thanh, nút nhấn, LED và OTA vẫn hoạt động.'),
    input('cname','Tên Client',c.client.name,'text','maxlength="32"','VBot Client Socket'),
    input('host','Máy chủ socket',c.server.host,'text','','192.168.1.10'),
    input('port','Địa chỉ port Socket',c.server.port,'number','','5003'),
    input('apiport','Địa chỉ port API',c.server.api_port||5002,'number','','5002'),
    input('path','Socket path (Mặc định: /)',c.server.path,'text','','/'),
    input('sapi','URL API stream audio_proxy',c.server.stream_api_url||'','url','','http://ip_VBot_Server:5002/audio_proxy?url='),
    note('ESP32 có thể phát HTTPS trực tiếp, nhưng audio_proxy nội bộ giúp xử lý redirect, định dạng và kết nối ổn định hơn.'),
    `<tr><td></td><td><small class="text-muted">Tải file làm server stream proxy: <a href="https://github.com/marion001/VBot_Client_Offline/tree/main/SOCKET_CONNECTION/Audio_Proxy_Server_Demo" target="_blank" rel="noopener noreferrer">Audio_Proxy_Server_Demo</a></small></td></tr>`,
    check('usesapi','Sử dụng URL API stream đã nhập',c.server.use_custom_stream_api_url),
    check('sfbapi','Nếu URL stream lỗi thì dùng API máy chủ đang kết nối',c.server.fallback_stream_api_to_server),
    select('mode','Chế độ làm việc',modes,c.client.working_mode)
  ]));
  const hasPsram = Boolean(c.audio.psram_active ?? state.status?.psram_active ?? c.audio.wakenet_supported);
  const wnSupported = Boolean(c.audio.wakenet_supported);
  const wnOptionText = hasPsram ? 'WakeNet On-Device PSRAM (ESP32 / S3)' : 'WakeNet On-Device PSRAM (ESP32 / S3) [Không có PSRAM]';
  const hotwordModes = [
    ['server', 'Sử Dụng Hotword Ở Máy Chủ VBot'],
    ['wakenet', wnOptionText, !hasPsram],
    ['none', 'Không Sử Dụng']
  ];
  let currentHotwordMode = c.audio.hotword_mode || (c.audio.wakenet_enabled ? 'wakenet' : (c.audio.hotword_stream_enabled ? 'server' : 'none'));
  if (!hasPsram && currentHotwordMode === 'wakenet') {
    currentHotwordMode = c.audio.hotword_stream_enabled ? 'server' : 'none';
  }
  let wakenetModels = [];
  if (c.audio.wakenet_models && c.audio.wakenet_models.length > 0) {
    wakenetModels = c.audio.wakenet_models.map(m => [String(m.id), m.name]);
  } else {
    wakenetModels = [
      ['0', 'Hi Lily (wn9_hilili_tts) [Mặc định]'],
      ['1', 'Alexa (wn9_alexa)'],
      ['2', 'Sophia (wn9_sophia_tts)'],
      ['3', 'Jarvis (wn9_jarvis_tts)']
    ];
  }
  h.push(tableBlock('Cấu Hình Âm Thanh / Mic',[
    check('micen','Bật Tắt Mic',c.audio.mic_enabled),
    check('mvad','Lọc khoảng lặng thông minh (VAD) khi chờ đánh thức',c.audio.mic_vad_enabled!==false),
    note('Khi bật VAD, ESP32 tự động ngắt gửi gói âm thanh lúc phòng yên tĩnh và dùng bộ đệm nhìn trước (Pre-Roll 128ms) để giữ nguyên độ nhạy đánh thức.'),
    check('conv','Chế độ hội thoại',c.audio.conversation_mode),
    check('bootsnd','Âm thanh khi khởi động',c.audio.startup_sound_enabled),
    input('vol','Âm Lượng (0-100)',c.audio.volume,'number','min="0" max="100"','70'),
    input('micgain','Khuếch đại Mic (gain 0.1-16)',c.audio.mic_gain,'number','min="0.1" max="16" step="0.1"','2.0'),
    input('pcmboost','Hệ số âm lượng Audio PCM RAW (0.1-16)',c.audio.pcm_volume_boost,'number','min="0.1" max="16" step="0.1"','1.0'),
    note('Hệ số 1.0 giữ nguyên PCM; 0.5 giảm còn một nửa; lớn hơn 1.0 khuếch đại. Âm lượng tổng của loa vẫn tiếp tục được áp dụng.')
  ]));

  let hotwordTabContent = tableBlock('Cấu Hình Chế Độ Hotword / Đánh Thức',[
    select('hotword_mode','Chế độ Hotword / Đánh thức',hotwordModes,currentHotwordMode),
    select('wn_model','Từ khóa WakeNet On-Device',wakenetModels,c.audio.wakenet_model!=null?c.audio.wakenet_model:'0'),
    note('Chế độ WakeNet chạy trực tiếp trên chip ESP32/ESP32-S3 không tốn băng thông Wi-Fi ở chế độ chờ và đánh thức tức thì 0ms. Khi chọn Hotword Máy Chủ, ESP32 sẽ stream âm thanh về máy chủ VBot để nhận diện từ khóa "VBot ơi".')
  ]);

  if (wnSupported) {
    const customHotwords = c.audio.custom_hotwords || [];
    const customCount = c.audio.custom_hotwords_count || customHotwords.filter(cw => cw.active).length;
    const maxCustom = c.audio.custom_hotwords_max || 5;

    let hwRows = [];
    const defaultList = [
      { name: 'Hi Lily', key: 'wn9_hilili_tts' },
      { name: 'Alexa', key: 'wn9_alexa' },
      { name: 'Sophia', key: 'wn9_sophia_tts' },
      { name: 'Jarvis', key: 'wn9_jarvis_tts' }
    ];
    defaultList.forEach((dh) => {
      hwRows.push(`<tr>
        <td><span class="badge bg-secondary">Mặc định</span> <strong>${esc(dh.name)}</strong></td>
        <td><code>${esc(dh.key)}</code></td>
        <td>Flash Partition</td>
        <td><span class="text-muted small"><i class="bi bi-lock-fill"></i> Cố định (Không thể xóa)</span></td>
      </tr>`);
    });

    for (let s = 0; s < maxCustom; s++) {
      const slotData = customHotwords.find(cw => cw.slot === s && cw.active);
      if (slotData) {
        hwRows.push(`<tr>
          <td><span class="badge bg-success">Tùy chỉnh ${s+1}</span> <strong>${esc(slotData.name)}</strong></td>
          <td><code>${esc(slotData.key)}</code></td>
          <td>${fmtBytes(slotData.size || 0)}</td>
          <td>
            <button type="button" class="btn btn-outline-danger btn-sm" onclick="deleteCustomHotword(${s}, '${esc(slotData.name)}')">
              <i class="bi bi-trash"></i> Xóa
            </button>
          </td>
        </tr>`);
      }
    }

    let uploadCard = '';
    if (customCount < maxCustom) {
      uploadCard = `
        <div class="card bg-light border p-3 mt-3">
          <div class="d-flex flex-wrap justify-content-between align-items-center gap-2 mb-2">
            <div class="fw-bold text-primary"><i class="bi bi-cloud-arrow-up"></i> Tải Lên Hotword Mới (Còn trống ${maxCustom - customCount}/${maxCustom} slot)</div>
            <a href="https://github.com/marion001/VBot_Client_Offline/tree/main/SOCKET_CONNECTION/ESP_WakeNet_Hotword_Sample" target="_blank" rel="noopener noreferrer" class="btn btn-outline-primary btn-sm">
              <i class="bi bi-github"></i> Danh Sách Mẫu WakeUp Hotword WakeNet
            </a>
          </div>
          <div id="hotwordUploadBox">
            <div class="row g-2 align-items-center">
              <div class="col-md-5">
                <label class="form-label small mb-1">Tệp mô hình WakeNet (.bin):</label>
                <input class="form-control form-control-sm border-primary" type="file" id="hwFileInput" accept=".bin">
              </div>
              <div class="col-md-4">
                <label class="form-label small mb-1">Tên hiển thị gợi nhớ (tùy chọn):</label>
                <input class="form-control form-control-sm" type="text" id="hwNameInput" placeholder="Ví dụ: VBot Ơi">
              </div>
              <div class="col-md-3 d-flex align-items-end">
                <button type="button" id="hwUploadBtn" class="btn btn-primary btn-sm w-100" onclick="uploadCustomHotword()">
                  <i class="bi bi-upload"></i> Nạp Hotword
                </button>
              </div>
            </div>
            <div class="form-text mt-1 text-muted small">Chỉ chấp nhận file nhị phân WakeNet đóng gói chuẩn <code>.bin</code> (dung lượng các mô hình chuẩn thường khoảng ~280KB - 300KB, hỗ trợ tải lên tối đa tới 2MB). Tải các mẫu có sẵn tại <a href="https://github.com/marion001/VBot_Client_Offline/tree/main/SOCKET_CONNECTION/ESP_WakeNet_Hotword_Sample" target="_blank" rel="noopener noreferrer">Danh Sách Mẫu WakeUp Hotword WakeNet</a>. Sau khi nạp thành công, từ khóa sẽ xuất hiện trong danh sách lựa chọn bên trên.</div>
          </div>
        </div>`;
    } else {
      uploadCard = `
        <div class="alert alert-warning py-2 mt-3 mb-0 d-flex flex-wrap justify-content-between align-items-center gap-2">
          <div><i class="bi bi-exclamation-triangle-fill"></i> Đã đạt giới hạn tối đa ${maxCustom} hotword tùy chỉnh. Vui lòng xóa bớt một hotword nếu muốn tải lên từ khóa mới.</div>
          <a href="https://github.com/marion001/VBot_Client_Offline/tree/main/SOCKET_CONNECTION/ESP_WakeNet_Hotword_Sample" target="_blank" rel="noopener noreferrer" class="btn btn-outline-dark btn-sm text-nowrap">
            <i class="bi bi-github"></i> Danh Sách Mẫu WakeUp Hotword WakeNet
          </a>
        </div>`;
    }

    hotwordTabContent += `
      <div id="wakenetManagerCard" class="card shadow-sm mb-4">
        <div class="card-header bg-dark text-white fw-bold d-flex justify-content-between align-items-center">
          <span><i class="bi bi-mic"></i> Quản Lý Hotword On-Device (ESP WakeNet)</span>
          <span class="badge bg-primary text-white">${customCount}/${maxCustom} Tùy chỉnh</span>
        </div>
        <div class="card-body">
          <div class="table-responsive">
            <table class="table table-sm table-hover align-middle mb-0">
              <thead class="table-light">
                <tr><th>Tên Hotword</th><th>Key nhận diện</th><th>Dung lượng</th><th>Thao tác</th></tr>
              </thead>
              <tbody>
                ${hwRows.join('')}
              </tbody>
            </table>
          </div>
          ${uploadCard}
        </div>
      </div>
    `;
  }
  h.push(hotwordTabContent);
  h.push(tableBlock('Cấu Hình mDNS',[
    check('mdns','Cho phép các thiết bị trong mạng LAN phát hiện ESP32 VBot Client',c.network?.mdns_enabled!==false)
  ]));
  const customApiUrl=state.status?.ip?`http://${state.status.ip}`:location.origin;
  h.push(tableBlock('Cấu Hình MQTT',[
    check('mqen','Kích Hoạt MQTT',c.mqtt?.active||false),
    input('mqhost','Địa chỉ MQTT Broker',c.mqtt?.host||'','text','','192.168.1.10'),
    input('mqport','Cổng MQTT Broker',c.mqtt?.port||1883,'number','min="1" max="65535"','1883'),
    input('mquser','Tài khoản MQTT',c.mqtt?.username||'','text','',''),
    `<tr><td>Mật khẩu MQTT</td><td><div class="input-group"><input id="mqttPasswordInput" class="form-control border-success" name="mqpass" type="password" value="${esc(c.mqtt?.password||'')}"><span class="input-group-text"><input id="mqttPasswordVisible" class="form-check-input mt-0 me-2" type="checkbox" onchange="document.getElementById('mqttPasswordInput').type=this.checked?'text':'password'"><label for="mqttPasswordVisible">Hiển thị mật khẩu</label></span></div></td></tr>`,
    input('mqclient','Tên Client MQTT',c.mqtt?.client_name||'VBot_Client_ESP32','text','maxlength="39"','VBot_Client_ESP32'),
    `<tr><td>URL API</td><td><input class="form-control border-success" type="text" value="${esc(customApiUrl)}" readonly onclick="this.select()" title="Nhấn để chọn và sao chép URL API khi cấu hình Custom thủ công"></td></tr>`,
    `<tr><td></td><td><small class="text-danger">Dùng Custom Component VBot Assistant để liên kết ESP32 VBot Client lên Home Assistant: <a href="https://github.com/marion001/VBot_Offline_Custom_Component" target="_blank" rel="noopener noreferrer">marion001/VBot_Offline_Custom_Component</a></small></td></tr>`
  ]));
  h.push(tableBlock('Cấu Hình I2S Mic INMP441',[
    input('mbclk','SCK/BCLK',c.i2s_mic.bclk,'number','',String(gpioDefaults.mbclk)),
    input('mws','WS/LRC',c.i2s_mic.ws,'number','',String(gpioDefaults.mws)),
    input('mdin','SD',c.i2s_mic.din,'number','',String(gpioDefaults.mdin)),
    select('mch','Kênh Mic',[['L','Kênh Trái - Left (L)'],['R','Kênh Phải - Right (R)']],c.i2s_mic.channel)
  ]));
  h.push(tableBlock('Cấu Hình I2S DAC / MAX98357 (Giải mã âm thanh)',[
    input('dbclk','BCLK/SCK',c.i2s_dac.bclk,'number','',String(gpioDefaults.dbclk)),
    input('dlrc','LRC/WS',c.i2s_dac.lrc,'number','',String(gpioDefaults.dlrc)),
    input('ddout','DIN',c.i2s_dac.dout,'number','',String(gpioDefaults.ddout))
  ]));
  h.push(tableBlock('Cấu Hình LED WS2812',[
    check('leden','Kích Hoạt',c.led.active),
    input('pled','LED GPIO',c.led.gpio,'number','',String(gpioDefaults.pled)),
    input('ledbr','Độ sáng LED (0-100)',c.led.brightness_percent,'number','min="0" max="100"','100'),
    input('ledcnt','Số lượng LED',c.led.count,'number','min="1" max="300"','12'),
    color('lthink','Màu LED khi đánh thức',c.led.think_color),
    color('lmute','Màu LED khi Mic bị tắt',c.led.mute_color)
  ]));
  h.push(tableBlock('Cấu Hình Nút Nhấn / Rotary Encoder',[
    //section('Cấu Hình Chế Độ Nút Nhấn'),
    check('btnen','Kích Hoạt',c.button.active),
    select('inputmode','Chế độ sử dụng nút nhấn',inputModes,c.button.input_mode||'button'),
    select('wla','Hành động nhấn giữ WakeUP / Rotary SW',wakeActions,c.button.wakeup_long_action),
    input('bsdelay','Thời gian trễ ổn định khi khởi động ms',c.button.startup_delay_ms??3000,'number','min="0" max="10000"','3000'),
    input('bdbp','Thời gian khi nhấn (debounce) ms',c.button.press_debounce_ms,'number','min="10" max="1000"','50'),
    input('bdbr','Thời gian khi thả (debounce) ms',c.button.release_debounce_ms,'number','min="10" max="1000"','50'),
    input('bhold','Thời gian nhấn giữ nút (debounce) ms',c.button.hold_ms,'number','min="300" max="10000"','1500'),
    section('Cấu Hình Sử Dụng Nút Nhấn Thường'),
    select('btnlevel','Mức kích hoạt nút nhấn',buttonActiveLevels,c.button.active_level||'low'),
    input('bmic','Nút nhấn Mic GPIO',c.button.mic,'number','',String(gpioDefaults.bmic)),
    input('bup','Nút nhấn Volume+ GPIO',c.button.volume_up,'number','',String(gpioDefaults.bup)),
    input('bdn','Nút nhấn Volume- GPIO',c.button.volume_down,'number','',String(gpioDefaults.bdn)),
    input('bwake','Nút nhấn WakeUP GPIO',c.button.wakeup,'number','',String(gpioDefaults.bwake)),
    section('Cấu Hình Nút Nhấn Dạng Xoay (Rotary Encoder)'),
    input('rclk','Rotary Encoder CLK GPIO',c.rotary?.clk??gpioDefaults.rclk,'number','',String(gpioDefaults.rclk)),
    input('rdt','Rotary Encoder DT GPIO',c.rotary?.dt??gpioDefaults.rdt,'number','',String(gpioDefaults.rdt)),
    input('rsw','Rotary Encoder SW GPIO',c.rotary?.sw??gpioDefaults.rsw,'number','',String(gpioDefaults.rsw)),
    input('rstep','Bước âm lượng khi xoay Rotary',c.rotary?.volume_step??2,'number','min="1" max="20"','2')
  ]));
  h.push(tableBlock('Cấu Hình Hiển Thị Logs',[
    check('logs','Hiển thị logs ra Serial (Baud rate 115200)',c.logs.serial)
  ]));
  const defaults=isS3
    ? [['Mic','WS 4 · SCK 5 · SD 6'],['MAX98357','DIN 7 · BCLK 15 · LRC 16'],['LED','GPIO48'],['Button','Wake 42 · Vol- 39 · Vol+ 40 · Mic 41']]
    : [['Mic','WS 13 · SCK 14 · SD 34'],['MAX98357','DIN 25 · BCLK 27 · LRC 26'],['LED','GPIO23'],['Button','Mic 32 · Vol+ 33 · Vol- 18 · Wake 19']];
  document.getElementById('configMount').innerHTML=`<section class="config-shell">
    <div class="config-title"><h2>Cấu hình thiết bị</h2><span>${isS3?'ESP32-S3':'ESP32'} · Thay đổi được áp dụng sau khi khởi động lại</span></div>
    <ul class="nav nav-tabs" role="tablist">
      <li class="nav-item"><button class="nav-link active" data-bs-toggle="tab" data-bs-target="#cfgServer" type="button">Máy chủ</button></li>
      <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#cfgAudio" type="button">Âm thanh</button></li>
      <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#cfgHotword" type="button">Cấu Hình Hotword WakeUP</button></li>
      <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#cfgNetwork" type="button">Mạng & MQTT</button></li>
      <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#cfgGpio" type="button">GPIO</button></li>
      <li class="nav-item"><button class="nav-link" data-bs-toggle="tab" data-bs-target="#cfgSystem" type="button">Hệ thống</button></li>
    </ul>
    <div class="tab-content">
      <div class="tab-pane fade show active" id="cfgServer">${h[0]}</div>
      <div class="tab-pane fade" id="cfgAudio">${h[1]}</div>
      <div class="tab-pane fade" id="cfgHotword">${h[2]}</div>
      <div class="tab-pane fade" id="cfgNetwork">${h[3]}${h[4]}</div>
      <div class="tab-pane fade" id="cfgGpio"><div class="gpio-guide">${defaults.map(x=>`<div class="gpio-chip"><strong>${esc(x[0])}</strong><br>${esc(x[1])}</div>`).join('')}</div><div id="gpioValidation"></div>${h[5]}${h[6]}${h[7]}${h[8]}</div>
      <div class="tab-pane fade" id="cfgSystem">${h[9]}</div>
    </div>
  </section><div class="save-bar"><small>Mọi thay đổi được kiểm tra trước khi lưu vào NVS.</small><button id="saveConfigBtn" class="btn btn-success" type="submit">Lưu cấu hình & khởi động lại</button></div>`;
}

function renderActions(){
  const hardwareReady=state.status.hardware_gpio_valid!==false;
  const hwDisabled=hardwareReady?'':'disabled title="GPIO đang ở chế độ an toàn"';
  const hardwareWarning=hardwareReady?'':`<tr><td>Cảnh báo phần cứng</td><td><div class="alert alert-danger py-2 mb-0">Cấu hình GPIO không hợp lệ. Phát âm thanh, TTS và kiểm tra LED đã bị khóa; WebUI và các chức năng sửa cấu hình vẫn hoạt động.</div></td></tr>`;
  document.getElementById('actionsMount').innerHTML=`<table class="table table-bordered align-middle bg-white shadow-sm">
    ${section('Hành Động, Thao Tác Khác')}
    ${hardwareWarning}
    <tr><td>Chương Trình, Firmware</td><td><a class="btn btn-outline-primary btn-sm me-2 mb-2" href="/update">Flash Chương Trình, Firmware</a><button id="checkUpdateBtn" type="button" class="btn btn-outline-primary btn-sm me-2 mb-2">Kiểm tra phiên bản mới</button><a class="btn btn-info btn-sm me-2 mb-2" href="/VBot_Client_Info" target="_blank">API VBot Client</a><a class="btn btn-info btn-sm mb-2" href="/api/webui_state" target="_blank">API WebUI State</a></td></tr>
    <tr><td>Cấu Hình WiFi</td><td><button id="scanWifiBtn" type="button" class="btn btn-outline-primary btn-sm me-2 mb-2">Quét mạng WiFi</button><form class="d-inline-block" method="post" action="/connect_wifi" data-confirm="Bạn có chắc chắn muốn kết nối đến mạng WiFi này?"><input id="wifiSsid" class="form-control border-success form-control-sm d-inline-block me-2 mb-2" style="width:220px" name="ssid" type="text" placeholder="Tên mạng Wifi"><input class="form-control border-success form-control-sm d-inline-block me-2 mb-2" style="width:220px" name="password" type="text" placeholder="Mật khẩu Wifi"><button class="btn btn-success btn-sm mb-2">Kết nối WiFi</button></form><div id="wifiScanResult" class="mt-2"></div></td></tr>
    <tr><td>Sao Lưu, Khôi Phục Cấu Hình</td><td><form class="d-inline" method="get" action="/download_config"><button class="btn btn-outline-info btn-sm me-2 mb-2">Tải xuống cấu hình Config</button></form><form class="d-inline-block" method="post" action="/restore_config" enctype="multipart/form-data" data-confirm="Bạn có chắc chắn muốn khôi phục cấu hình từ tệp đã chọn?"><input class="form-control border-success form-control-sm d-inline-block me-2 mb-2" style="width:260px" name="config" type="file" accept=".json,application/json" required><button class="btn btn-success btn-sm mb-2">Tải lên cấu hình khôi phục</button></form></td></tr>
    <tr><td>Âm Thanh</td><td><form class="d-inline-block" method="post" action="/play_url"><input class="form-control border-success form-control-sm d-inline-block me-2 mb-2" style="width:320px" name="audio_url" type="url" placeholder="http://.../audio.mp3" ${hwDisabled}><button class="btn btn-success btn-sm me-2 mb-2" ${hwDisabled}>Play URL</button></form><form class="d-inline" method="post" action="/stop_audio"><button class="btn btn-warning btn-sm mb-2">Stop Audio</button></form></td></tr>
    <tr><td>Google Translate TTS</td><td><form class="d-inline-block" method="post" action="/google_tts"><input id="ttsTextInput" class="form-control border-success form-control-sm d-inline-block me-2 mb-2" style="width:420px" name="text" type="text" placeholder="Nhập nội dung tiếng Việt cần phát" required ${hwDisabled}><button class="btn btn-success btn-sm me-2 mb-2" ${hwDisabled}>Phát TTS</button></form><div id="ttsByteCount" class="small text-muted">0 / 240 byte UTF-8</div><div class="small text-muted">ESP32 có thể phát HTTPS trực tiếp; audio_proxy chỉ được dùng khi cấu hình yêu cầu hoặc làm phương án dự phòng.</div></td></tr>
    <tr><td>Test hiệu ứng LED</td><td><form class="d-inline-block" method="post" action="/test_led"><select class="form-select border-success form-select-sm d-inline-block me-2 mb-2" style="width:180px" name="effect" ${hwDisabled}>${ledEffects.map(x=>`<option>${x}</option>`).join('')}</select><button class="btn btn-info btn-sm mb-2" ${hwDisabled}>Kiểm tra hiệu ứng</button></form></td></tr>
    <tr><td>Hệ thống</td><td><form class="d-inline" method="post" action="/resetwifi" data-confirm="Bạn có chắc chắn muốn đặt lại cấu hình WiFi?"><button class="btn btn-outline-danger btn-sm me-2 mb-2">Reset WiFi</button></form><form class="d-inline" method="post" action="/resetnvs" data-confirm="Bạn có chắc chắn muốn đặt lại cấu hình Config?"><button class="btn btn-outline-danger btn-sm me-2 mb-2">Đặt lại cấu hình Config</button></form><form class="d-inline" method="post" action="/restart"><button class="btn btn-danger btn-sm me-2 mb-2">Khởi động lại ESP</button></form><form class="d-inline" method="post" action="/factoryreset" data-confirm="Bạn có chắc chắn muốn đặt lại toàn bộ cấu hình Config và Wifi?"><button class="btn btn-danger btn-sm mb-2">Đặt lại Cấu Hình Config và Wifi</button></form></td></tr>
  </table>`;
}

function renderLittleFsPlaceholder(){
  const mount=document.getElementById('littlefsMount');
  if(!mount)return;
  mount.innerHTML=tableBlock('Thông Tin Bộ Nhớ LittleFS',[
    `<tr><td>Thông tin</td><td><button id="showFsBtn" type="button" class="btn btn-outline-primary btn-sm me-2 mb-2">Hiển thị thông tin</button></td></tr>`
  ],'table-hover');
  document.getElementById('showFsBtn').addEventListener('click',renderLittleFs);
}

async function renderLittleFs(){
  const mount=document.getElementById('littlefsMount');
  if(!mount)return;
  try{
    mount.innerHTML=tableBlock('Thông Tin Bộ Nhớ LittleFS',[
      `<tr><td>Trạng thái</td><td><span class="text-primary">Đang tải thông tin LittleFS...</span></td></tr>`
    ],'table-hover');
    const r=await fetch('/api/littlefs',{cache:'no-store'});
    const data=await r.json();
    if(!data.success)throw new Error(data.message||'Không đọc được LittleFS');
    const usedPct=data.total_bytes?Math.round((data.used_bytes/data.total_bytes)*100):0;
    let rows=`
      <tr><td>Tổng dung lượng</td><td>${fmtBytes(data.total_bytes||0)}</td></tr>
      <tr><td>Dung lượng đã dùng</td><td>${fmtBytes(data.used_bytes||0)} (${usedPct}%)</td></tr>
      <tr><td>Dung lượng còn trống</td><td>${fmtBytes(data.free_bytes||0)}</td></tr>
      <tr><td>Chức năng</td><td>
        <button id="refreshFsBtn" type="button" class="btn btn-outline-primary btn-sm me-2 mb-2">Danh Sách File</button>
        <form class="d-inline" method="post" action="/littlefs_format" data-confirm="Format bộ nhớ LittleFS? Sau khi format cần nạp lại giao diện WebUI bằng file littlefs.bin">
        <button class="btn btn-danger btn-sm me-2 mb-2">Format filesystem</button></form>
      </td></tr>`;
    const files=Array.isArray(data.files)?data.files:[];
    rows+=`<tr><td>Danh sách file</td><td><div class="table-responsive"><table class="table table-sm table-bordered mb-0"><thead><tr><th>Loại</th><th>Đường dẫn</th><th>Dung lượng</th></tr></thead><tbody>`;
    rows+=files.length?files.map(f=>`<tr><td>${f.directory?'Thư mục':'File'}</td><td><code>${esc(f.path)}</code></td><td>${f.directory?'-':fmtBytes(f.size||0)}</td></tr>`).join(''):`<tr><td colspan="3" class="text-muted">LittleFS chưa có file</td></tr>`;
    rows+=`</tbody></table></div></td></tr>`;
    mount.innerHTML=tableBlock('Thông Tin Bộ Nhớ LittleFS', [rows], 'table-hover');
    document.getElementById('refreshFsBtn').addEventListener('click',renderLittleFs);
    bindPostForms(mount);
  }catch(err){
    mount.innerHTML=tableBlock('Thông Tin Bộ Nhớ LittleFS',[
      `<tr><td>Trạng thái</td><td><span class="text-danger">${esc(String(err))}</span></td></tr>`,
      `<tr><td>Chức năng</td><td><button id="showFsBtn" type="button" class="btn btn-outline-primary btn-sm me-2 mb-2">Thử lại</button></td></tr>`
    ],'table-hover');
    document.getElementById('showFsBtn').addEventListener('click',renderLittleFs);
  }
}

function wifiReconnectMessage(){
  return 'Đã gửi cấu hình WiFi. Nếu ESP32 đổi IP hoặc khởi động lại, trình duyệt có thể mất kết nối tới địa chỉ cũ. Vui lòng mở lại WebUI bằng IP mới của ESP32.';
}

function updateHotwordModeUi(){
  const mode=document.querySelector('[name=hotword_mode]')?.value;
  const isWn=mode==='wakenet';
  document.querySelector('[name=wn_model]')?.closest('tr')?.classList.toggle('mode-hidden',!isWn);
  const card=document.getElementById('wakenetManagerCard');
  if(card)card.style.display=isWn?'':'none';
}

function updateInputModeUi(){
  const rotary=document.querySelector('[name=inputmode]')?.value==='rotary';
  ['btnlevel','bmic','bup','bdn','bwake'].forEach(n=>document.querySelector(`[name=${n}]`)?.closest('tr')?.classList.toggle('mode-hidden',rotary));
  ['rclk','rdt','rsw','rstep'].forEach(n=>document.querySelector(`[name=${n}]`)?.closest('tr')?.classList.toggle('mode-hidden',!rotary));
  document.querySelectorAll('#cfgGpio th[colspan]').forEach(th=>{
    const t=th.textContent||'';
    if(t.includes('Nút Nhấn Thường'))th.closest('tr')?.classList.toggle('mode-hidden',rotary);
    if(t.includes('Dạng Xoay'))th.closest('tr')?.classList.toggle('mode-hidden',!rotary);
  });
}

function validateGpioUi(){
  const isS3=String(state.status?.chip_target||'').startsWith('ESP32S3');
  const get=n=>Number(document.querySelector(`[name=${n}]`)?.value);
  const enabled=n=>!!document.querySelector(`[name=${n}]`)?.checked;
  const rotary=document.querySelector('[name=inputmode]')?.value==='rotary';
  const standalone=document.querySelector('[name=startupmode]')?.value==='standalone';
  const pins=[];
  if(enabled('leden'))pins.push(['pled','LED WS2812',get('pled'),'out']);
  if(!standalone)pins.push(['mbclk','Mic SCK',get('mbclk'),'out'],['mws','Mic WS',get('mws'),'out'],['mdin','Mic SD',get('mdin'),'in']);
  pins.push(['dbclk','DAC BCLK',get('dbclk'),'out'],['dlrc','DAC LRC',get('dlrc'),'out'],['ddout','DAC DIN',get('ddout'),'out']);
  if(enabled('btnen')){
    if(rotary)pins.push(['rclk','Rotary CLK',get('rclk'),'pullup'],['rdt','Rotary DT',get('rdt'),'pullup'],['rsw','Rotary SW',get('rsw'),'pullup']);
    else {
      const activeHigh=document.querySelector('[name=btnlevel]')?.value==='high';
      const kind=activeHigh?'pulldown':'pullup';
      pins.push(['bmic','Button Mic',get('bmic'),kind],['bup','Volume+',get('bup'),kind],['bdn','Volume-',get('bdn'),kind],['bwake','WakeUp',get('bwake'),kind]);
    }
  }
  document.querySelectorAll('#cfgGpio input[type=number]').forEach(el=>el.setCustomValidity(''));
  const errors=[];
  const validPin=p=>Number.isInteger(p)&&p>=0&&(isS3?(p<=48&&!(p>=22&&p<=37)):(p<=39&&p!==20&&p!==24&&!(p>=6&&p<=11)));
  const canOut=p=>validPin(p)&&!(isS3?p===46:p>=34&&p<=39);
  const straps=isS3?[0,3,45,46]:[0,2,5,12,15];
  const fastLed=[0,2,4,5,12,13,14,15,16,17,18,19,21,22,23,25,27,32,33,48];
  pins.forEach(([field,label,pin,kind])=>{
    let reason='';
    if(!validPin(pin))reason='không hợp lệ với chip';
    else if((kind==='out'||kind==='pullup'||kind==='pulldown')&&!canOut(pin))reason=`không hỗ trợ ${kind==='pulldown'?'INPUT_PULLDOWN':(kind==='pullup'?'INPUT_PULLUP':'output')}`;
    else if(straps.includes(pin))reason='là chân boot strap';
    else if(field==='pled'&&!fastLed.includes(pin))reason='không được FastLED hỗ trợ trong firmware';
    if(reason){errors.push(`${label} GPIO${pin}: ${reason}`);document.querySelector(`[name=${field}]`)?.setCustomValidity(`${label}: ${reason}`);}
  });
  for(let i=0;i<pins.length;i++)for(let j=i+1;j<pins.length;j++)if(pins[i][2]===pins[j][2]){
    const text=`${pins[i][1]} trùng ${pins[j][1]} tại GPIO${pins[i][2]}`;errors.push(text);
    document.querySelector(`[name=${pins[i][0]}]`)?.setCustomValidity(text);document.querySelector(`[name=${pins[j][0]}]`)?.setCustomValidity(text);
  }
  const box=document.getElementById('gpioValidation');
  if(box)box.innerHTML=errors.length?`<div class="alert alert-danger gpio-alert"><strong>Chưa thể lưu cấu hình GPIO</strong>\n${esc(errors.join('\n'))}</div>`:`<div class="alert alert-success gpio-alert"><strong>GPIO hợp lệ.</strong> Có thể lưu cấu hình an toàn.</div>`;
  return errors.length===0;
}

function bindPostForms(root=document){
  root.querySelectorAll('form[method=post]').forEach(f=>{
    if(f.dataset.bound==='1')return;
    f.dataset.bound='1';
    f.addEventListener('submit',async e=>{
      e.preventDefault();
      const action=f.getAttribute('action')||'';
      if((action==='/save'||action.endsWith('/save'))&&!validateGpioUi()){
        const gpioTab=document.querySelector('[data-bs-target="#cfgGpio"]');
        if(gpioTab){if(window.bootstrap)bootstrap.Tab.getOrCreateInstance(gpioTab).show();else gpioTab.click();}
        setTimeout(()=>f.reportValidity(),150);
        return;
      }
      if(!f.reportValidity())return;
      const isWifiConnect=action.endsWith('/connect_wifi')||action==='/connect_wifi';
      const confirmText=f.dataset.confirm;
      if(confirmText&&!confirm(confirmText)){
        return;
      }
      setBusy(true,isWifiConnect?'Đang gửi cấu hình WiFi...':'Đang gửi...');
      try{
        const r=await fetch(actionUrl(action),{method:'POST',body:new FormData(f)});
        const text=await r.text();
        let display=text;
        try{const data=JSON.parse(text);display=data.message||JSON.stringify(data,null,2);}catch(_){}
        if(!r.ok)throw new Error(display||('HTTP '+r.status));
        setBusy(false,`<pre>${esc(display)}</pre>`);
        showModal(display);
        if(action.includes('littlefs_'))setTimeout(renderLittleFs,300);
      }catch(err){
        const errText=String(err).replace(/^Error:\s*/,'');
        const t=isWifiConnect&&errText.includes('Không thể tải dữ liệu')?wifiReconnectMessage():errText;
        setBusy(false,`<pre>${esc(t)}</pre>`);
        showModal(t);
      }
    });
  });
}

function bindForms(){
  document.querySelectorAll('[data-bs-toggle="tab"]').forEach(tab=>tab.addEventListener('click',()=>{
    document.querySelectorAll('[data-bs-toggle="tab"]').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.tab-pane').forEach(x=>x.classList.remove('show','active'));
    tab.classList.add('active');
    document.querySelector(tab.dataset.bsTarget)?.classList.add('show','active');
  }));
  document.querySelectorAll('input[type=color][data-sync]').forEach(p=>{const t=document.querySelector(`input[name="${p.dataset.sync}"]`);if(t){p.addEventListener('input',()=>t.value=p.value.toUpperCase());t.addEventListener('input',()=>{if(/^#[0-9a-fA-F]{6}$/.test(t.value))p.value=t.value;});}});
  bindPostForms();
  const ttsInput=document.getElementById('ttsTextInput');
  const ttsCount=document.getElementById('ttsByteCount');
  if(ttsInput&&ttsCount){
    const updateTtsCount=()=>{const bytes=new TextEncoder().encode(ttsInput.value).length;const invalid=bytes>240;ttsCount.textContent=`${bytes} / 240 byte UTF-8`;ttsCount.classList.toggle('text-danger',invalid);ttsCount.classList.toggle('text-muted',!invalid);ttsInput.setCustomValidity(invalid?'Nội dung TTS vượt quá 240 byte UTF-8':'');};
    ttsInput.addEventListener('input',updateTtsCount);updateTtsCount();
  }
  const gpioFields=['startupmode','leden','btnen','inputmode','btnlevel','pled','mbclk','mws','mdin','dbclk','dlrc','ddout','bmic','bup','bdn','bwake','rclk','rdt','rsw'];
  gpioFields.forEach(n=>{const el=document.querySelector(`[name=${n}]`);if(el){el.addEventListener('input',()=>{updateInputModeUi();validateGpioUi();});el.addEventListener('change',()=>{updateInputModeUi();validateGpioUi();});}});
  updateInputModeUi();validateGpioUi();
  const hwSelect=document.querySelector('[name=hotword_mode]');
  if(hwSelect)hwSelect.addEventListener('change',updateHotwordModeUi);
  updateHotwordModeUi();
  document.getElementById('checkUpdateBtn').addEventListener('click',()=>checkUpdate(false));
  document.getElementById('scanWifiBtn').addEventListener('click',scanWifi);
}

function updateHtml(r){let h='<div class="list-group mb-3">';h+=`<div class="list-group-item"><b>Thiết bị:</b> ${esc(r.chip_target)}</div>`;h+=`<div class="list-group-item"><b>Phiên bản hiện tại:</b> ${esc(r.current_build_date)}</div>`;if(r.remote_build_date)h+=`<div class="list-group-item"><b>Phiên bản trên GitHub:</b> ${esc(r.remote_build_date)}</div>`;h+=`<div class="list-group-item"><b>Trạng thái:</b> <span class="${r.success?(r.has_new_version?'text-danger':'text-success'):'text-danger'}">${esc(r.message||'')}</span></div>`;if(r.description)h+=`<div class="list-group-item"><b>Mô tả:</b> ${esc(r.description)}</div>`;if(r.error)h+=`<div class="list-group-item"><b>Lỗi:</b> ${esc(r.error)}</div>`;h+='</div>';if(r.success&&r.has_new_version){h+=`<a class="btn btn-danger me-2 mb-2" href="${esc(r.bin_url)}" target="_blank" download>Tải xuống Firmware mới</a>`;if(r.littlefs_bin_url)h+=`<a class="btn btn-warning mb-2" href="${esc(r.littlefs_bin_url)}" target="_blank" download>Tải xuống LittleFS mới</a>`;}return h;}
async function fetchUpdateJson(url){const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),10000);try{const separator=url.includes('?')?'&':'?';const response=await fetch(url+separator+'_t='+Date.now(),{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error('HTTP '+response.status);const data=await response.json();if(!data||!String(data.build_date||'').trim())throw new Error('Version.json thiếu build_date');return data;}finally{clearTimeout(timer);}}
async function checkUpdate(auto=false){if(!auto)setBusy(true,'Đang kiểm tra cập nhật bằng trình duyệt...');try{const update=state.update||{};const urls=[update.version_url,update.version_fallback_url].filter((url,index,list)=>url&&list.indexOf(url)===index);if(!urls.length)throw new Error('Thiếu URL Version.json trong trạng thái WebUI');let remote=null;let lastError=null;for(const url of urls){try{remote=await fetchUpdateJson(url);break;}catch(err){lastError=err;}}if(!remote)throw lastError||new Error('Không tải được Version.json');const current=String(state.status.version||'');const latest=String(remote.build_date||'').trim();const data={success:true,chip_target:state.status.chip_target,current_build_date:current,remote_build_date:latest,has_new_version:latest!==current,message:latest!==current?'Có phiên bản mới':'Thiết bị đang dùng phiên bản mới nhất',description:String(remote.description||''),bin_url:update.bin_url||'',littlefs_bin_url:update.littlefs_bin_url||''};const h=updateHtml(data);if(!auto){setBusy(false,h);showModalHtml(h);}}catch(err){const result={success:false,chip_target:state.status.chip_target,current_build_date:state.status.version,message:'Không kiểm tra được phiên bản mới',error:String(err)};const h=updateHtml(result);if(!auto){setBusy(false,h);showModalHtml(h);}}}
async function scanWifi(){const box=document.getElementById('wifiScanResult');box.innerHTML='<span class="text-primary">Đang tìm kiếm mạng Wifi...</span>';try{const r=await fetch('/scan_wifi',{cache:'no-store'});const data=await r.json();if(data.scanning){setTimeout(scanWifi,1200);return;}if(!data.success)throw new Error(data.message||'Tìm kiếm mạng wifi thất bại');let h='<div class="list-group">';data.networks.forEach(n=>{h+=`<button type="button" class="list-group-item list-group-item-action d-flex justify-content-between align-items-center" data-ssid="${esc(n.ssid)}"><span>${esc(n.ssid||'(hidden)')}</span><small>${n.rssi} dBm ${esc(n.encryption||'')}</small></button>`;});h+='</div>';box.innerHTML=h;box.querySelectorAll('button[data-ssid]').forEach(b=>b.addEventListener('click',()=>{document.getElementById('wifiSsid').value=b.dataset.ssid;}));}catch(err){box.innerHTML=`<span class="text-danger">${esc(String(err))}</span>`;}}

async function boot(){
  // Chan double-init khi script bi nap lai/chen trung: moi lan boot tao mot JSON
  // trang thai kha lon tren heap ESP32 va mot ket noi TCP rieng.
  if(bootInProgress)return;
  bootInProgress=true;
  const controller=new AbortController();
  const timer=setTimeout(()=>controller.abort(),8000);
  try{
    let r=await fetch('/api/webui_state',{cache:'no-store',signal:controller.signal});
    if(r.status===404) r=await fetch('/webui_state',{cache:'no-store',signal:controller.signal});
    const text=await r.text();
    if(!r.ok){let detail='HTTP '+r.status;try{detail=JSON.parse(text).message||detail;}catch(_){}throw new Error(detail);}
    try{state=JSON.parse(text);}catch(e){throw new Error('Phản hồi trạng thái WebUI không phải JSON hợp lệ hoặc bị cắt giữa chừng ('+text.length+' ký tự). Vui lòng tải lại trang.');}
    if(!state||!state.success||!state.config||!state.config.server||!state.status)throw new Error('API WebUI trả về dữ liệu trạng thái không đầy đủ. Vui lòng tải lại trang.');
    loadServerAssets(state.config.server.host);
    renderStatus();renderConfig();renderActions();bindForms();
    document.getElementById('actionsMount').insertAdjacentHTML('afterend','<div id="littlefsMount"></div>');
    renderLittleFsPlaceholder();
    setBusy(false,'Giao diện cấu hình WebUI đã sẵn sàng');
    // Không tự mở TLS khi vừa tải WebUI. ESP32 không PSRAM có thể không còn
    // khối heap liên tục đủ lớn nếu WebSocket, MQTT và phản hồi WebUI cùng chạy.
    // Người dùng vẫn có thể kiểm tra bằng nút "Kiểm tra cập nhật".
  }catch(err){
    setBusy(false,`<span class="text-danger">Không tải được trạng thái dữ liệu WebUI: ${esc(err.message||err)}</span> <button type="button" id="retryBootBtn" class="btn btn-outline-primary btn-sm">Thử lại</button>`);
    document.getElementById('retryBootBtn').addEventListener('click',()=>boot(),{once:true});
  }finally{
    clearTimeout(timer);
    bootInProgress=false;
  }
}
window.uploadCustomHotword = async function(e) {
  if (e) e.preventDefault();
  const fileInput = document.getElementById('hwFileInput');
  const nameInput = document.getElementById('hwNameInput');
  const btn = document.getElementById('hwUploadBtn');
  if (!fileInput || !fileInput.files.length) {
    alert("Vui lòng chọn một tệp mô hình .bin!");
    return false;
  }
  const file = fileInput.files[0];
  if (!file.name.toLowerCase().endsWith('.bin')) {
    alert("Chỉ chấp nhận tệp định dạng .bin đóng gói của WakeNet!");
    return false;
  }

  const fd = new FormData();
  fd.append('file', file, file.name);
  if (nameInput && nameInput.value.trim().length > 0) {
    fd.append('name', nameInput.value.trim());
  }

  if (btn) {
    btn.disabled = true;
    btn.innerHTML = '<span class="spinner-border spinner-border-sm me-1"></span> Đang nạp...';
  }

  const uploadUrl = (nameInput && nameInput.value.trim().length > 0)
    ? `/upload_hotword?name=${encodeURIComponent(nameInput.value.trim())}`
    : '/upload_hotword';

  try {
    const res = await fetch(uploadUrl, {
      method: 'POST',
      body: fd
    });
    const data = await res.json();
    if (res.ok && data.success) {
      alert("Nạp hotword tùy chỉnh thành công! Trang sẽ được làm mới.");
      location.reload();
    } else {
      alert("Lỗi nạp hotword: " + (data.message || "Không xác định"));
      if (btn) {
        btn.disabled = false;
        btn.innerHTML = '<i class="bi bi-upload"></i> Nạp Hotword';
      }
    }
  } catch (err) {
    alert("Lỗi kết nối khi tải lên: " + err.message);
    if (btn) {
      btn.disabled = false;
      btn.innerHTML = '<i class="bi bi-upload"></i> Nạp Hotword';
    }
  }
  return false;
};

window.deleteCustomHotword = async function(slot, name) {
  if (!confirm(`Bạn có chắc chắn muốn xóa hotword tùy chỉnh "${name}" không?\n(Lưu ý: Chỉ hotword do người dùng tải lên mới có thể xóa)`)) {
    return;
  }
  try {
    const params = new URLSearchParams();
    params.append('slot', slot);
    const res = await fetch('/delete_hotword', {
      method: 'POST',
      body: params
    });
    const data = await res.json();
    if (res.ok && data.success) {
      alert("Đã xóa hotword tùy chỉnh thành công!");
      location.reload();
    } else {
      alert("Lỗi khi xóa: " + (data.message || "Không xác định"));
    }
  } catch (err) {
    alert("Lỗi kết nối: " + err.message);
  }
};

boot();
