//Code By: Vũ Tuyển
//Designed by: BootstrapMade
//GitHub VBot: https://github.com/marion001/VBot_Offline.git
//Facebook Group: https://www.facebook.com/groups/1148385343358824
//Facebook: https://www.facebook.com/TWFyaW9uMDAx
//Email: VBot.Assistant@gmail.com

#include "vbot_dlna.h"
#include <WiFi.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static bool dlnaRunning = false;
static DlnaCallbacks dlnaCallbacks;
static String dlnaFriendlyName = "VBot Smart Speaker";
static String dlnaUuid = "28383838-0000-0000-0000-000000000000";
static String dlnaCurrentState = "STOPPED";
static String dlnaCurrentUri = "";
static String dlnaCurrentTitle = "";
static int dlnaCurrentVolume = 60;
static TaskHandle_t ssdpTaskHandle = nullptr;

// XML mô tả dịch vụ AVTransport (SCPD)
static const char AVTRANSPORT_SCPD[] PROGMEM = 
"<?xml version=\"1.0\"?>\r\n"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
"  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
"  <actionList>\r\n"
"    <action><name>SetAVTransportURI</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentURI</name><direction>in</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentURIMetaData</name><direction>in</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>Play</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>Speed</name><direction>in</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>Pause</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>Stop</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetTransportInfo</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentTransportState</name><direction>out</direction><relatedStateVariable>TransportState</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentTransportStatus</name><direction>out</direction><relatedStateVariable>TransportStatus</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentSpeed</name><direction>out</direction><relatedStateVariable>TransportPlaySpeed</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetPositionInfo</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>Track</name><direction>out</direction><relatedStateVariable>CurrentTrack</relatedStateVariable></argument>\r\n"
"        <argument><name>TrackDuration</name><direction>out</direction><relatedStateVariable>CurrentTrackDuration</relatedStateVariable></argument>\r\n"
"        <argument><name>TrackMetaData</name><direction>out</direction><relatedStateVariable>CurrentTrackMetaData</relatedStateVariable></argument>\r\n"
"        <argument><name>TrackURI</name><direction>out</direction><relatedStateVariable>CurrentTrackURI</relatedStateVariable></argument>\r\n"
"        <argument><name>RelTime</name><direction>out</direction><relatedStateVariable>RelativeTimePosition</relatedStateVariable></argument>\r\n"
"        <argument><name>AbsTime</name><direction>out</direction><relatedStateVariable>AbsoluteTimePosition</relatedStateVariable></argument>\r\n"
"        <argument><name>RelCount</name><direction>out</direction><relatedStateVariable>RelativeCounterPosition</relatedStateVariable></argument>\r\n"
"        <argument><name>AbsCount</name><direction>out</direction><relatedStateVariable>AbsoluteCounterPosition</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetDeviceCapabilities</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>PlayMedia</name><direction>out</direction><relatedStateVariable>PossiblePlaybackStorageMedia</relatedStateVariable></argument>\r\n"
"        <argument><name>RecMedia</name><direction>out</direction><relatedStateVariable>PossibleRecordStorageMedia</relatedStateVariable></argument>\r\n"
"        <argument><name>RecQualityModes</name><direction>out</direction><relatedStateVariable>PossibleRecordQualityModes</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetMediaInfo</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>NrTracks</name><direction>out</direction><relatedStateVariable>NumberOfTracks</relatedStateVariable></argument>\r\n"
"        <argument><name>MediaDuration</name><direction>out</direction><relatedStateVariable>CurrentMediaDuration</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentURI</name><direction>out</direction><relatedStateVariable>AVTransportURI</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentURIMetaData</name><direction>out</direction><relatedStateVariable>AVTransportURIMetaData</relatedStateVariable></argument>\r\n"
"        <argument><name>NextURI</name><direction>out</direction><relatedStateVariable>NextAVTransportURI</relatedStateVariable></argument>\r\n"
"        <argument><name>NextURIMetaData</name><direction>out</direction><relatedStateVariable>NextAVTransportURIMetaData</relatedStateVariable></argument>\r\n"
"        <argument><name>PlayMedium</name><direction>out</direction><relatedStateVariable>PlaybackStorageMedium</relatedStateVariable></argument>\r\n"
"        <argument><name>RecordMedium</name><direction>out</direction><relatedStateVariable>RecordStorageMedium</relatedStateVariable></argument>\r\n"
"        <argument><name>WriteStatus</name><direction>out</direction><relatedStateVariable>RecordMediumWriteStatus</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"  </actionList>\r\n"
"  <serviceStateTable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>TransportState</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>TransportStatus</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>CurrentTrack</name><dataType>ui4</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>CurrentTrackDuration</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>CurrentTrackMetaData</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>CurrentTrackURI</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>AVTransportURI</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>AVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>NextAVTransportURI</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>NextAVTransportURIMetaData</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>RelativeTimePosition</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>AbsoluteTimePosition</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>RelativeCounterPosition</name><dataType>i4</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>AbsoluteCounterPosition</name><dataType>i4</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>NumberOfTracks</name><dataType>ui4</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>CurrentMediaDuration</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>PlaybackStorageMedium</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>RecordStorageMedium</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>RecordMediumWriteStatus</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>PossiblePlaybackStorageMedia</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>PossibleRecordStorageMedia</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>PossibleRecordQualityModes</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\r\n"
"  </serviceStateTable>\r\n"
"</scpd>\r\n";

