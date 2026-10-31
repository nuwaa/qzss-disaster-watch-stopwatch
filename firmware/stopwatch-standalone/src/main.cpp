#include <Arduino.h>
#include <M5Unified.h>
#include <EspUsbHost.h>
#include <QZQSM.h>
#include <QZSSDCX.h>
#include <Preferences.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#ifndef QZSS_NOTIFICATION_TEST
#define QZSS_NOTIFICATION_TEST 0
#endif
#ifndef QZSS_DECODER_TEST
#define QZSS_DECODER_TEST 0
#endif
#ifndef QZSS_LIFECYCLE_TEST
#define QZSS_LIFECYCLE_TEST 0
#endif
#ifndef QZSS_DCX_TEST
#define QZSS_DCX_TEST 0
#endif
#if QZSS_DECODER_TEST
#include "comparison_fixtures.h"
#endif
#if QZSS_LIFECYCLE_TEST
#include "lifecycle_fixtures.h"
#endif
#if QZSS_DCX_TEST
#include "dcx_fixtures.h"
#endif

namespace {
EspUsbHost usb;
EspUsbHostCdcSerial gnss(usb);
QZQSM dcr;
DCXDecoder dcx;
Preferences alertPrefs;
Preferences evidencePrefs;
Preferences settingsPrefs;
struct AlertLedger {
  uint32_t version;
  uint32_t next;
  uint64_t ids[64];
};
AlertLedger alertLedger = {};
bool alertStorageReady = false;
struct __attribute__((packed)) AlertEvidence {
  uint32_t magic;
  uint8_t version;
  uint8_t classification;
  uint8_t issue;
  uint8_t source;
  int32_t ageSeconds;
  int64_t receivedUtc;
  uint64_t id;
  uint8_t l1s[32];
  uint32_t checksum;
};
static_assert(sizeof(AlertEvidence) == 64, "Unexpected alert evidence layout");
constexpr uint32_t evidenceMagic = 0x515a4556;
AlertEvidence lastEvidence = {};
bool evidenceStorageReady = false;
bool evidenceAvailable = false;
bool evidenceSaveFailed = false;
bool evidenceReadFailed = false;
struct Entry { uint64_t id; String badge, title, body, ageStatus, group; uint32_t receivedMs; };
constexpr int historyCapacity = 16;
Entry history[historyCapacity];
int count = 0, selected = 0;
uint32_t bytesSeen = 0, l1sSeen = 0, crcFailures = 0;
uint32_t lastDraw = 0, lastByte = 0, lastValidL1s = 0, lastEnable = 0;
bool ch340Seen = false, serialReady = false, hostReady = false, dirty = true;
uint32_t vibrationEndsAt = 0;
uint32_t manualTestEndsAt = 0;
uint32_t automaticCueEndsAt = 0;
bool manualTestLatched = false;
uint8_t viewMode = 0; // 0: overview, 1: text, 2: saved alert evidence, 3: sound settings.
uint8_t soundLevel = 3; // 0: mute, 1: low, 2: medium, 3: high.
bool settingsStorageReady = false;
bool settingsSaveFailed = false;
#if QZSS_LIFECYCLE_TEST
int lifecycleStage = 0;
#endif
#if QZSS_DCX_TEST
int dcxStage = 0;
#endif
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
constexpr uint8_t soundVolumes[] = {0, 180, 200, 220};
constexpr const char* soundLabels[] = {"消音", "小", "中", "大"};
constexpr float alertToneHz = 4000.0f;
constexpr uint32_t alertToneMs = 300;

void applySoundLevel() {
  M5.Speaker.setVolume(soundVolumes[soundLevel]);
}
void initSoundSettings() {
  settingsStorageReady = settingsPrefs.begin("qzsscfg", false);
  if (settingsStorageReady) {
    const uint8_t saved = settingsPrefs.getUChar("sound", 3);
    if (saved < 4) soundLevel = saved;
  }
  applySoundLevel();
}
void changeSoundLevel(int direction) {
  const int next = static_cast<int>(soundLevel) + direction;
  if (next < 0 || next > 3) return;
  soundLevel = static_cast<uint8_t>(next);
  applySoundLevel();
  settingsSaveFailed = !settingsStorageReady ||
    settingsPrefs.putUChar("sound", soundLevel) != sizeof(uint8_t);
  dirty = true;
}

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
struct AgeResult { String label; bool fresh; int32_t seconds; };
AgeResult ageForDcr(const uint8_t* data) {
  if (!validClock()) return {"GNSS時刻未確認", false, -1};
  const int month = bits(data, 21, 4), day = bits(data, 25, 5);
  const int hour = bits(data, 30, 5), minute = bits(data, 35, 6);
  if (month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59) return {"発表時刻不正", false, -1};
  const int64_t now = utcAtRmc + (millis() - msAtRmc) / 1000;
  int64_t bestAge = INT64_MAX;
  for (int year = utcYear - 1; year <= utcYear + 1; ++year) {
    if (day > daysInMonth(year, month)) continue;
    const int64_t age = now - utcSeconds(year, month, day, hour, minute, 0);
    if (llabs(age) < llabs(bestAge)) bestAge = age;
  }
  if (bestAge == INT64_MAX) return {"発表時刻不正", false, -1};
  if (bestAge < 0) return {"未来の発表時刻", false, -1};
  if (bestAge > 900) return {"古い通報", false, -1};
  const String label = "発表から " + String(static_cast<int>(bestAge / 60)) + " 分";
  return {label, bestAge <= 180, static_cast<int32_t>(bestAge)};
}
AgeResult ageForDcx(const DCXMessage& report) {
  // A6/A7 encode hazard onset, not issue time. Never label it as publication time.
  if (!validClock()) return {"GNSS時刻未確認", false, -1};
  if (report.a7_hazard_onset_ToW < 1 || report.a7_hazard_onset_ToW > 10080 ||
      report.a6_hazard_onset_week_number > 1)
    return {"発生時刻未指定", false, -1};
  const int64_t now = utcAtRmc + (millis() - msAtRmc) / 1000;
  const int64_t days = now / 86400;
  const int weekday = static_cast<int>((days + 3) % 7); // Monday=0, 1970-01-01=Thursday.
  const int64_t monday = (days - weekday) * 86400;
  const int64_t onset = monday + report.a6_hazard_onset_week_number * 604800LL +
    (report.a7_hazard_onset_ToW - 1) * 60LL;
  const int64_t age = now - onset;
  if (age < -180) return {"発生予定時刻は先", false, -1};
  if (age > 900) return {"発生から15分超", false, -1};
  return {age < 0 ? "発生予定まで " + String(static_cast<int>((-age + 59) / 60)) + " 分" :
                      "発生から " + String(static_cast<int>(age / 60)) + " 分",
          age >= -180 && age <= 180, static_cast<int32_t>(age)};
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
  if (bits(data, 8, 6) == 44) {
    // Exclude satellite designation and CRC: the same DCX from another QZS
    // must not become a new alert merely because those transport bits differ.
    for (int i = 3; i < 28; ++i) { hash ^= data[i]; hash *= 1099511628211ULL; }
    hash ^= data[28] & 0xc0; hash *= 1099511628211ULL;
    return hash;
  }
  for (int i = 0; i < 32; ++i) { hash ^= data[i]; hash *= 1099511628211ULL; }
  return hash;
}
uint32_t evidenceChecksum(const AlertEvidence& evidence) {
  const auto* bytes = reinterpret_cast<const uint8_t*>(&evidence);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(AlertEvidence, checksum); ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}
bool validEvidence(const AlertEvidence& evidence) {
  const bool dcrEvidence = evidence.version == 1 &&
    evidence.classification >= 1 && evidence.classification <= 3 &&
    evidence.issue == 0 && evidence.ageSeconds >= 0 && evidence.ageSeconds <= 180;
  const bool dcxEvidence = evidence.version == 2 &&
    evidence.classification == 44 && (evidence.issue == 1 || evidence.issue == 2) &&
    evidence.ageSeconds >= -180 && evidence.ageSeconds <= 180;
  return evidence.magic == evidenceMagic && (dcrEvidence || dcxEvidence) &&
         evidence.source == (QZSS_NOTIFICATION_TEST ? 2 : 1) &&
         (evidence.source != 2 || dcrEvidence) &&
         (evidence.source == 2 || evidence.receivedUtc > 0) &&
         evidence.checksum == evidenceChecksum(evidence) &&
         evidence.id == messageId(evidence.l1s);
}
void initEvidenceStorage() {
  evidenceStorageReady = evidencePrefs.begin(
    QZSS_NOTIFICATION_TEST ? "qzssdiagt" : "qzssdiag", false);
  if (!evidenceStorageReady) return;
  const size_t storedSize = evidencePrefs.getBytesLength("last");
  if (!storedSize) return;
  if (storedSize != sizeof(lastEvidence)) { evidenceReadFailed = true; return; }
  AlertEvidence loaded = {};
  if (evidencePrefs.getBytes("last", &loaded, sizeof(loaded)) == sizeof(loaded) &&
      validEvidence(loaded)) {
    lastEvidence = loaded;
    evidenceAvailable = true;
  } else evidenceReadFailed = true;
}
bool saveAlertEvidence(const uint8_t* l1s, int32_t ageSeconds, int64_t receivedUtc,
                       uint8_t classification, uint8_t issue, uint8_t mt = 43) {
  if (!evidenceStorageReady) { evidenceSaveFailed = true; return false; }
  AlertEvidence candidate = {};
  candidate.magic = evidenceMagic;
  candidate.version = mt == 44 ? 2 : 1;
  candidate.classification = classification;
  candidate.issue = issue;
  candidate.source = QZSS_NOTIFICATION_TEST ? 2 : 1;
  candidate.ageSeconds = ageSeconds;
  candidate.receivedUtc = receivedUtc;
  candidate.id = messageId(l1s);
  memcpy(candidate.l1s, l1s, sizeof(candidate.l1s));
  candidate.checksum = evidenceChecksum(candidate);
  AlertEvidence verified = {};
  const bool saved = evidencePrefs.putBytes("last", &candidate, sizeof(candidate)) ==
                       sizeof(candidate) &&
                     evidencePrefs.getBytes("last", &verified, sizeof(verified)) ==
                       sizeof(verified) && validEvidence(verified) &&
                     memcmp(&candidate, &verified, sizeof(candidate)) == 0;
  evidenceSaveFailed = !saved;
  if (saved) {
    lastEvidence = verified;
    evidenceAvailable = true;
    evidenceReadFailed = false;
  }
  dirty = true;
  return saved;
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
  // The synthetic test build has its own NVS namespace.
  if (!alertPrefs.begin(QZSS_NOTIFICATION_TEST ? "qzsstest" : "qzssalert", false)) return;
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
bool considerAutomaticAlert(uint64_t id, bool newToHistory, bool fresh,
                            uint8_t classification, uint8_t issue, bool afterWarmup) {
  if (!newToHistory || !fresh || classification < 1 || classification > 3 || issue != 0)
    return false;
  // Remember even during warmup so a later repeat cannot become a new alert.
  return rememberAlert(id) && afterWarmup;
}
void startAutomaticCue() {
  M5.Power.setVibration(110);
  vibrationEndsAt = millis() + 250;
  if (soundLevel != 0) M5.Speaker.tone(alertToneHz, alertToneMs);
  automaticCueEndsAt = millis() + 5000;
  dirty = true;
}
bool store(const uint8_t* data, const String& badge,
           const String& title, const String& body, const String& ageStatus,
           const String& group = "") {
  const uint64_t id = messageId(data);
  for (int i = 0; i < count; ++i) if (history[i].id == id) {
    // A still-transmitted report may follow another event's cancellation.
    // Refresh its display without treating the repeated frame as a new alert.
    history[i].receivedMs = millis();
    if (history[i].group.isEmpty() &&
        history[i].badge.indexOf("過去の受信情報") >= 0 &&
        badge.indexOf("取消") < 0) {
      history[i].badge = badge;
      selected = i;
    }
    dirty = true;
    return false;
  }
  for (int i = historyCapacity - 1; i > 0; --i) history[i] = history[i - 1];
  history[0] = {id, badge, title, body, ageStatus, group, millis()};
  if (count < historyCapacity) ++count;
  selected = 0;
  dirty = true;
  return true;
}
bool pastEntry(const Entry& entry) {
  return entry.badge.indexOf("過去の受信情報") >= 0 ||
    (entry.title == "緊急地震速報" && entry.badge.indexOf("取消") < 0 &&
     entry.badge.indexOf("訓練") < 0 && entry.badge.indexOf("試験") < 0 &&
     millis() - entry.receivedMs > 300000);
}
bool hasOtherReportInCategory(const String& title, uint64_t id, bool test) {
  for (int i = 0; i < count; ++i)
    if (history[i].title == title && history[i].id != id &&
        history[i].badge.indexOf("取消") < 0 &&
        (history[i].badge.indexOf("試験") >= 0 ||
         history[i].badge.indexOf("訓練") >= 0) == test)
      return true;
  return false;
}
void markEarlierReportsAsPast(const String& title) {
  // A DCR cancellation has no reliable identifier for a particular older entry.
  // Neutralize only the same category; do not claim that every event was cancelled.
  for (int i = 1; i < count; ++i)
    if (history[i].title == title && history[i].badge.indexOf("取消") < 0 &&
        (QZSS_LIFECYCLE_TEST ||
         (history[i].badge.indexOf("訓練") < 0 &&
          history[i].badge.indexOf("試験") < 0)))
      history[i].badge = QZSS_LIFECYCLE_TEST ?
        "試験・過去の受信情報" : "過去の受信情報";
}
String dcxGroup(const DCXMessage& report) {
  String group = String(QZSS_DCX_TEST || report.a1_message_type == 0 ? "test:" : "actual:") +
    String(report.a2_country_region_name) + ":" +
    String(report.a3_provider_identifier) + ":" +
    String(report.a4_hazard_category_type);
  if (report.dcx_msg_type == DCX_MSG_J_ALERT)
    group += ":" + String(report.ex8_target_area_code_list_type) + ":" +
      String(report.ex9_target_area_code_list[0], HEX) + ":" +
      String(report.ex9_target_area_code_list[1], HEX);
  else if (report.dcx_msg_type == DCX_MSG_L_ALERT || report.dcx_msg_type == DCX_MSG_MT_INFO)
    group += ":" + String(report.ex1_target_area_code);
  return group;
}
void markEarlierDcxAsPast(const String& group) {
  // DCX has no guaranteed event identifier. Neutralize only matching provider,
  // hazard and target-code group; do not claim that every event was cleared.
  for (int i = 1; i < count; ++i)
    if (history[i].group == group &&
        history[i].badge.indexOf("過去の受信情報") < 0 &&
        history[i].badge.indexOf("全解除") < 0)
      history[i].badge = group.startsWith("test:") ?
        "試験・過去の受信情報" : "過去の受信情報";
}
String dcxTarget(const DCXMessage& report) {
  if (report.a2_country_region_name != 111)
    return "国・地域コード " + String(report.a2_country_region_name);
  if (report.dcx_msg_type == DCX_MSG_J_ALERT) {
    if (report.ex8_target_area_code_list_type == 0) {
      String names;
      int total = 0;
      for (int bit = 0; bit < 64; ++bit) {
        if (!(report.ex9_target_area_code_list[bit / 32] &
              (uint32_t(1) << (31 - bit % 32)))) continue;
        ++total;
        if (total <= 3) {
          if (!names.isEmpty()) names += "、";
          names += DCXDecoder::get_prefecture_name_ja(bit);
        }
      }
      return total ? "対象: " + names + (total > 3 ? " ほか" : "") : "対象地域不明";
    }
    const uint16_t area = report.ex9_target_area_code_list[0] >> 16;
    return area ? "対象市区町村コード: " + String(area) + " ほか" : "対象地域不明";
  }
  if (report.dcx_msg_type == DCX_MSG_L_ALERT || report.dcx_msg_type == DCX_MSG_MT_INFO)
    return report.ex1_target_area_code ?
      "対象市区町村コード: " + String(report.ex1_target_area_code) : "対象地域不明";
  return "対象地域未確認";
}
void startManualCue() {
  // A+B long press is an explicit hardware self-test, never an actual alert.
  M5.Power.setVibration(110);
  vibrationEndsAt = millis() + 250;
  if (soundLevel != 0) M5.Speaker.tone(alertToneHz, alertToneMs);
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
  lastValidL1s = millis();
  const uint8_t mt = bits(message, 8, 6);
  if (mt == 43) {
    if (bits(message, 214, 6) != 1) return;
    const uint8_t classification = bits(message, 14, 3);
    dcr.Decode(message);
    const String body = dcr.GetReport();
    if (body.isEmpty()) return;
    const uint8_t category = bits(message, 17, 4);
    const uint8_t informationType = bits(message, 41, 2);
    const bool cancellation = (category == 1 || category == 5) && informationType == 2;
    const String title = dcrTitle(category);
    const bool different = !cancellation && informationType == 0 &&
      hasOtherReportInCategory(title, messageId(message),
                               QZSS_LIFECYCLE_TEST || classification == 7);
    const String badge = QZSS_LIFECYCLE_TEST ?
      (cancellation ? "試験・取消" : different ? "試験・別内容" : "試験・発表") :
      classification == 7 ?
      (cancellation ? "訓練・試験・取消" : different ? "訓練・別内容" : "訓練・試験") :
      cancellation ? "取消・衛星受信" :
      informationType == 1 ? (category == 1 || category == 5 ?
        "種別未定義" : "訂正・衛星受信") :
      informationType == 3 ? "種別未定義" :
      different ? "発表・別内容" :
      classification >= 1 && classification <= 3 ? "衛星受信" : "区分不明";
    const AgeResult age = ageForDcr(message);
    const int64_t observedUtc = age.fresh ?
      utcAtRmc + (millis() - msAtRmc) / 1000 : 0;
    const bool newToHistory = store(message, badge, title, body, age.label);
    if (newToHistory && cancellation && (QZSS_LIFECYCLE_TEST ||
        (classification >= 1 && classification <= 3 && age.seconds >= 0 &&
         age.seconds <= 300))) markEarlierReportsAsPast(title);
    // Only a newly decoded DCR issue with a stable UTC clock can be considered.
    // Persist its identity before any output; the first minute after clock lock is silent.
    const bool afterWarmup = firstStableClockMs && millis() - firstStableClockMs >= 60000;
    if (considerAutomaticAlert(messageId(message), newToHistory, age.fresh,
                               classification, informationType, afterWarmup)) {
      history[0].badge = "衛星受信・通知済";
      startAutomaticCue();
      saveAlertEvidence(message, age.seconds, observedUtc,
        classification, informationType);
    }
  } else if (mt == 44) {
    if (!dcx.decode(message) || dcx.r.dcx_msg_type == DCX_MSG_NULL) return;
    const uint8_t action = dcx.r.a1_message_type;
    const String badge = QZSS_DCX_TEST ?
      (action == 0 ? "試験・訓練" : action == 1 ? "試験・発表" :
       action == 2 ? "試験・更新" : "試験・全解除") :
      action == 0 ? "訓練・試験" :
      action == 1 ? "発表・衛星受信" :
      action == 2 ? "更新・衛星受信" : "全解除・衛星受信";
    const String title = dcx.r.dcx_msg_type == DCX_MSG_J_ALERT ? "Jアラート" :
      dcx.r.dcx_msg_type == DCX_MSG_L_ALERT ? "Lアラート" :
      dcx.r.dcx_msg_type == DCX_MSG_MT_INFO ? "自治体通報" : "拡張通報";
    const AgeResult onset = ageForDcx(dcx.r);
    const String group = dcxGroup(dcx.r);
    String body = dcxTarget(dcx.r);
    body += "\n対象地域未照合\n";
    body += DCXDecoder::get_hazard_category_and_type_ja(dcx.r.a4_hazard_category_type);
    body += "\n";
    body += DCXDecoder::get_severity_str_ja(dcx.r.a5_severity);
    body += "\n";
    body += DCXDecoder::get_guidance_instruction_library_ja(dcx.r.a11_guidance_to_react_library);
    const bool newToHistory = store(message, badge, title, body, onset.label, group);
    if (!newToHistory) return;
    if (action == 2 || action == 3) markEarlierDcxAsPast(group);
    // A6/A7 is onset, never publication time. Alert only for a near-now onset,
    // stable GNSS UTC, a recognized Japanese provider, and a persisted new ID.
    const bool knownJapaneseProvider = dcx.r.a2_country_region_name == 111 &&
      (dcx.r.dcx_msg_type == DCX_MSG_J_ALERT ||
       dcx.r.dcx_msg_type == DCX_MSG_L_ALERT ||
       dcx.r.dcx_msg_type == DCX_MSG_MT_INFO);
    const bool afterWarmup = firstStableClockMs && millis() - firstStableClockMs >= 60000;
    if (!QZSS_DCX_TEST && (action == 1 || action == 2) &&
        knownJapaneseProvider && onset.fresh &&
        afterWarmup && alertStorageReady && rememberAlert(messageId(message))) {
      history[0].badge = badge + "・通知済";
      startAutomaticCue();
      saveAlertEvidence(message, onset.seconds,
        utcAtRmc + (millis() - msAtRmc) / 1000, 44, action, 44);
    }
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

void drawBody(const String& text, int center, int top, int width,
              int maxLines = 7, int lineHeight = 22, bool compactBlankLines = false) {
  String lines[7]; int row = 0;
  for (int i = 0; i < text.length() && row < maxLines;) {
    const uint8_t first = static_cast<uint8_t>(text[i]);
    if (first == '\r') { ++i; continue; }
    if (first == '\n') {
      ++i;
      if (!compactBlankLines || !lines[row].isEmpty()) ++row;
      continue;
    }
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
void drawOverviewBody(const String& body, int center) {
  int start = 0, row = 0;
  while (start < body.length() && row < 3) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.isEmpty()) continue;
    M5.Display.setTextSize(1.25f);
    if (M5.Display.textWidth(line) > 380) M5.Display.setTextSize(1);
    if (M5.Display.textWidth(line) > 380) {
      const String ellipsis = "…";
      while (line.length() && M5.Display.textWidth(line + ellipsis) > 380) {
        int last = line.length() - 1;
        while (last > 0 && (static_cast<uint8_t>(line[last]) & 0xc0) == 0x80) --last;
        line.remove(last);
      }
      line += ellipsis;
    }
    M5.Display.drawCenterString(line, center, 247 + row * 23);
    ++row;
  }
}
String receiverStatus() {
#if QZSS_DCX_TEST
  return "DCX試験データ・非衛星";
#endif
#if QZSS_LIFECYCLE_TEST
  return "試験データ・非衛星";
#endif
  if (!serialReady) return "CH340準備失敗";
  if (!hostReady) return "USBホスト開始失敗";
  if (!ch340Seen || !gnss.connected()) return "GNSS接続待ち";
  if (!lastByte) return "GNSSデータ未受信";
  if (millis() - lastByte > 15000) return "GNSS受信途絶";
  if (!lastValidL1s) return "GNSS受信中・L1S未受信";
  if (millis() - lastValidL1s > 30000) return "L1S受信途絶";
  return "L1S受信中";
}
String notificationStatus() {
#if QZSS_DCX_TEST
  static const char* stages[] = {"発表", "別衛星再送", "更新", "全解除", "解除再送", "試験"};
  return "試験 " + String(dcxStage + 1) + "/6 " + stages[dcxStage];
#endif
#if QZSS_LIFECYCLE_TEST
  static const char* stages[] = {"発表", "同一再送", "別内容", "取消", "取消再送", "別内容継続"};
  return "試験 " + String(lifecycleStage + 1) + "/6 " + stages[lifecycleStage] + " A:次 B:投入";
#endif
  String status;
  if (!alertStorageReady) status = "保存不可・通知無効";
  else if (receiverStatus() != "L1S受信中") status = "通知: L1S受信待ち";
  else if (!validClock()) status = "通知: GNSS時刻待ち";
  else if (!firstStableClockMs) status = "通知: 準備中";
  else {
    const uint32_t elapsed = millis() - firstStableClockMs;
    status = elapsed < 60000 ?
      "通知: 準備中 あと" + String((60000 - elapsed + 999) / 1000) + "秒" :
      "自動通知待機";
  }
  if (automaticCueEndsAt && static_cast<int32_t>(automaticCueEndsAt - millis()) > 0)
    status = soundLevel == 0 ? "新規通報・消音（振動）" : "新規通報通知";
  if (manualTestEndsAt && static_cast<int32_t>(manualTestEndsAt - millis()) > 0)
    status = soundLevel == 0 ? "手動通知テスト・消音" : "手動通知テスト";
  return status;
}
String bootStatus() {
  return "起動後 " + String(millis() / 60000) + "分";
}
void drawTextView() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString(bootStatus(), center, 24);
  M5.Display.drawCenterString("QZSS 災危ウォッチ", center, 50);
  M5.Display.drawCenterString("研究・実験用", center, 76);
  M5.Display.drawCenterString(receiverStatus(), center, 105);
  if (!count) M5.Display.drawCenterString("通報未確認・受信状態を確認", center, 210);
  else {
    const Entry& entry = history[selected];
    M5.Display.drawCenterString(pastEntry(entry) ?
      (QZSS_LIFECYCLE_TEST ? "試験・過去の受信情報" : "過去の受信情報") :
      entry.badge, center, 139);
    M5.Display.drawCenterString(entry.title, center, 169);
    drawBody(entry.body, center, 207, M5.Display.width() * 3 / 4);
    M5.Display.drawCenterString(entry.ageStatus, center, 365);
#if QZSS_DCX_TEST
    M5.Display.drawCenterString(String(count) + "件  A:次 B:投入", center, 391);
#else
    M5.Display.drawCenterString(String(selected + 1) + "/" + String(count) +
                                "  A:前 B:次", center, 391);
#endif
  }
  M5.Display.drawCenterString("受信 " + String(bytesSeen) + "B  L1S " +
                              String(l1sSeen), center, 411);
  M5.Display.drawCenterString(notificationStatus(), center, 438);
}
void drawStateIcon(int x, int y, bool test, bool old, bool notified, uint16_t color) {
  if (test) { // Beaker: test data remains recognizable without color.
    M5.Display.drawLine(x - 4, y - 8, x + 4, y - 8, color);
    M5.Display.drawLine(x - 2, y - 8, x - 2, y - 1, color);
    M5.Display.drawLine(x + 2, y - 8, x + 2, y - 1, color);
    M5.Display.drawLine(x - 2, y - 1, x - 8, y + 8, color);
    M5.Display.drawLine(x + 2, y - 1, x + 8, y + 8, color);
    M5.Display.drawLine(x - 8, y + 8, x + 8, y + 8, color);
  } else if (old) {
    M5.Display.drawCircle(x, y, 9, color);
    M5.Display.drawLine(x, y, x, y - 5, color);
    M5.Display.drawLine(x, y, x + 4, y + 2, color);
  } else {
    M5.Display.drawTriangle(x, y - 10, x - 10, y + 9, x + 10, y + 9, color);
    M5.Display.drawLine(x, y - 3, x, y + 3, color);
    M5.Display.fillCircle(x, y + 6, 1, color);
    if (notified) M5.Display.drawCircle(x, y, 13, color);
  }
}
void drawCategoryIcon(const String& title, int x, int y, uint16_t color) {
  if (title.indexOf("津波") >= 0 || title.indexOf("海上") >= 0) {
    M5.Display.drawLine(x - 11, y + 2, x - 5, y - 2, color);
    M5.Display.drawLine(x - 5, y - 2, x + 1, y + 2, color);
    M5.Display.drawLine(x + 1, y + 2, x + 7, y - 2, color);
    M5.Display.drawLine(x + 7, y - 2, x + 11, y + 1, color);
    M5.Display.drawFastHLine(x - 11, y + 7, 22, color);
  } else if (title.indexOf("地震") >= 0 || title.indexOf("震度") >= 0) {
    M5.Display.drawLine(x - 11, y, x - 6, y, color);
    M5.Display.drawLine(x - 6, y, x - 3, y - 8, color);
    M5.Display.drawLine(x - 3, y - 8, x + 2, y + 8, color);
    M5.Display.drawLine(x + 2, y + 8, x + 5, y, color);
    M5.Display.drawLine(x + 5, y, x + 11, y, color);
  } else if (title.indexOf("火山") >= 0 || title.indexOf("降灰") >= 0) {
    M5.Display.drawTriangle(x - 11, y + 9, x, y - 8, x + 11, y + 9, color);
    M5.Display.fillCircle(x - 2, y - 11, 1, color);
    M5.Display.fillCircle(x + 4, y - 13, 1, color);
  } else if (title.indexOf("台風") >= 0 || title.indexOf("気象") >= 0) {
    M5.Display.drawCircle(x, y, 9, color);
    M5.Display.fillCircle(x, y, 2, color);
    M5.Display.drawLine(x - 11, y - 3, x - 4, y - 3, color);
    M5.Display.drawLine(x + 4, y + 3, x + 11, y + 3, color);
  } else {
    M5.Display.drawCircle(x, y, 9, color);
    M5.Display.fillCircle(x, y, 2, color);
  }
}
void drawSatelliteIcon(int x, int y, uint16_t color) {
  M5.Display.fillRect(x - 4, y - 4, 8, 8, color);
  M5.Display.drawRect(x - 15, y - 6, 8, 12, color);
  M5.Display.drawRect(x + 7, y - 6, 8, 12, color);
  M5.Display.drawLine(x - 7, y, x - 4, y, color);
  M5.Display.drawLine(x + 4, y, x + 7, y, color);
  M5.Display.drawLine(x, y - 4, x + 4, y - 10, color);
}
void drawDishIcon(int x, int y, uint16_t color, bool waves, bool interrupted) {
  // A compact antenna pictogram for the receiver-status band.
  M5.Display.drawLine(x - 10, y - 8, x - 9, y - 3, color);
  M5.Display.drawLine(x - 9, y - 3, x - 5, y + 2, color);
  M5.Display.drawLine(x - 5, y + 2, x + 1, y + 5, color);
  M5.Display.drawLine(x + 1, y + 5, x + 6, y + 5, color);
  M5.Display.drawLine(x - 1, y + 4, x + 7, y - 6, color);
  M5.Display.fillCircle(x + 7, y - 6, 2, color);
  M5.Display.drawLine(x - 1, y + 5, x - 1, y + 12, color);
  M5.Display.drawFastHLine(x - 7, y + 12, 13, color);
  if (waves) {
    M5.Display.drawLine(x + 11, y - 9, x + 15, y - 12, color);
    M5.Display.drawLine(x + 12, y - 4, x + 17, y - 5, color);
  }
  if (interrupted) M5.Display.drawLine(x - 13, y + 11, x + 14, y - 13, color);
}
void drawOverview() {
  const int center = M5.Display.width() / 2;
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  const String status = receiverStatus();
  const bool connected = status == "L1S受信中";
  const bool receiverConnected = status == "GNSS受信中・L1S未受信" ||
    status == "L1S受信途絶" || connected;
  const bool interrupted = status == "GNSS受信途絶" || status == "L1S受信途絶";
  uint16_t background = connected ? 0x0841 : 0x1082;
  if (count) {
    const Entry& entry = history[selected];
    if (entry.badge.indexOf("訓練") >= 0 || entry.badge.indexOf("試験") >= 0)
      background = 0x2100;
    else if (pastEntry(entry) || entry.ageStatus.indexOf("古い") >= 0) background = 0x18C3;
    else if (entry.badge.indexOf("通知済") >= 0) background = 0x2000;
  }
  M5.Display.fillScreen(background);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawCenterString(bootStatus(), center, 22);
  M5.Display.fillRoundRect(68, 49, 332, 44, 18, connected ? 0x03E0 : 0x4208);
  M5.Display.setTextColor(TFT_WHITE);
  drawDishIcon(101, 70, connected ? TFT_WHITE :
               interrupted ? 0xBDF7 : receiverConnected ? 0x07FF : 0xBDF7,
               connected, interrupted);
  M5.Display.drawCenterString(status, center + 18, 61);
  if (!count) {
    drawSatelliteIcon(center, 142, connected ? 0x07FF : 0x8410);
    M5.Display.setTextSize(2);
    M5.Display.drawCenterString("通報未確認", center, 185);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString("受信状態を確認してください", center, 252);
  } else {
    const Entry& entry = history[selected];
    const bool test = entry.badge.indexOf("訓練") >= 0 || entry.badge.indexOf("試験") >= 0;
    const bool old = pastEntry(entry) || entry.ageStatus.indexOf("古い") >= 0;
    const bool notified = entry.badge.indexOf("通知済") >= 0;
    const uint16_t accent = test ? 0xFD20 : old ? 0x8410 : notified ? 0xF800 : 0x07FF;
    M5.Display.fillRoundRect(55, 111, 358, 48, 16, accent);
    M5.Display.setTextColor(old || notified ? TFT_WHITE : TFT_BLACK);
    drawStateIcon(82, 135, test, old, notified, old || notified ? TFT_WHITE : TFT_BLACK);
    M5.Display.drawCenterString(old && pastEntry(entry) ?
      (QZSS_LIFECYCLE_TEST ? "試験・過去の受信情報" : "過去の受信情報") :
      entry.badge, center, 125);
    M5.Display.setTextColor(TFT_WHITE);
    drawCategoryIcon(entry.title, 79, 190, accent);
    M5.Display.setTextSize(2);
    drawBody(entry.title, center, 174, 350, 1, 38);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString(entry.ageStatus, center, 208);
    M5.Display.drawFastHLine(80, 237, 308, 0x4208);
    drawOverviewBody(entry.body, center);
    M5.Display.setTextSize(1);
    M5.Display.drawCenterString(String(selected + 1) + "/" + String(count) +
                                "  A:前 B:次", center, 405);
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawCenterString("通知音:" + String(soundLabels[soundLevel]), center, 365);
  M5.Display.drawCenterString(notificationStatus(), center, 433);
}
void drawEvidence() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("自動通知の保存記録", center, 48);
  M5.Display.drawCenterString("研究・実験用", center, 75);
  if (!evidenceStorageReady)
    M5.Display.drawCenterString("記録領域を開けません", center, 180);
  else if (evidenceReadFailed)
    M5.Display.drawCenterString("保存記録を検証できません", center, 180);
  else if (!evidenceAvailable)
    M5.Display.drawCenterString("通知記録はありません", center, 180);
  else {
    M5.Display.drawCenterString(lastEvidence.source == 2 ?
      "試験データ・衛星通報ではありません" : "実衛星通報・通知指示", center, 103);
    char line[48];
    snprintf(line, sizeof(line), "UTC秒 %lld",
      static_cast<long long>(lastEvidence.receivedUtc));
    M5.Display.drawCenterString(line, center, 130);
    if (lastEvidence.version == 2)
      snprintf(line, sizeof(line), "MT44 発生時刻差 %ld秒 A1=%u",
        static_cast<long>(lastEvidence.ageSeconds),
        static_cast<unsigned>(lastEvidence.issue));
    else
      snprintf(line, sizeof(line), "発表から %ld秒 区分 %u 種別 %u",
        static_cast<long>(lastEvidence.ageSeconds),
        static_cast<unsigned>(lastEvidence.classification),
        static_cast<unsigned>(lastEvidence.issue));
    M5.Display.drawCenterString(line, center, 157);
    snprintf(line, sizeof(line), "ID %016llX",
      static_cast<unsigned long long>(lastEvidence.id));
    M5.Display.drawCenterString(line, center, 184);
    M5.Display.drawCenterString("L1S 32バイト（16進数）", center, 219);
    for (int row = 0; row < 4; ++row) {
      char hex[17];
      for (int column = 0; column < 8; ++column)
        snprintf(hex + column * 2, 3, "%02X",
          static_cast<unsigned>(lastEvidence.l1s[row * 8 + column]));
      M5.Display.drawCenterString(hex, center, 247 + row * 25);
    }
  }
  if (evidenceSaveFailed)
    M5.Display.drawCenterString("最新通知の診断保存に失敗", center, 365);
  M5.Display.drawCenterString("A+B短押し: 画面切替", center, 405);
}
void drawSoundSettings() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("通知音の設定", center, 80);
  M5.Display.setTextSize(2);
  M5.Display.drawCenterString(soundLabels[soundLevel], center, 160);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("A:小さく  B:大きく", center, 250);
  M5.Display.drawCenterString("消音中も振動で通知", center, 290);
  if (settingsSaveFailed || !settingsStorageReady)
    M5.Display.drawCenterString("設定を保存できません", center, 342);
  M5.Display.drawCenterString("A+B短押し: 画面切替", center, 405);
}
void draw() {
#if QZSS_DCX_TEST
  drawTextView();
  dirty = false;
  return;
#endif
#if QZSS_LIFECYCLE_TEST
  drawOverview();
  dirty = false;
  return;
#endif
  if (viewMode == 1) drawTextView();
  else if (viewMode == 2) drawEvidence();
  else if (viewMode == 3) drawSoundSettings();
  else drawOverview();
  dirty = false;
}
#if QZSS_NOTIFICATION_TEST
constexpr uint64_t testMessageId = 0x515a535354455354ULL;
int testStage = 0;
String testResult = "B: 試験を実行";
bool testResetLatched = false;
void drawNotificationTest() {
  M5.Display.fillScreen(0x2100);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString("通知試験モード", 234, 55);
  M5.Display.drawCenterString("衛星通報ではありません", 234, 88);
  M5.Display.drawFastHLine(80, 118, 308, TFT_WHITE);
  M5.Display.setTextSize(1.5f);
  static const char* labels[] = {
    "1 訓練・試験", "2 古い通報", "3 新着DCR模擬", "4 重複・再起動"
  };
  M5.Display.drawCenterString(labels[testStage], 234, 166);
  M5.Display.setTextSize(1);
  M5.Display.drawCenterString(testResult, 234, 245);
  M5.Display.drawCenterString(alertStorageReady ? "試験用の保存領域" : "保存不可・通知無効", 234, 301);
  M5.Display.drawCenterString(evidenceSaveFailed ? "診断記録の保存失敗" :
    evidenceReadFailed ? "診断記録の検証失敗" :
    evidenceAvailable ? "診断記録あり（試験用）" : "診断記録なし", 234, 332);
  M5.Display.drawCenterString("A: 次  B: 実行", 234, 364);
  M5.Display.drawCenterString("A+B 2秒: 試験履歴消去", 234, 396);
}
void runNotificationTest() {
  if (!alertStorageReady) { testResult = "保存不可・通知無効"; return; }
  if (testStage == 3 && alertLedger.ids[0] != testMessageId) {
    testResult = "先に3を実行してください";
    return;
  }
  const bool training = testStage == 0;
  const bool stale = testStage == 1;
  const bool cue = considerAutomaticAlert(testMessageId, true, !stale,
    training ? 7 : 1, 0, true);
  if (cue) {
    startAutomaticCue();
    // Synthetic bytes are isolated from normal evidence and never labeled satellite data.
    uint8_t synthetic[32] = {};
    synthetic[0] = 0x51;
    synthetic[1] = 0x5a;
    saveAlertEvidence(synthetic, 60, 0, 1, 0);
  }
  testResult = cue ? "試験通知: 音・振動あり" : "試験通知: 音・振動なし";
}
void loopNotificationTest() {
  M5.update();
  if (vibrationEndsAt && static_cast<int32_t>(millis() - vibrationEndsAt) >= 0) {
    M5.Power.setVibration(0);
    vibrationEndsAt = 0;
  }
  const bool both = M5.BtnA.isPressed() && M5.BtnB.isPressed();
  if (both && M5.BtnA.pressedFor(1800) && M5.BtnB.pressedFor(1800)) {
    if (!testResetLatched) {
      if (alertStorageReady && alertPrefs.clear()) {
        alertLedger = {};
        alertLedger.version = 1;
        if (evidenceStorageReady && evidencePrefs.clear()) {
          lastEvidence = {};
          evidenceAvailable = false;
          evidenceSaveFailed = false;
          evidenceReadFailed = false;
        } else evidenceSaveFailed = true;
        testStage = 0;
        testResult = "試験履歴を消去";
      } else testResult = "試験履歴の消去失敗";
      testResetLatched = true;
      dirty = true;
    }
  } else if (!both) testResetLatched = false;
  if (!both && M5.BtnA.wasPressed()) {
    testStage = (testStage + 1) % 4;
    testResult = "B: 試験を実行";
    dirty = true;
  }
  if (!both && M5.BtnB.wasPressed()) { runNotificationTest(); dirty = true; }
  if (dirty || millis() - lastDraw >= 1000) {
    drawNotificationTest(); dirty = false; lastDraw = millis();
  }
  delay(1);
}
#endif
#if QZSS_DECODER_TEST
void drawDecoderTest() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.drawCenterString("保存ログ比較・試験", 234, 43);
  M5.Display.drawCenterString("現在の衛星通報ではありません", 234, 69);
  M5.Display.drawFastHLine(75, 101, 318, 0x8410);
  if (!count) {
    M5.Display.drawCenterString("復号できませんでした", 234, 190);
    return;
  }
  M5.Display.drawCenterString(history[selected].title, 234, 113);
  int start = 0, row = 0;
  const String& body = history[selected].body;
  while (start < body.length() && row < 10) {
    int end = body.indexOf('\n', start);
    if (end < 0) end = body.length();
    String line = body.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.isEmpty()) continue;
    while (line.length() && M5.Display.textWidth(line) > 380) {
      int last = line.length() - 1;
      while (last > 0 && (static_cast<uint8_t>(line[last]) & 0xc0) == 0x80) --last;
      line.remove(last);
    }
    M5.Display.drawCenterString(line, 234, 145 + row * 24);
    ++row;
  }
  M5.Display.drawCenterString(String(selected + 1) + "/" + String(count) +
                              "  A:前 B:次", 234, 423);
}
#endif
}

