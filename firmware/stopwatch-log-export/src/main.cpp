#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>

namespace {
bool storageReady = false;
String command;
String status = "起動中";
uint32_t lastDraw = 0;

void drawStatus() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(1);
  const int center = M5.Display.width() / 2;
  M5.Display.drawCenterString("検証ログ書き出し", center, 100);
  M5.Display.drawCenterString("研究・実験用", center, 145);
  M5.Display.drawCenterString(status, center, 220);
  M5.Display.drawCenterString("PCから操作してください", center, 300);
  lastDraw = millis();
}

void sendFile(const char* path, int index) {
  File file = LittleFS.open(path, "r");
  if (!file) return;
  Serial.printf("LOG_BEGIN %d %u\n", index, static_cast<unsigned>(file.size()));
  while (file.available()) {
    uint8_t buffer[128];
    const size_t n = file.read(buffer, sizeof(buffer));
    if (!n) break;
    Serial.write(buffer, n);
  }
  file.close();
  Serial.flush();
  Serial.printf("LOG_END %d\n", index);
}
}

void setup() {
  Serial.begin(115200);
  M5.begin(M5.config());
  drawStatus();
  Serial.println("LOG_BOOT");
  storageReady = LittleFS.begin(false); // Never format or erase capture evidence.
  status = storageReady ? "記録領域OK" : "記録領域未準備";
  drawStatus();
  Serial.println(storageReady ? "LOG_READY mounted" : "LOG_READY unmounted");
}

void loop() {
  M5.update();
  while (Serial.available()) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      if (command == "DUMP") {
        status = "ログ送信中"; drawStatus();
        if (storageReady) {
          sendFile("/capture0.jsonl", 0);
          sendFile("/capture1.jsonl", 1);
          Serial.println("LOG_DONE");
          status = "送信完了";
        } else Serial.println("LOG_ERROR mount");
      } else if (command == "INIT") {
        status = "記録領域を準備中"; drawStatus();
        // Explicit one-time setup only. Never initialize during DUMP or capture.
        if (!storageReady) storageReady = LittleFS.begin(true);
        Serial.println(storageReady ? "LOG_INIT_OK" : "LOG_ERROR init");
        status = storageReady ? "記録領域OK" : "準備に失敗";
      } else if (command == "CLEAR") {
        if (!storageReady) Serial.println("LOG_ERROR mount");
        else {
          const bool a = !LittleFS.exists("/capture0.jsonl") || LittleFS.remove("/capture0.jsonl");
          const bool b = !LittleFS.exists("/capture1.jsonl") || LittleFS.remove("/capture1.jsonl");
          Serial.println(a && b ? "LOG_CLEAR_OK" : "LOG_ERROR clear");
          status = a && b ? "記録を消去しました" : "消去に失敗";
        }
      }
      drawStatus();
      command = "";
    } else if (command.length() < 20) command += value;
  }
  if (millis() - lastDraw >= 1000) drawStatus();
  delay(2);
}