// XML mô tả dịch vụ RenderingControl (SCPD)
static const char RENDERINGCONTROL_SCPD[] PROGMEM = 
"<?xml version=\"1.0\"?>\r\n"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
"  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
"  <actionList>\r\n"
"    <action><name>SetVolume</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
"        <argument><name>DesiredVolume</name><direction>in</direction><relatedStateVariable>Volume</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetVolume</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentVolume</name><direction>out</direction><relatedStateVariable>Volume</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>SetMute</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
"        <argument><name>DesiredMute</name><direction>in</direction><relatedStateVariable>Mute</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetMute</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>InstanceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable></argument>\r\n"
"        <argument><name>Channel</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable></argument>\r\n"
"        <argument><name>CurrentMute</name><direction>out</direction><relatedStateVariable>Mute</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"  </actionList>\r\n"
"  <serviceStateTable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>Volume</name><dataType>ui2</dataType><allowedValueRange><minimum>0</minimum><maximum>100</maximum><step>1</step></allowedValueRange></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>Mute</name><dataType>boolean</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</dataType></stateVariable>\r\n"
"  </serviceStateTable>\r\n"
"</scpd>\r\n";

// XML mô tả dịch vụ ConnectionManager (SCPD)
static const char CONNECTIONMANAGER_SCPD[] PROGMEM = 
"<?xml version=\"1.0\"?>\r\n"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
"  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
"  <actionList>\r\n"
"    <action><name>GetProtocolInfo</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument>\r\n"
"        <argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetCurrentConnectionIDs</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"    <action><name>GetCurrentConnectionInfo</name>\r\n"
"      <argumentList>\r\n"
"        <argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\r\n"
"        <argument><name>RcsID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument>\r\n"
"        <argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument>\r\n"
"        <argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument>\r\n"
"        <argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument>\r\n"
"        <argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument>\r\n"
"        <argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument>\r\n"
"        <argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument>\r\n"
"      </argumentList>\r\n"
"    </action>\r\n"
"  </actionList>\r\n"
"  <serviceStateTable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable>\r\n"
"    <stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable>\r\n"
"  </serviceStateTable>\r\n"
"</scpd>\r\n";

static String extractXmlTag(const String &xml, const char *tag) {
  String startTag = String("<") + tag + ">";
  String endTag = String("</") + tag + ">";
  int startIdx = xml.indexOf(startTag);
  if (startIdx < 0) {
    // Thử trường hợp tag có namespace hoặc thuộc tính
    startTag = String("<") + tag;
    startIdx = xml.indexOf(startTag);
    if (startIdx < 0) return "";
    int closeAngle = xml.indexOf('>', startIdx);
    if (closeAngle < 0) return "";
    startIdx = closeAngle + 1;
  } else {
    startIdx += startTag.length();
  }
  int endIdx = xml.indexOf(endTag, startIdx);
  if (endIdx < 0) return "";
  String result = xml.substring(startIdx, endIdx);
  result.trim();
  // Giải mã thực thể HTML cơ bản
  result.replace("&amp;", "&");
  result.replace("&lt;", "<");
  result.replace("&gt;", ">");
  result.replace("&quot;", "\"");
  result.replace("&apos;", "'");
  return result;
}