void setup() {
  M5.begin(M5.config());
  M5.Power.setVibration(0);
#if QZSS_DCX_TEST
  M5.Speaker.setVolume(soundVolumes[3]);
  draw();
  return; // Synthetic frames only: no USB host, NVS, or alert output.
#endif
  initSoundSettings();
#if QZSS_DECODER_TEST
  dcr.SetYear(2026); // Year of the saved frame, normally supplied by GNSS RMC.
  for (int i = 3; i >= 0; --i)
    for (uint8_t value : comparisonFrames[i]) parse(value);
  drawDecoderTest();
  return;
#endif
#if QZSS_LIFECYCLE_TEST
  dcr.SetYear(2026);
  draw();
  return; // No USB host and no NVS alert ledger in this isolated test mode.
#endif
  initAlertStorage();
  initEvidenceStorage();
#if QZSS_NOTIFICATION_TEST
  testStage = alertLedger.ids[0] == testMessageId ? 3 : 0;
  drawNotificationTest();
  return;
#endif
  draw();
  usb.onDeviceConnected([](const EspUsbHostDeviceInfo& device) {
    if (device.vid == 0x1a86 && device.pid == 0x7523) ch340Seen = true;
  });
  usb.onDeviceDisconnected([](const EspUsbHostDeviceInfo& device) {
    if (device.vid == 0x1a86 && device.pid == 0x7523) {
      ch340Seen = false;
      lastByte = 0;
      lastValidL1s = 0;
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
#if QZSS_DCX_TEST
  M5.update();
  if (M5.BtnA.wasPressed()) { dcxStage = (dcxStage + 1) % 6; dirty = true; }
  if (M5.BtnB.wasPressed()) {
    processSfrbx(dcxFrames[dcxStage], sizeof(dcxFrames[0]));
    dirty = true;
  }
  if (dirty || millis() - lastDraw >= 1000) { draw(); lastDraw = millis(); }
  delay(1);
  return;
#endif
#if QZSS_DECODER_TEST
  M5.update();
  if (M5.BtnA.wasPressed() && count) { selected = (selected + count - 1) % count; drawDecoderTest(); }
  if (M5.BtnB.wasPressed() && count) { selected = (selected + 1) % count; drawDecoderTest(); }
  delay(100);
  return;
#endif
#if QZSS_LIFECYCLE_TEST
  M5.update();
  if (M5.BtnA.wasPressed()) { lifecycleStage = (lifecycleStage + 1) % 6; dirty = true; }
  if (M5.BtnB.wasPressed()) {
    processSfrbx(lifecycleFrames[lifecycleStage], sizeof(lifecycleFrames[0]));
    dirty = true;
  }
  if (dirty || millis() - lastDraw >= 1000) { draw(); lastDraw = millis(); }
  delay(1);
  return;
#endif
#if QZSS_NOTIFICATION_TEST
  loopNotificationTest();
  return;
#endif
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
    if (!buttonChordLong) { viewMode = (viewMode + 1) % 4; dirty = true; }
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
  if (!buttonChord && M5.BtnA.wasPressed()) {
    if (viewMode == 3) changeSoundLevel(-1);
    else if (count) { selected = (selected + count - 1) % count; dirty = true; }
  }
  if (!buttonChord && M5.BtnB.wasPressed()) {
    if (viewMode == 3) changeSoundLevel(1);
    else if (count) { selected = (selected + 1) % count; dirty = true; }
  }
  if (dirty || millis() - lastDraw >= 1000) { draw(); lastDraw = millis(); }
  delay(1);
}
