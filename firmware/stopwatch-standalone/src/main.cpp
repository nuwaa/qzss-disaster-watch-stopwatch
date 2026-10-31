#include <Arduino.h>
#include <M5Unified.h>
#include <EspUsbHost.h>
#include <QZQSM.h>
#include <QZSSDCX.h>
#include <Preferences.h>
#include <cstdint>
#include <cstdlib>

namespace {
EspUsbHost usb;
EspUsbHostCdcSerial gnss(usb);
QZQSM dcr;
DCXDecoder dcx;
Preferences alertPrefs;
struct AlertLedger {
  uint32_t version;
  uint32_t next;
  uint64_t ids[64];
};
AlertLedger alertLedger = {};
bool alertStorageReady = false;
struct Entry { uint64_t id; String badge, title, body, ageStatus; uint32_t receivedMs; };
Entry history[8];
int count = 0, selected = 0;
uint32_t bytesSeen = 0, l1sSeen = 0, crcFailures = 0;
uint32_t lastDraw = 0, lastByte = 0, lastEnable = 0;
bool ch340Seen = false, serialReady = false, hostReady = false, dirty = true;
uint32_t vibrationEndsAt = 0;
uint32_t manualTestEndsAt = 0;
uint32_t automaticCueEndsAt = 0;
bool manualTestLatched = false;
bool textView = false;
bool buttonChord = false;
bool buttonChordLong = false;
String nmeaLine;
int64_t utcAtRmc = 0;
uint32_t msAtRmc = 0;
int utcYear = 0;
uint8_t consistentRmcCount = 0;
uint32_t firstStableClockMs = 0;
constexpr uint8_t enableSfrbx[] = {
  0xb5, 0x62, 0x06, 0x8a, 0x09, 0x00, 0x01, 0x01, 0x00,
  0x00, 0x32, 0x02, 0x91, 0x20, 0x01, 0x81, 0x30
};

uint32_t bits(const uint8_t* data, int start, int length) {
  uint32_t result = 0;
  for (int i = 0; i < length; ++i) {
    int bit = start + i;
    result = (result << 1) | ((data[bit / 8] >> (7 - bit % 8)) & 1);
  }
  return result;
}
bool leapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}
int daysInMonth(int year, int month) {
  static const uint8_t days[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
  return month == 2 && leapYear(year) ? 29 : days[month];
}
int64_t utcSeconds(int year, int month, int day, int hour, int minute, int second) {
  // Gregorian civil date to days since 1970-01-01; no local timezone involved.
  year -= month <= 2;
  const int era = year / 400;
  const unsigned yoe = year - era * 400;
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const int64_t days = era * 146097LL + doe - 719468;
  return days * 86400 + hour * 3600 + minute * 60 + second;
}
bool validClock() {
  return ch340Seen && gnss.connected() && consistentRmcCount >= 2 &&
         millis() - msAtRmc <= 10000;
}
String field(const String& line, int number) {
  int start = 0;
  for (int i = 0; i < number; ++i) {
    start = line.indexOf(',', start);
    if (start < 0) return "";
    ++start;
  }
  int end = line.indexOf(',', start);
  if (end < 0) end = line.indexOf('*', start);
  return end < 0 ? "" : line.substring(start, end);
}
void acceptRmc(const String& line) {
  if (line.length() < 30 || line[0] != '$' || line.substring(3, 6) != "RMC" || line[6] != ',') return;
  const int star = line.indexOf('*');
  if (star < 0 || star + 2 >= line.length()) return;
  uint8_t check = 0;
  for (int i = 1; i < star; ++i) check ^= static_cast<uint8_t>(line[i]);
  char actual[3] = {line[star + 1], line[star + 2], 0};
  char* end = nullptr;
  const unsigned expected = strtoul(actual, &end, 16);
  if (end != actual + 2 || expected != check || field(line, 2) != "A") return;
  const String time = field(line, 1), date = field(line, 9);
  if (time.length() < 6 || date.length() != 6) return;
  for (int i = 0; i < 6; ++i)
    if (time[i] < '0' || time[i] > '9' || date[i] < '0' || date[i] > '9') return;
  const int hour = time.substring(0, 2).toInt(), minute = time.substring(2, 4).toInt();
  const int second = time.substring(4, 6).toInt();
  const int day = date.substring(0, 2).toInt(), month = date.substring(2, 4).toInt();
  const int year = 2000 + date.substring(4, 6).toInt();
  if (year < 2020 || month < 1 || month > 12 || day < 1 ||
      day > daysInMonth(year, month) || hour > 23 || minute > 59 || second > 59) return;
  const int64_t receivedUtc = utcSeconds(year, month, day, hour, minute, second);
  if (utcYear && millis() - msAtRmc <= 5000 &&
      llabs(receivedUtc - (utcAtRmc + (millis() - msAtRmc) / 1000)) <= 2) {
    if (consistentRmcCount < 3) ++consistentRmcCount;
    if (consistentRmcCount == 2) firstStableClockMs = millis();
  } else {
    consistentRmcCount = 1;
    firstStableClockMs = 0;
  }
  utcYear = year;
  utcAtRmc = receivedUtc;
  msAtRmc = millis();
  dcr.SetYear(year);
}
void acceptNmeaByte(uint8_t value) {
  if (value == '$') { nmeaLine = "$"; return; }
  if (value == '\n') { acceptRmc(nmeaLine); nmeaLine = ""; return; }
  if (value == '\r') return;
  if (nmeaLine.length() && value >= 32 && value <= 126 && nmeaLine.length() < 120)
    nmeaLine += static_cast<char>(value);
  else if (nmeaLine.length()) nmeaLine = "";
}
struct AgeResult { String label; bool fresh; };
AgeResult ageForDcr(const uint8_t* data) {
  if (!validClock()) return {"GNSS時刻未確認", false};
  const int month = bits(data, 21, 4), day = bits(data, 25, 5);
  const int hour = bits(data, 30, 5), minute = bits(data, 35, 6);
  if (month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59) return {"発表時刻不正", false};
  const int64_t now = utcAtRmc + (millis() - msAtRmc) / 1000;
  int64_t bestAge = INT64_MAX;
  for (int year = utcYear - 1; year <= utcYear + 1; ++year) {
    if (day > daysInMonth(year, month)) continue;
    const int64_t age = now - utcSeconds(year, month, day, hour, minute, 0);
    if (llabs(age) < llabs(bestAge)) bestAge = age;
  }
  if (bestAge == INT64_MAX) return {"発表時刻不正", false};
  if (bestAge < 0) return {"未来の発表時刻", false};
  if (bestAge > 900) return {"古い通報", false};
  const String label = "発表から " + String(static_cast<int>(bestAge / 60)) + " 分";
  return {label, bestAge <= 180};
}
bool validCrc(const uint8_t* data) {
  // CRC-24Q over the first 226 bits, matching Azarashi's L1S decoder.
  uint32_t crc = 0;
  for (int bit = 0; bit < 226; ++bit) {
    crc ^= uint32_t((data[bit / 8] >> (7 - bit % 8)) & 1) << 23;
    crc <<= 1;
    if (crc & 0x1000000) crc ^= 0x1864cfb;
    crc &= 0xffffff;
  }
  return crc == bits(data, 226, 24);
}
uint64_t messageId(const uint8_t* data) {
  uint64_t hash = 14695981039346656037ULL;
  for (int i = 0; i < 32; ++i) { hash ^= data[i]; hash *= 1099511628211ULL; }
  return hash;
}
String dcrTitle(uint8_t category) {
  switch (category) {
    case 1: return "緊急地震速報"; case 2: return "震源情報";
    case 3: return "震度情報"; case 4: return "南海トラフ地震";
    case 5: return "津波情報"; case 6: return "北西太平洋津波";
    case 8: return "火山情報"; case 9: return "降灰情報";
    case 10: return "気象情報"; case 11: return "洪水情報";
    case 12: return "台風情報"; case 14: return "海上警報";
    default: return "災危通報";
  }
}
void initAlertStorage() {
  if (!alertPrefs.begin("qzssalert", false)) return;
  const size_t storedSize = alertPrefs.getBytesLength("seen");
  if (storedSize == 0) {
    alertLedger.version = 1;
    alertStorageReady = true;
    return;
  }
  alertStorageReady = storedSize == sizeof(alertLedger) &&
    alertPrefs.getBytes("seen", &alertLedger, sizeof(alertLedger)) == sizeof(alertLedger) &&
    alertLedger.version == 1 && alertLedger.next < 64;
}
bool rememberAlert(uint64_t id) {
  if (!alertStorageReady) return false;
  for (uint64_t seen : alertLedger.ids) if (seen == id) return false;
  const uint32_t slot = alertLedger.next;
  const uint64_t previous = alertLedger.ids[slot];
  alertLedger.ids[slot] = id;
  alertLedger.next = (slot + 1) % 64;
  if (alertPrefs.putBytes("seen", &alertLedger, sizeof(alertLedger)) == sizeof(alertLedger)) return true;
  alertLedger.ids[slot] = previous;
  alertLedger.next = slot;
  alertStorageReady = false;
  dirty = true;
  return false;
}
void startAutomaticCue() {
  M5.Power.setVibration(110);
  vibrationEndsAt = millis() + 250;
  M5.Speaker.tone(880, 180);
  automaticCueEndsAt = millis() + 5000;
  dirty = true;
}
bool store(const uint8_t* data, const String& badge,
           const String& title, const String& body, const String& ageStatus) {
  const uint64_t id = messageId(data);
  for (int i = 0; i < count; ++i) if (history[i].id == id) return false;
  for (int i = 7; i > 0; --i) history[i] = history[i - 1];
  history[0] = {id, badge, title, body, ageStatus, millis()};
  if (count < 8) ++count;
  selected = 0;
  dirty = true;
  return true;
}
void startManualCue() {
  // A+B long press is an explicit hardware self-test, never an actual alert.
  M5.Power.setVibration(110);
  vibrationEndsAt = millis() + 250;
  M5.Speaker.tone(880, 180);
  manualTestEndsAt = millis() + 1500;
  dirty = true;
}
void processSfrbx(const uint8_t* payload, uint16_t length) {
  if (length < 40 || payload[0] != 5 || payload[2] != 1 ||
      payload[4] < 8 || length != 8 + payload[4] * 4) return;
  ++l1sSeen;
  uint8_t message[32];
  for (int i = 0; i < 8; ++i)
    for (int b = 0; b < 4; ++b)
      message[i * 4 + b] = payload[8 + i * 4 + 3 - b];
  message[31] &= 0xc0;
  if (!validCrc(message)) { ++crcFailures; return; }
  const uint8_t mt = bits(message, 8, 6);
  if (mt == 43) {
    if (bits(message, 214, 6) != 1) return;
    const uint8_t classification = bits(message, 14, 3);
    dcr.Decode(message);
    const String body = dcr.GetReport();
    if (body.isEmpty()) return;
    const String badge = classification == 7 ? "訓練・試験" :
      classification >= 1 && classification <= 3 ? "衛星受信" : "区分不明";
    const AgeResult age = ageForDcr(message);
    const bool newToHistory = store(message, badge, dcrTitle(bits(message, 17, 4)), body, age.label);
    // Only a newly decoded DCR issue with a stable UTC clock can be considered.
    // Persist its identity before any output; the first minute after clock lock is silent.
    if (newToHistory && age.fresh && classification >= 1 && classification <= 3 &&
        bits(message, 41, 2) == 0) {
      const bool afterWarmup = firstStableClockMs && millis() - firstStableClockMs >= 60000;
      if (rememberAlert(messageId(message)) && afterWarmup) {
        history[0].badge = "衛星受信・通知済";
        startAutomaticCue();
      }
    }
  } else if (mt == 44) {
    if (!dcx.decode(message) || dcx.r.dcx_msg_type == DCX_MSG_NULL) return;
    const String badge = dcx.r.a1_message_type == 0 ? "訓練・試験" : "衛星受信";
    const String title = dcx.r.dcx_msg_type == DCX_MSG_J_ALERT ? "Jアラート" :
      dcx.r.dcx_msg_type == DCX_MSG_L_ALERT ? "Lアラート" : "拡張通報";
    String body = DCXDecoder::get_hazard_category_and_type_ja(dcx.r.a4_hazard_category_type);
    body += "\n";
    body += DCXDecoder::get_severity_str_ja(dcx.r.a5_severity);
    body += "\n";
    body += DCXDecoder::get_guidance_instruction_library_ja(dcx.r.a11_guidance_to_react_library);
    store(message, badge, title, body, "発表時刻未確認");
  }
}

enum class State { sync1, sync2, header, payload, checkA, checkB };
State state = State::sync1;
uint8_t header[4], payload[256], headerPos = 0, sumA = 0, sumB = 0;
uint16_t payloadLength = 0, payloadPos = 0;
void addSum(uint8_t value) { sumA += value; sumB += sumA; }
void parse(uint8_t value) {
  switch (state) {
    case State::sync1: if (value == 0xb5) state = State::sync2; return;
    case State::sync2:
      if (value == 0x62) { headerPos = 0; sumA = sumB = 0; state = State::header; }
      else state = value == 0xb5 ? State::sync2 : State::sync1;
      return;
    case State::header:
      header[headerPos++] = value; addSum(value);
      if (headerPos == 4) {
        payloadLength = uint16_t(header[2]) | (uint16_t(header[3]) << 8);
        payloadPos = 0;
        state = payloadLength > sizeof(payload) ? State::sync1 :
          payloadLength ? State::payload : State::checkA;
      }
      return;
    case State::payload:
      payload[payloadPos++] = value; addSum(value);
      if (payloadPos == payloadLength) state = State::checkA;
      return;
    case State::checkA:
      state = value == sumA ? State::checkB : State::sync1; return;
    case State::checkB:
      if (value == sumB && header[0] == 0x02 && header[1] == 0x13)
        processSfrbx(payload, payloadLength);
      state = State::sync1; return;
  }
}

void drawBody(const String& text, int center, int top, int width, int maxLines = 7, int lineHeight = 22) {
  String lines[7]; int row = 0;
  for (int i = 0; i < text.length() && row < maxLines;) {
    const uint8_t first = static_cast<uint8_t>(text[i]);
    if (first == '\r') { ++i; continue; }
    if (first == '\n') { ++i; ++row; continue; }
    int n = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    if (i + n > text.length()) n = 1;
    const String character = text.substring(i, i + n); i += n;
    if (lines[row].length() && M5.Display.textWidth(lines[row] + character) > width)
      if (++row >= maxLines) break;
    lines[row] += character;
  }
  for (int j = 0; j <= row && j < maxLines; ++j)
    M5.Display.drawCenterString(lines[j], center, top + j * lineHeight);
}
String receiverStatus() {
  return !serialReady ? "CH340準備失敗" : !hostReady ?
    "USBホスト開始失敗" : !ch340Seen ? "GNSS接続待ち" :
    !lastByte || millis() - lastByte > 15000 ? "GNSS未受信" : "衛星データ受信中";
}
String notificationStatus() {
  String status;
  if (!alertStorageReady) status = "保存不可・通知無効";
  else if (!validClock()) status = "通知: GNSS時刻待ち";
  else if (!firstStableClockMs) status = "通知: 準備中";
  else {
    const uint32_t elapsed = millis() - firstStableClockMs;
    status = elapsed < 60000 ?
      "通知: 準備中 あと" + String((60000 - elapsed + 999) / 1000) + "秒" :
      "自動通知待機";
  }
  if (automaticCueEndsAt && static_cast<int32_t>(automaticCueEndsAt - millis()) > 0)
    status = "新規通報通知";
  if (manualTestEndsAt && static_cast<int32_t>(manualTestEndsAt - millis()) > 0)
    status = "手動通知テスト";
  return status;
}
void drawTextView() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("QZSS 災危ウォッチ", center, 50);
  M5.Display.drawCenterString("研究・実験用", center, 76);
  M5.Display.drawCenterString(receiverStatus(), center, 105);
  if (!count) M5.Display.drawCenterString("通報を待っています", center, 210);
  else {
    const Entry& entry = history[selected];
    M5.Display.drawCenterString(entry.badge, center, 139);
    M5.Display.drawCenterString(entry.title, center, 169);
    drawBody(entry.body, center, 207, M5.Display.width() * 3 / 4);
    M5.Display.drawCenterString(entry.ageStatus, center, 365);
    M5.Display.drawCenterString(String(selected + 1) + "/" + String(count) +
                                "  A:前 B:次", center, 391);
  }
  M5.Display.drawCenterString("受信 " + String(bytesSeen) + "B  L1S " +
                              String(l1sSeen), center, 411);
  M5.Display.drawCenterString(notificationStatus(), center, 438);
}
void drawOverview() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  const String status = receiverStatus();
  const bool connected = status == "衛星データ受信中";
  M5.Display.fillRoundRect(68, 49, 332, 44, 18, connected ? 0x03E0 : 0x4208);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawCenterString(status, center, 61);
  if (!count) {
    M5.Display.setTextSize(2);
    M5.Display.drawCenterString("通報待ち", center, 175);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString("新しい通報はありません", center, 252);
  } else {
    const Entry& entry = history[selected];
    const bool test = entry.badge.indexOf("訓練") >= 0 || entry.badge.indexOf("試験") >= 0;
    const bool old = entry.ageStatus.indexOf("古い") >= 0;
    const uint16_t accent = test ? 0xFD20 : old ? 0x8410 : 0x07FF;
    M5.Display.fillRoundRect(55, 111, 358, 48, 16, accent);
    M5.Display.setTextColor(TFT_BLACK);
    M5.Display.drawCenterString(entry.badge, center, 125);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setTextSize(2);
    drawBody(entry.title, center, 174, 350, 1, 38);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString(entry.ageStatus, center, 208);
    M5.Display.drawFastHLine(80, 277, 308, 0x4208);
    M5.Display.setTextSize(2);
    drawBody(entry.body, center, 295, 350, 3, 42);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString(String(selected + 1) + "/" + String(count) +
                                "  A:前 B:次", center, 405);
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawCenterString(notificationStatus(), center, 433);
}
void draw() {
  if (textView) drawTextView(); else drawOverview();
  dirty = false;
}
}