static String buildSoapEnvelope(const String &action, const String &serviceType, const String &body) {
  String out;
  out.reserve(256 + body.length());
  out += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
  out += "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">\r\n";
  out += "  <s:Body>\r\n";
  out += "    <u:" + action + "Response xmlns:u=\"" + serviceType + "\">\r\n";
  out += body;
  out += "    </u:" + action + "Response>\r\n";
  out += "  </s:Body>\r\n";
  out += "</s:Envelope>\r\n";
  return out;
}

static void sendSoapResponse(AsyncWebServerRequest *request, const String &action, const String &serviceType, const String &body = "") {
  String responseXml = buildSoapEnvelope(action, serviceType, body);
  AsyncWebServerResponse *res = request->beginResponse(200, "text/xml; charset=\"utf-8\"", responseXml);
  res->addHeader("EXT", "");
  request->send(res);
}

// Sinh XML mô tả thiết bị MediaRenderer (description.xml)
static String generateDeviceDescriptionXml() {
  IPAddress ip = WiFi.localIP();
  String ipStr = ip.toString();

  String out;
  out.reserve(2048);
  out += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
  out += "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n";
  out += "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n";
  out += "  <device>\r\n";
  out += "    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\r\n";
  out += "    <friendlyName>" + dlnaFriendlyName + "</friendlyName>\r\n";
  out += "    <manufacturer>VBot Assistant</manufacturer>\r\n";
  out += "    <manufacturerURL>https://github.com/marion001/VBot_Offline</manufacturerURL>\r\n";
  out += "    <modelDescription>VBot Smart Speaker DLNA Audio Renderer</modelDescription>\r\n";
  out += "    <modelName>VBot ESP32-S3</modelName>\r\n";
  out += "    <modelNumber>1.0</modelNumber>\r\n";
  out += "    <modelURL>https://github.com/marion001/VBot_Offline</modelURL>\r\n";
  out += "    <UDN>uuid:" + dlnaUuid + "</UDN>\r\n";
  out += "    <serviceList>\r\n";
  out += "      <service>\r\n";
  out += "        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>\r\n";
  out += "        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>\r\n";
  out += "        <SCPDURL>/dlna/avtransport.xml</SCPDURL>\r\n";
  out += "        <controlURL>/dlna/control/avtransport</controlURL>\r\n";
  out += "        <eventSubURL>/dlna/event/avtransport</eventSubURL>\r\n";
  out += "      </service>\r\n";
  out += "      <service>\r\n";
  out += "        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>\r\n";
  out += "        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>\r\n";
  out += "        <SCPDURL>/dlna/renderingcontrol.xml</SCPDURL>\r\n";
  out += "        <controlURL>/dlna/control/renderingcontrol</controlURL>\r\n";
  out += "        <eventSubURL>/dlna/event/renderingcontrol</eventSubURL>\r\n";
  out += "      </service>\r\n";
  out += "      <service>\r\n";
  out += "        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>\r\n";
  out += "        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>\r\n";
  out += "        <SCPDURL>/dlna/connectionmanager.xml</SCPDURL>\r\n";
  out += "        <controlURL>/dlna/control/connectionmanager</controlURL>\r\n";
  out += "        <eventSubURL>/dlna/event/connectionmanager</eventSubURL>\r\n";
  out += "      </service>\r\n";
  out += "    </serviceList>\r\n";
  out += "  </device>\r\n";
  out += "</root>\r\n";
  return out;
}

