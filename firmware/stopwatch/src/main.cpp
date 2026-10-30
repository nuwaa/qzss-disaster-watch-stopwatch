#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "secrets.h"

namespace {
WebServer server(8080);
struct Entry {
  String id;
  String type;
  String badge;
  String text;
  String receivedAt;
};
Entry history[8];
int historySize = 0;
int selected = 0;
uint32_t lastTransferMs = 0;
uint32_t lastDrawMs = 0;
uint32_t lastWifiTryMs = 0;
bool dirty = true;
bool serverStarted = false;
String serialLine;
bool serialOverflow = false;
bool lastTransferUsb = false;

String titleFor(const String& type) {
  if (type == "qzss.dcx.null") return "空メッセージ";
  if (type.indexOf("ash_fall") >= 0) return "降灰情報";
  if (type.indexOf("typhoon") >= 0) return "台風情報";
  if (type.indexOf("marine") >= 0) return "海上警報";
  if (type.indexOf("earthquake") >= 0) return "地震情報";
  if (type.indexOf("tsunami") >= 0) return "津波情報";
  return "災危通報";
}

void drawCenteredBody(const String& text, int centerX, int topY, int maxWidth) {
  constexpr int maxLines = 6;
  String lines[maxLines];
  int line = 0;
  bool truncated = false;
  for (int i = 0; i < text.length();) {
    const uint8_t first = static_cast<uint8_t>(text[i]);
    if (first == '\r') { ++i; continue; }
    if (first == '\n') {
      ++i;
      if (++line >= maxLines) { truncated = i < text.length(); break; }
      continue;
    }
    int bytes = first < 0x80 ? 1 : first < 0xE0 ? 2 : first < 0xF0 ? 3 : 4;
    if (i + bytes > text.length()) bytes = 1;
    const String character = text.substring(i, i + bytes);
    i += bytes;
    if (lines[line].length() && M5.Display.textWidth(lines[line] + character) > maxWidth) {
      if (++line >= maxLines) { truncated = true; break; }
    }
    lines[line] += character;
  }
  if (truncated) {
    while (lines[maxLines - 1].length() &&
           M5.Display.textWidth(lines[maxLines - 1] + "...") > maxWidth) {
      int cut = lines[maxLines - 1].length() - 1;
      while (cut > 0 && (static_cast<uint8_t>(lines[maxLines - 1][cut]) & 0xC0) == 0x80) --cut;
      lines[maxLines - 1].remove(cut);
    }
    lines[maxLines - 1] += "...";
  }
  for (int row = 0; row <= line && row < maxLines; ++row) {
    M5.Display.drawCenterString(lines[row], centerX, topY + row * 21);
  }
}

void draw() {
  const int w = M5.Display.width();
  const int h = M5.Display.height();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.drawCenterString("QZSS 災危ウォッチ", w / 2, h / 10);
  M5.Display.drawCenterString("研究・実験用", w / 2, h / 10 + 24);

  String status;
  if (lastTransferMs != 0 && millis() - lastTransferMs <= 90000)
    status = lastTransferUsb ? "USB-C 転送あり" : "Wi-Fi 転送あり";
  else if (WiFi.status() != WL_CONNECTED) status = "Wi-Fi 切断 / USB待機";
  else status = "GNSS転送なし";
  M5.Display.drawCenterString(status, w / 2, h / 5);
  if (WiFi.status() == WL_CONNECTED) {
    M5.Display.drawCenterString(WiFi.localIP().toString(), w / 2, h / 5 + 25);
  }

  if (historySize == 0) {
    M5.Display.drawCenterString("通報を待っています", w / 2, h / 2);
  } else {
    const Entry& item = history[selected];
    M5.Display.drawCenterString(item.badge, w / 2, h / 4 + 18);
    M5.Display.drawCenterString(titleFor(item.type), w / 2, h / 4 + 48);
    drawCenteredBody(item.text, w / 2, h / 2 - 8, w * 3 / 4);
    M5.Display.drawCenterString(item.receivedAt.substring(0, 19) + " UTC", w / 2, h - h / 5);
    M5.Display.drawCenterString(String(selected + 1) + "/" + String(historySize) + " A:前 B:次", w / 2, h - h / 8);
  }
  dirty = false;
}

const char* storeReport(const String& body, bool usb) {
  if (body.length() > 4096) return "too_large";
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "invalid_json";
  JsonObjectConst root = doc.as<JsonObjectConst>();
  if (root["schema"].as<int>() != 1 || !root["id"].is<const char*>() ||
      !root["type"].is<const char*>() || !root["text"].is<const char*>()) return "invalid_json";
  const String source = root["source"].as<String>();
  const bool test = root["is_test"].as<bool>();
  const String type = root["type"].as<String>();
  String badge = root["badge"].as<String>();
  // Compute badge again on the device; never trust a sender supplied badge alone.
  if (source == "sample") badge = test ? "デモ・訓練" : "デモ";
  else if (source == "replay") badge = test ? "再生・訓練" : "再生";
  else if (test) badge = "訓練・試験";
  else if (type == "qzss.dcx.null") badge = "空メッセージ";
  else if (source == "live") badge = "衛星受信";
  else badge = "出所不明";

  const String id = root["id"].as<String>();
  lastTransferMs = millis();
  lastTransferUsb = usb;
  dirty = true;
  for (int i = 0; i < historySize; ++i) {
    if (history[i].id == id && history[i].badge == badge) {
      return "duplicate";
    }
  }
  for (int i = 7; i > 0; --i) history[i] = history[i - 1];
  history[0] = {id, type, badge, root["text"].as<String>(), root["received_at"].as<String>()};
  if (historySize < 8) ++historySize;
  selected = 0;
  dirty = true;
  // No vibration or sound in the prototype. Alert freshness is not verified here.
  return "stored";
}

void acceptReport() {
  if (server.header("X-Watch-Token") != WATCH_TOKEN) {
    server.send(403, "text/plain", "forbidden");
    return;
  }
  const char* result = storeReport(server.arg("plain"), false);
  server.send(strcmp(result, "too_large") == 0 ? 413 :
              strcmp(result, "invalid_json") == 0 ? 400 : 200,
              "text/plain", result);
}

void handleSerial() {
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n') {
      if (serialOverflow) Serial.println("WATCH_ACK too_large");
      else if (serialLine.startsWith("WATCH_REPORT ")) {
        const int tokenEnd = serialLine.indexOf(' ', 13);
        if (tokenEnd < 0 || strlen(WATCH_TOKEN) == 0 || serialLine.substring(13, tokenEnd) != WATCH_TOKEN)
          Serial.println("WATCH_ACK forbidden");
        else
          Serial.printf("WATCH_ACK %s\n", storeReport(serialLine.substring(tokenEnd + 1), true));
      }
      serialLine = "";
      serialOverflow = false;
    } else if (c != '\r' && !serialOverflow) {
      if (serialLine.length() < 4300) serialLine += c;
      else { serialLine = ""; serialOverflow = true; }
    }
  }
}
}  // namespace