void setup() {
  M5.begin(M5.config()); draw();
  M5.Power.setVibration(0);
  M5.Speaker.setVolume(64);
  initAlertStorage();
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo& device) {
    if (device.vid == 0x1a86 && device.pid == 0x7523) ch340Seen = true;
  });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo& device) {
    if (device.vid == 0x1a86 && device.pid == 0x7523) {
      ch340Seen = false;
      consistentRmcCount = 0;
      firstStableClockMs = 0;
      utcYear = 0;
      lastEnable = 0;
      dirty = true;
    }
  });
  serialReady = gnss.setRxBufferSize(8192) && gnss.begin(9600);
  hostReady = serialReady && usb.begin();
}
void loop() {
  M5.update();
  if (vibrationEndsAt && static_cast<int32_t>(millis() - vibrationEndsAt) >= 0) {
    M5.Power.setVibration(0);
    vibrationEndsAt = 0;
  }
  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) buttonChord = true;
  if (buttonChord && M5.BtnA.pressedFor(1800) && M5.BtnB.pressedFor(1800)) {
    if (!manualTestLatched) {
      startManualCue(); manualTestLatched = true; buttonChordLong = true;
    }
  }
  if (buttonChord && !M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
    if (!buttonChordLong) { textView = !textView; dirty = true; }
    buttonChord = false; buttonChordLong = false; manualTestLatched = false;
  }
  if (hostReady && gnss.connected()) {
    const uint32_t now = millis();
    if (!lastEnable || now - lastEnable >= 60000) {
      gnss.write(enableSfrbx, sizeof(enableSfrbx)); lastEnable = now;
    }
    while (gnss.available() > 0) {
      const int value = gnss.read(); if (value < 0) break;
      ++bytesSeen; lastByte = millis();
      acceptNmeaByte(static_cast<uint8_t>(value));
      parse(static_cast<uint8_t>(value));
    }
  } else lastEnable = 0;
  if (!buttonChord && M5.BtnA.wasPressed() && count) { selected = (selected + count - 1) % count; dirty = true; }
  if (!buttonChord && M5.BtnB.wasPressed() && count) { selected = (selected + 1) % count; dirty = true; }
  if (dirty || millis() - lastDraw >= 1000) { draw(); lastDraw = millis(); }
  delay(1);
}
