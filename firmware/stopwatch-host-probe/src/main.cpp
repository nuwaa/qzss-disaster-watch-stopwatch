#include <Arduino.h>
#include <M5Unified.h>
#include "usb/usb_host.h"

namespace {
usb_host_client_handle_t client = nullptr;
String state = "USBホスト初期化中";
String detail = "";
bool dirty = true;

void draw() {
  const int center = M5.Display.width() / 2;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::efontJA_16);
  M5.Display.drawCenterString("USBホスト接続試験", center, 100);
  M5.Display.drawCenterString("研究・実験用", center, 135);
  M5.Display.drawCenterString(state, center, 220);
  M5.Display.drawCenterString(detail, center, 255);
  M5.Display.drawCenterString("通報表示機能なし", center, 340);
  dirty = false;
}

void hostEvents(void*) {
  for (;;) {
    uint32_t events = 0;
    usb_host_lib_handle_events(portMAX_DELAY, &events);
  }
}

void clientEvent(const usb_host_client_event_msg_t* event, void*) {
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    usb_device_handle_t device = nullptr;
    if (usb_host_device_open(client, event->new_dev.address, &device) != ESP_OK) {
      state = "USB機器の読取失敗";
      dirty = true;
      return;
    }
    const usb_device_desc_t* descriptor = nullptr;
    if (usb_host_get_device_descriptor(device, &descriptor) == ESP_OK) {
      char id[20];
      snprintf(id, sizeof(id), "%04X:%04X", descriptor->idVendor, descriptor->idProduct);
      detail = id;
      if (descriptor->idVendor == 0x1A86 && descriptor->idProduct == 0x7523)
        state = "GNSS CH340 を認識";
      else if (descriptor->bDeviceClass == 0x09)
        state = "USBハブを認識";
      else
        state = "USB機器を認識";
    } else {
      state = "USB機器の識別失敗";
    }
    usb_host_device_close(client, device);
    dirty = true;
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    state = "USB機器を取り外しました";
    detail = "";
    dirty = true;
  }
}
}  // namespace

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  draw();
  usb_host_config_t hostConfig = {};
  const esp_err_t installed = usb_host_install(&hostConfig);
  if (installed != ESP_OK) {
    state = "USBホスト開始失敗";
    detail = String(static_cast<int>(installed));
    dirty = true;
    return;
  }
  xTaskCreate(hostEvents, "usb-host-events", 4096, nullptr, 5, nullptr);
  usb_host_client_config_t clientConfig = {};
  clientConfig.is_synchronous = false;
  clientConfig.max_num_event_msg = 5;
  clientConfig.async.client_event_callback = clientEvent;
  const esp_err_t registered = usb_host_client_register(&clientConfig, &client);
  if (registered != ESP_OK) {
    state = "USBクライアント失敗";
    detail = String(static_cast<int>(registered));
  } else {
    state = "USB機器接続待ち";
    detail = "ハブまたはGNSSを待機";
  }
  dirty = true;
}

void loop() {
  M5.update();
  if (client) usb_host_client_handle_events(client, pdMS_TO_TICKS(20));
  if (dirty) draw();
  delay(5);
}