void setup() {
  // HWCDC defaults to only 256 RX bytes; a UTF-8 report is about 500 bytes.
  // Allocate before begin(), while the receive queue has not been created.
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.printf("WATCH_BOOT board=%d display=%dx%d\n", static_cast<int>(M5.getBoard()),
                M5.Display.width(), M5.Display.height());
  const char* headers[] = {"X-Watch-Token"};
  server.collectHeaders(headers, 1);
  server.on("/report", HTTP_POST, acceptReport);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WATCH_WIFI_SSID, WATCH_WIFI_PASSWORD);
  draw();
}

void loop() {
  M5.update();
  handleSerial();
  if (WiFi.status() == WL_CONNECTED && !serverStarted) {
    server.begin();
    serverStarted = true;
    Serial.println("WATCH_SERVER_READY");
  }
  if (serverStarted) server.handleClient();
  if (M5.BtnA.wasPressed() && historySize > 0) { selected = (selected + historySize - 1) % historySize; dirty = true; }
  if (M5.BtnB.wasPressed() && historySize > 0) { selected = (selected + 1) % historySize; dirty = true; }
  if (WiFi.status() != WL_CONNECTED && millis() - lastWifiTryMs > 10000) {
    WiFi.reconnect();
    lastWifiTryMs = millis();
  }
  if (dirty || millis() - lastDrawMs > 5000) {
    draw();
    lastDrawMs = millis();
    Serial.printf("WATCH_STATUS wifi=%d display=%dx%d\n", static_cast<int>(WiFi.status()),
                  M5.Display.width(), M5.Display.height());
    if (WiFi.status() == WL_CONNECTED) Serial.println(WiFi.localIP());
  }
  delay(5);
}