// FreeRTOS Task lắng nghe và phản hồi gói tin SSDP (UDP 1900 multicast)
static void vbot_ssdp_task(void *param) {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    Serial.println("[DLNA] Không thể tạo SSDP socket.");
    vTaskDelete(NULL);
    return;
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_in bindAddr;
  memset(&bindAddr, 0, sizeof(bindAddr));
  bindAddr.sin_family = AF_INET;
  bindAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  bindAddr.sin_port = htons(1900);

  if (bind(sock, (struct sockaddr*)&bindAddr, sizeof(bindAddr)) < 0) {
    Serial.println("[DLNA] Lỗi bind SSDP port 1900.");
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  // Gia nhập nhóm Multicast SSDP 239.255.255.250
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
  mreq.imr_interface.s_addr = (uint32_t)WiFi.localIP();
  if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
  }

  Serial.println("[DLNA] Máy chủ SSDP Discovery đã sẵn sàng trên cổng 1900.");

  auto sendSsdpNotify = [&](const char *nt) {
    IPAddress localIp = WiFi.localIP();
    String notify;
    notify.reserve(512);
    notify += "NOTIFY * HTTP/1.1\r\n";
    notify += "HOST: 239.255.255.250:1900\r\n";
    notify += "CACHE-CONTROL: max-age=1800\r\n";
    notify += "LOCATION: http://" + localIp.toString() + ":80/dlna/desc.xml\r\n";
    notify += "NT: " + String(nt) + "\r\n";
    notify += "NTS: ssdp:alive\r\n";
    notify += "SERVER: ESP32/1.0 UPnP/1.0 VBot/1.0\r\n";
    notify += "USN: uuid:" + dlnaUuid;
    if (strcmp(nt, "upnp:rootdevice") != 0) {
      notify += "::" + String(nt);
    }
    notify += "\r\nBOOTID.UPNP.ORG: 1\r\nCONFIGID.UPNP.ORG: 1\r\n\r\n";

    struct sockaddr_in destAddr;
    memset(&destAddr, 0, sizeof(destAddr));
    destAddr.sin_family = AF_INET;
    destAddr.sin_addr.s_addr = inet_addr("239.255.255.250");
    destAddr.sin_port = htons(1900);
    sendto(sock, notify.c_str(), notify.length(), 0, (struct sockaddr*)&destAddr, sizeof(destAddr));
  };

  auto sendSsdpResponse = [&](const struct sockaddr_in &remoteAddr, const char *st) {
    IPAddress localIp = WiFi.localIP();
    String resp;
    resp.reserve(512);
    resp += "HTTP/1.1 200 OK\r\n";
    resp += "CACHE-CONTROL: max-age=1800\r\n";
    resp += "EXT:\r\n";
    resp += "LOCATION: http://" + localIp.toString() + ":80/dlna/desc.xml\r\n";
    resp += "SERVER: ESP32/1.0 UPnP/1.0 VBot/1.0\r\n";
    resp += "ST: " + String(st) + "\r\n";
    resp += "USN: uuid:" + dlnaUuid;
    if (strcmp(st, "upnp:rootdevice") != 0 && strcmp(st, "ssdp:all") != 0) {
      resp += "::" + String(st);
    }
    resp += "\r\nBOOTID.UPNP.ORG: 1\r\nCONFIGID.UPNP.ORG: 1\r\n\r\n";

    sendto(sock, resp.c_str(), resp.length(), 0, (struct sockaddr*)&remoteAddr, sizeof(remoteAddr));
  };

  // Broadcast NOTIFY ngay khi khởi động
  sendSsdpNotify("upnp:rootdevice");
  sendSsdpNotify("urn:schemas-upnp-org:device:MediaRenderer:1");
  sendSsdpNotify("urn:schemas-upnp-org:service:AVTransport:1");
  sendSsdpNotify("urn:schemas-upnp-org:service:RenderingControl:1");

  uint32_t lastNotifyMs = millis();
  char buffer[1024];

  while (dlnaRunning) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
    int ret = select(sock + 1, &readfds, NULL, NULL, &tv);

    if (ret > 0 && FD_ISSET(sock, &readfds)) {
      struct sockaddr_in fromAddr;
      socklen_t fromLen = sizeof(fromAddr);
      int len = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&fromAddr, &fromLen);
      if (len > 0) {
        buffer[len] = '\0';
        String msg(buffer);

        if (msg.indexOf("M-SEARCH") >= 0) {
          int stIdx = msg.indexOf("ST:");
          if (stIdx < 0) stIdx = msg.indexOf("st:");
          if (stIdx >= 0) {
            int endLine = msg.indexOf("\r\n", stIdx);
            if (endLine > 0) {
              String targetSt = msg.substring(stIdx + 3, endLine);
              targetSt.trim();

              if (targetSt == "ssdp:all" || 
                  targetSt == "upnp:rootdevice" || 
                  targetSt == "urn:schemas-upnp-org:device:MediaRenderer:1" ||
                  targetSt == "urn:schemas-upnp-org:service:AVTransport:1" ||
                  targetSt == "urn:schemas-upnp-org:service:RenderingControl:1" ||
                  targetSt == "urn:schemas-upnp-org:service:ConnectionManager:1") {
                sendSsdpResponse(fromAddr, targetSt.c_str());
              }
            }
          }
        }
      }
    }

    if (millis() - lastNotifyMs >= 20000) {
      lastNotifyMs = millis();
      sendSsdpNotify("upnp:rootdevice");
      sendSsdpNotify("urn:schemas-upnp-org:device:MediaRenderer:1");
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  close(sock);
  vTaskDelete(NULL);
}

