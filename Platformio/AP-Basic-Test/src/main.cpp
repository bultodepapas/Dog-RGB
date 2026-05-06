#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

namespace {
constexpr const char *AP_SSID = "DogRGB-APTEST";
constexpr const char *AP_PASS = "Dog12345";
constexpr uint8_t AP_CHANNEL = 1;
constexpr uint8_t AP_MAX_CLIENTS = 1;
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);

WebServer server(80);
unsigned long last_log_ms = 0;
uint32_t ap_connect_count = 0;
uint32_t ap_disconnect_count = 0;
uint32_t http_request_count = 0;
bool ap_started = false;

const char *reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT_PIN";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

void note_http() {
  http_request_count++;
}

String status_json() {
  String out = "{";
  out += "\"ap_started\":" + String(ap_started ? "true" : "false");
  out += ",\"ssid\":\"" + String(AP_SSID) + "\"";
  out += ",\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
  out += ",\"mac\":\"" + WiFi.softAPmacAddress() + "\"";
  out += ",\"channel\":" + String(AP_CHANNEL);
  out += ",\"clients\":" + String(WiFi.softAPgetStationNum());
  out += ",\"connect_count\":" + String(ap_connect_count);
  out += ",\"disconnect_count\":" + String(ap_disconnect_count);
  out += ",\"http_requests\":" + String(http_request_count);
  out += ",\"uptime_ms\":" + String(millis());
  out += ",\"free_heap\":" + String(ESP.getFreeHeap());
  out += "}";
  return out;
}

void handle_root() {
  note_http();
  const String html =
      "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>DogRGB AP Test</title></head><body>"
      "<h1>DogRGB AP Test</h1>"
      "<p>SoftAP is running without GPS, LEDs, BLE, NVS, DNS, or production portal code.</p>"
      "<ul>"
      "<li>SSID: " + String(AP_SSID) + "</li>"
      "<li>IP: " + WiFi.softAPIP().toString() + "</li>"
      "<li>MAC: " + WiFi.softAPmacAddress() + "</li>"
      "<li>Clients: " + String(WiFi.softAPgetStationNum()) + "</li>"
      "</ul>"
      "<p><a href='/status'>JSON status</a></p>"
      "</body></html>";
  server.send(200, "text/html", html);
}

void handle_status() {
  note_http();
  server.send(200, "application/json", status_json());
}

void handle_not_found() {
  note_http();
  server.send(404, "text/plain", "DogRGB AP Test: not found. Open http://192.168.4.1/");
}

void on_wifi_event(WiFiEvent_t event) {
#if defined(ARDUINO_EVENT_WIFI_AP_STACONNECTED)
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
#elif defined(SYSTEM_EVENT_AP_STACONNECTED)
  if (event == SYSTEM_EVENT_AP_STACONNECTED) {
#else
  if (false) {
#endif
    ap_connect_count++;
    Serial.print("[AP_EVT] client_connected count=");
    Serial.println(WiFi.softAPgetStationNum());
    return;
  }

#if defined(ARDUINO_EVENT_WIFI_AP_STADISCONNECTED)
  if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
#elif defined(SYSTEM_EVENT_AP_STADISCONNECTED)
  if (event == SYSTEM_EVENT_AP_STADISCONNECTED) {
#else
  if (false) {
#endif
    ap_disconnect_count++;
    Serial.print("[AP_EVT] client_disconnected count=");
    Serial.println(WiFi.softAPgetStationNum());
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);

  const esp_reset_reason_t rr = esp_reset_reason();
  Serial.println();
  Serial.println("DogRGB AP Basic Test");
  Serial.print("[BOOT] reset_reason=");
  Serial.print(reset_reason_name(rr));
  Serial.print(" (");
  Serial.print(static_cast<int>(rr));
  Serial.println(")");

  WiFi.onEvent(on_wifi_event);
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  delay(200);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
  ap_started = WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, false, AP_MAX_CLIENTS);

  Serial.print("[AP] start=");
  Serial.print(ap_started ? "OK" : "FAIL");
  Serial.print(" ssid=");
  Serial.print(AP_SSID);
  Serial.print(" pass=");
  Serial.print(AP_PASS);
  Serial.print(" ip=");
  Serial.print(WiFi.softAPIP());
  Serial.print(" mac=");
  Serial.print(WiFi.softAPmacAddress());
  Serial.print(" channel=");
  Serial.println(AP_CHANNEL);

  server.on("/", HTTP_GET, handle_root);
  server.on("/status", HTTP_GET, handle_status);
  server.onNotFound(handle_not_found);
  server.begin();
  Serial.println("[HTTP] ready http://192.168.4.1/");
}

void loop() {
  server.handleClient();

  const unsigned long now_ms = millis();
  if (now_ms - last_log_ms >= 2000) {
    last_log_ms = now_ms;
    Serial.print("[AP_STATUS] uptime_s=");
    Serial.print(now_ms / 1000);
    Serial.print(" started=");
    Serial.print(ap_started ? "1" : "0");
    Serial.print(" clients=");
    Serial.print(WiFi.softAPgetStationNum());
    Serial.print(" http=");
    Serial.print(http_request_count);
    Serial.print(" heap=");
    Serial.println(ESP.getFreeHeap());
  }
}
