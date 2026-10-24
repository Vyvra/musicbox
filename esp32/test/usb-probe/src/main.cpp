// Minimal check: does ESP-IDF's host library actually see a device on the port?
//
// Prints client events (NEW_DEV / DEV_GONE), the device count, and the address list, so "no device
// attached" can be told apart from "attached but our attach logic is wrong".
#include <Arduino.h>
#include <usb/usb_host.h>

static usb_host_client_handle_t g_client = nullptr;

static void onEvent(const usb_host_client_event_msg_t* msg, void*) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    Serial.printf("[probe] NEW_DEV addr=%u\n", msg->new_dev.address);
    usb_device_handle_t dev = nullptr;
    if (usb_host_device_open(g_client, msg->new_dev.address, &dev) == ESP_OK) {
      usb_device_info_t info = {};
      if (usb_host_device_info(dev, &info) == ESP_OK) {
        Serial.printf("[probe] opened: speed %d addrs %d\n", (int)info.speed,
                      (int)info.dev_addr);
      }
    } else {
      Serial.println("[probe] open failed");
    }
  } else if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
    Serial.println("[probe] DEV_GONE");
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("[probe] IDF host: event check");

  usb_host_config_t cfg = {};
  cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.skip_phy_setup = false;
  Serial.printf("[probe] install: %s\n", esp_err_to_name(usb_host_install(&cfg)));

  usb_host_client_config_t ccfg = {};
  ccfg.is_synchronous = false;
  ccfg.max_num_event_msg = 5;
  ccfg.async.client_event_callback = onEvent;
  ccfg.async.callback_arg = nullptr;
  Serial.printf("[probe] client: %s\n", esp_err_to_name(usb_host_client_register(&ccfg, &g_client)));
}

void loop() {
  if (g_client == nullptr) {
    delay(1000);
    return;
  }
  uint32_t flags = 0;
  usb_host_lib_handle_events(10, &flags);
  usb_host_client_handle_events(g_client, 10);

  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    usb_host_lib_info_t info = {};
    uint8_t addrs[8] = {};
    int count = -1;
    const esp_err_t err = usb_host_device_addr_list_fill(8, addrs, &count);
    if (usb_host_lib_info(&info) == ESP_OK) {
      Serial.printf("[probe] devices=%d clients=%d fill=%s count=%d raw=%u,%u,%u\n",
                    info.num_devices, info.num_clients, esp_err_to_name(err), count,
                    (unsigned)addrs[0], (unsigned)addrs[1], (unsigned)addrs[2]);
    }
    // The list comes back empty, so try opening known addresses directly.
    for (uint8_t a = 1; a <= 2; a++) {
      usb_device_handle_t dev = nullptr;
      const esp_err_t oerr = usb_host_device_open(g_client, a, &dev);
      Serial.printf("[probe] open addr %u -> %s\n", a, esp_err_to_name(oerr));
      if (oerr == ESP_OK) {
        usb_device_info_t di = {};
        if (usb_host_device_info(dev, &di) == ESP_OK) {
          Serial.printf("[probe]   info: speed %d\n", (int)di.speed);
        }
      }
    }
  }
}
