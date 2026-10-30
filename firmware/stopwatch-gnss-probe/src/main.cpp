#include <Arduino.h>
#include <M5Unified.h>
#include <EspUsbHost.h>

namespace {
EspUsbHost usb;
EspUsbHostCdcSerial gnss(usb);
uint32_t bytesSeen = 0;
uint32_t nmeaLines = 0;
uint32_t ubxFrames = 0;
uint32_t sfrbxFrames = 0;
uint32_t qzssL1sFrames = 0;
uint32_t badChecksums = 0;
uint32_t lastDraw = 0;
uint32_t lastEnable = 0;
bool ch340Seen = false;
bool hostReady = false;
bool serialReady = false;

// PRIORIS GR-M10 example: enable UBX-RXM-SFRBX on UART1 in RAM only.
constexpr uint8_t enableSfrbx[] = {
    0xb5, 0x62, 0x06, 0x8a, 0x09, 0x00, 0x01, 0x01, 0x00,
    0x00, 0x32, 0x02, 0x91, 0x20, 0x01, 0x81, 0x30};

enum class ParseState { sync1, sync2, header, payload, checkA, checkB };
ParseState parseState = ParseState::sync1;
uint8_t header[4] = {};
uint8_t headerPos = 0;
uint16_t payloadLength = 0;
uint16_t payloadPos = 0;
uint8_t payloadPrefix[4] = {};
uint8_t checksumA = 0;
uint8_t checksumB = 0;

void addChecksum(uint8_t value) {
  checksumA += value;
  checksumB += checksumA;
}

void parseByte(uint8_t value) {
  if (value == '\n') ++nmeaLines; // Line count is diagnostic, not NMEA validation.
  switch (parseState) {
    case ParseState::sync1:
      if (value == 0xb5) parseState = ParseState::sync2;
      return;
    case ParseState::sync2:
      if (value == 0x62) {
        headerPos = 0;
        checksumA = checksumB = 0;
        parseState = ParseState::header;
      } else {
        parseState = value == 0xb5 ? ParseState::sync2 : ParseState::sync1;
      }
      return;
    case ParseState::header:
      header[headerPos++] = value;
      addChecksum(value);
      if (headerPos == sizeof(header)) {
        payloadLength = uint16_t(header[2]) | (uint16_t(header[3]) << 8);
        payloadPos = 0;
        if (payloadLength > 4096) parseState = ParseState::sync1;
        else parseState = payloadLength ? ParseState::payload : ParseState::checkA;
      }
      return;
    case ParseState::payload:
      if (payloadPos < sizeof(payloadPrefix)) payloadPrefix[payloadPos] = value;
      ++payloadPos;
      addChecksum(value);
      if (payloadPos == payloadLength) parseState = ParseState::checkA;
      return;
    case ParseState::checkA:
      if (value != checksumA) {
        ++badChecksums;
        parseState = ParseState::sync1;
      } else parseState = ParseState::checkB;
      return;
    case ParseState::checkB:
      if (value == checksumB) {
        ++ubxFrames;
        if (header[0] == 0x02 && header[1] == 0x13 && payloadLength >= 4) {
          ++sfrbxFrames;
          if (payloadPrefix[0] == 5 && payloadPrefix[2] == 1) ++qzssL1sFrames;
        }
      } else ++badChecksums;
      parseState = ParseState::sync1;
      return;
  }
}

void draw() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.drawCenterString("GNSS USB受信試験", center, 73);
  M5.Display.drawCenterString("研究・実験用", center, 105);
  String status = !serialReady ? "CH340準備失敗" :
                  !hostReady ? "USBホスト開始失敗" :
                  !ch340Seen ? "GNSS接続待ち" :
                  !gnss.connected() ? "CH340接続処理中" : "GNSS CH340 接続中";
  M5.Display.drawCenterString(status, center, 151);
  M5.Display.drawCenterString("1A86:7523  /  9600 bps", center, 180);
  M5.Display.drawCenterString("受信 " + String(bytesSeen) + " byte", center, 220);
  M5.Display.drawCenterString("NMEA行 " + String(nmeaLines), center, 250);
  M5.Display.drawCenterString("UBX " + String(ubxFrames) + " / SFRBX " + String(sfrbxFrames), center, 280);
  M5.Display.drawCenterString("QZSS L1S " + String(qzssL1sFrames), center, 310);
  M5.Display.drawCenterString("検査失敗 " + String(badChecksums), center, 340);
  M5.Display.drawCenterString("通報の復号・表示は未実装", center, 385);
}
}  // namespace

void setup() {
  M5.begin(M5.config());
  draw();
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo& device) {
    if (device.vid == 0x1a86 && device.pid == 0x7523) ch340Seen = true;
  });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo& device) {
    if (device.vid == 0x1a86 && device.pid == 0x7523) ch340Seen = false;
  });
  serialReady = gnss.setRxBufferSize(8192) && gnss.begin(9600);
  hostReady = serialReady && usb.begin();
}

void loop() {
  M5.update();
  if (hostReady && gnss.connected()) {
    const uint32_t now = millis();
    if (!lastEnable || now - lastEnable >= 60000) {
      gnss.write(enableSfrbx, sizeof(enableSfrbx));
      lastEnable = now;
    }
    while (gnss.available() > 0) {
      const int value = gnss.read();
      if (value < 0) break;
      ++bytesSeen;
      parseByte(static_cast<uint8_t>(value));
    }
  } else {
    lastEnable = 0;
  }
  if (millis() - lastDraw >= 1000) {
    draw();
    lastDraw = millis();
  }
  delay(1);
}