bool vbot_dlna_init(AsyncWebServer &server, const char *friendlyName, const DlnaCallbacks &callbacks) {
  if (dlnaRunning) return true;

  dlnaCallbacks = callbacks;
  if (friendlyName && strlen(friendlyName) > 0) {
    dlnaFriendlyName = String(friendlyName) + " (VBot DLNA)";
  } else {
    dlnaFriendlyName = "VBot Smart Speaker";
  }

  // Tạo UUID ổn định từ địa chỉ MAC của ESP32
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char uuidBuf[48];
  snprintf(uuidBuf, sizeof(uuidBuf), "28383838-5642-4f54-8888-%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  dlnaUuid = String(uuidBuf);

  // 1. Route XML mô tả thiết bị
  server.on("/dlna/desc.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    String xml = generateDeviceDescriptionXml();
    request->send(200, "text/xml; charset=\"utf-8\"", xml);
  });

  server.on("/dlna/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    String xml = generateDeviceDescriptionXml();
    request->send(200, "text/xml; charset=\"utf-8\"", xml);
  });

  // 2. Route SCPD dịch vụ
  server.on("/dlna/avtransport.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml; charset=\"utf-8\"", AVTRANSPORT_SCPD);
  });

  server.on("/dlna/renderingcontrol.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml; charset=\"utf-8\"", RENDERINGCONTROL_SCPD);
  });

  server.on("/dlna/connectionmanager.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/xml; charset=\"utf-8\"", CONNECTIONMANAGER_SCPD);
  });

  // 3. Route SOAP Control AVTransport
  server.on("/dlna/control/avtransport", HTTP_POST, 
    [](AsyncWebServerRequest *request) {
      // AsyncWebServer gọi callback này sau khi body hoàn thành
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) {
        request->_tempObject = new String();
      }
      String *body = (String*)request->_tempObject;
      if (body) {
        body->concat((const char*)data, len);
      }
      if (index + len >= total && body) {
        String soapAction = "";
        if (request->hasHeader("SOAPACTION")) {
          soapAction = request->getHeader("SOAPACTION")->value();
        } else if (request->hasHeader("Soapaction")) {
          soapAction = request->getHeader("Soapaction")->value();
        }

        const char *serviceType = "urn:schemas-upnp-org:service:AVTransport:1";

        if (soapAction.indexOf("SetAVTransportURI") >= 0 || body->indexOf("SetAVTransportURI") >= 0) {
          String uri = extractXmlTag(*body, "CurrentURI");
          String meta = extractXmlTag(*body, "CurrentURIMetaData");
          String title = extractXmlTag(meta, "dc:title");
          if (title.length() == 0) {
            title = extractXmlTag(meta, "title");
          }
          if (title.length() == 0) {
            int lastSlash = uri.lastIndexOf('/');
            if (lastSlash >= 0) title = uri.substring(lastSlash + 1);
            else title = "DLNA Music Stream";
          }
          dlnaCurrentUri = uri;
          dlnaCurrentTitle = title;
          Serial.printf("[DLNA] Nhận URL bài hát: %s (Tiêu đề: %s)\n", uri.c_str(), title.c_str());
          sendSoapResponse(request, "SetAVTransportURI", serviceType);
        }
        else if (soapAction.indexOf("Play") >= 0 || body->indexOf("<Play>") >= 0 || body->indexOf("<u:Play") >= 0) {
          dlnaCurrentState = "PLAYING";
          if (dlnaCallbacks.onPlay && dlnaCurrentUri.length() > 0) {
            dlnaCallbacks.onPlay(dlnaCurrentUri.c_str(), dlnaCurrentTitle.c_str());
          }
          Serial.println("[DLNA] Nhận lệnh Play từ điện thoại/PC.");
          sendSoapResponse(request, "Play", serviceType);
        }
        else if (soapAction.indexOf("Pause") >= 0 || body->indexOf("<Pause>") >= 0 || body->indexOf("<u:Pause") >= 0) {
          dlnaCurrentState = "PAUSED_PLAYBACK";
          if (dlnaCallbacks.onPause) dlnaCallbacks.onPause();
          Serial.println("[DLNA] Nhận lệnh Pause.");
          sendSoapResponse(request, "Pause", serviceType);
        }
        else if (soapAction.indexOf("Stop") >= 0 || body->indexOf("<Stop>") >= 0 || body->indexOf("<u:Stop") >= 0) {
          dlnaCurrentState = "STOPPED";
          if (dlnaCallbacks.onStop) dlnaCallbacks.onStop();
          Serial.println("[DLNA] Nhận lệnh Stop.");
          sendSoapResponse(request, "Stop", serviceType);
        }
        else if (soapAction.indexOf("GetTransportInfo") >= 0 || body->indexOf("GetTransportInfo") >= 0) {
          String res = "      <CurrentTransportState>" + dlnaCurrentState + "</CurrentTransportState>\r\n"
                       "      <CurrentTransportStatus>OK</CurrentTransportStatus>\r\n"
                       "      <CurrentSpeed>1</CurrentSpeed>\r\n";
          sendSoapResponse(request, "GetTransportInfo", serviceType, res);
        }
        else if (soapAction.indexOf("GetPositionInfo") >= 0 || body->indexOf("GetPositionInfo") >= 0) {
          String res = "      <Track>1</Track>\r\n"
                       "      <TrackDuration>00:00:00</TrackDuration>\r\n"
                       "      <TrackMetaData></TrackMetaData>\r\n"
                       "      <TrackURI>" + dlnaCurrentUri + "</TrackURI>\r\n"
                       "      <RelTime>00:00:00</RelTime>\r\n"
                       "      <AbsTime>00:00:00</AbsTime>\r\n"
                       "      <RelCount>0</RelCount>\r\n"
                       "      <AbsCount>0</AbsCount>\r\n";
          sendSoapResponse(request, "GetPositionInfo", serviceType, res);
        }
        else if (soapAction.indexOf("GetDeviceCapabilities") >= 0 || body->indexOf("GetDeviceCapabilities") >= 0) {
          String res = "      <PlayMedia>NETWORK</PlayMedia>\r\n"
                       "      <RecMedia>NONE</RecMedia>\r\n"
                       "      <RecQualityModes>NONE</RecQualityModes>\r\n";
          sendSoapResponse(request, "GetDeviceCapabilities", serviceType, res);
        }
        else if (soapAction.indexOf("GetMediaInfo") >= 0 || body->indexOf("GetMediaInfo") >= 0) {
          String res = "      <NrTracks>1</NrTracks>\r\n"
                       "      <MediaDuration>00:00:00</MediaDuration>\r\n"
                       "      <CurrentURI>" + dlnaCurrentUri + "</CurrentURI>\r\n"
                       "      <CurrentURIMetaData></CurrentURIMetaData>\r\n"
                       "      <NextURI></NextURI>\r\n"
                       "      <NextURIMetaData></NextURIMetaData>\r\n"
                       "      <PlayMedium>NETWORK</PlayMedium>\r\n"
                       "      <RecordMedium>NONE</RecordMedium>\r\n"
                       "      <WriteStatus>NOT_WRITABLE</WriteStatus>\r\n";
          sendSoapResponse(request, "GetMediaInfo", serviceType, res);
        }
        else {
          sendSoapResponse(request, "AVTransportAction", serviceType);
        }

        delete body;
        request->_tempObject = nullptr;
      }
    }
  );

  // 4. Route SOAP Control RenderingControl
  server.on("/dlna/control/renderingcontrol", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) request->_tempObject = new String();
      String *body = (String*)request->_tempObject;
      if (body) body->concat((const char*)data, len);
      if (index + len >= total && body) {
        const char *serviceType = "urn:schemas-upnp-org:service:RenderingControl:1";
        if (body->indexOf("SetVolume") >= 0) {
          String volStr = extractXmlTag(*body, "DesiredVolume");
          if (volStr.length() > 0) {
            int vol = volStr.toInt();
            vol = constrain(vol, 0, 100);
            dlnaCurrentVolume = vol;
            if (dlnaCallbacks.onVolume) dlnaCallbacks.onVolume(vol);
            Serial.printf("[DLNA] Đổi âm lượng từ điện thoại: %d%%\n", vol);
          }
          sendSoapResponse(request, "SetVolume", serviceType);
        }
        else if (body->indexOf("GetVolume") >= 0) {
          String res = "      <CurrentVolume>" + String(dlnaCurrentVolume) + "</CurrentVolume>\r\n";
          sendSoapResponse(request, "GetVolume", serviceType, res);
        }
        else if (body->indexOf("SetMute") >= 0) {
          sendSoapResponse(request, "SetMute", serviceType);
        }
        else if (body->indexOf("GetMute") >= 0) {
          String res = "      <CurrentMute>0</CurrentMute>\r\n";
          sendSoapResponse(request, "GetMute", serviceType, res);
        }
        else {
          sendSoapResponse(request, "RenderingControlAction", serviceType);
        }
        delete body;
        request->_tempObject = nullptr;
      }
    }
  );

  // 5. Route SOAP Control ConnectionManager
  server.on("/dlna/control/connectionmanager", HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      if (index == 0) request->_tempObject = new String();
      String *body = (String*)request->_tempObject;
      if (body) body->concat((const char*)data, len);
      if (index + len >= total && body) {
        const char *serviceType = "urn:schemas-upnp-org:service:ConnectionManager:1";
        if (body->indexOf("GetProtocolInfo") >= 0) {
          String res = "      <Source></Source>\r\n"
                       "      <Sink>http-get:*:audio/mpeg:*,http-get:*:audio/mp3:*,http-get:*:audio/aac:*,http-get:*:audio/wav:*,http-get:*:audio/x-wav:*,http-get:*:audio/flac:*,http-get:*:audio/ogg:*</Sink>\r\n";
          sendSoapResponse(request, "GetProtocolInfo", serviceType, res);
        }
        else if (body->indexOf("GetCurrentConnectionIDs") >= 0) {
          String res = "      <ConnectionIDs>0</ConnectionIDs>\r\n";
          sendSoapResponse(request, "GetCurrentConnectionIDs", serviceType, res);
        }
        else if (body->indexOf("GetCurrentConnectionInfo") >= 0) {
          String res = "      <RcsID>0</RcsID>\r\n"
                       "      <AVTransportID>0</AVTransportID>\r\n"
                       "      <ProtocolInfo></ProtocolInfo>\r\n"
                       "      <PeerConnectionManager></PeerConnectionManager>\r\n"
                       "      <PeerConnectionID>-1</PeerConnectionID>\r\n"
                       "      <Direction>Input</Direction>\r\n"
                       "      <Status>OK</Status>\r\n";
          sendSoapResponse(request, "GetCurrentConnectionInfo", serviceType, res);
        }
        else {
          sendSoapResponse(request, "ConnectionManagerAction", serviceType);
        }
        delete body;
        request->_tempObject = nullptr;
      }
    }
  );

  // 6. Xử lý GENA event subscription từ điện thoại/PC
  auto handleGenaSubscribe = [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/xml", "");
    response->addHeader("SID", "uuid:28383838-5642-4f54-0000-000000000001");
    response->addHeader("TIMEOUT", "Second-1800");
    request->send(response);
  };
  server.on("/dlna/event/avtransport", HTTP_ANY, handleGenaSubscribe);
  server.on("/dlna/event/renderingcontrol", HTTP_ANY, handleGenaSubscribe);
  server.on("/dlna/event/connectionmanager", HTTP_ANY, handleGenaSubscribe);

  dlnaRunning = true;

  // Tạo FreeRTOS task cho SSDP Discovery
  xTaskCreatePinnedToCore(
    vbot_ssdp_task,
    "vbot_ssdp",
    8192,
    NULL,
    1,
    &ssdpTaskHandle,
    0 // Chạy trên Core 0 (chung với Core WiFi/Network)
  );

  Serial.printf("[DLNA] Đã kích hoạt DLNA MediaRenderer thành công: %s\n", dlnaFriendlyName.c_str());
  return true;
}

void vbot_dlna_stop() {
  if (!dlnaRunning) return;
  dlnaRunning = false;
  if (ssdpTaskHandle) {
    vTaskDelete(ssdpTaskHandle);
    ssdpTaskHandle = nullptr;
  }
  Serial.println("[DLNA] Đã dừng DLNA MediaRenderer.");
}

bool vbot_dlna_is_running() {
  return dlnaRunning;
}

void vbot_dlna_set_state(const char *state) {
  if (state) dlnaCurrentState = state;
}

void vbot_dlna_set_volume(int vol) {
  dlnaCurrentVolume = constrain(vol, 0, 100);
}

int vbot_dlna_get_volume() {
  return dlnaCurrentVolume;
}

const char* vbot_dlna_get_title() {
  return dlnaCurrentTitle.c_str();
}

const char* vbot_dlna_get_uri() {
  return dlnaCurrentUri.c_str();
}
