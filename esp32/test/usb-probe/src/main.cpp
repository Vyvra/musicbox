// Does ESP-IDF's own USB host stack see a device on the native port?
//
// Arduino's TinyUSB host reports nothing at all when a stick is inserted. That could equally be a
// driver-initialisation problem or an electrical one, and TinyUSB's debug output is compiled into a
// prebuilt archive so it cannot be switched on. IDF's usb_host is a separate implementation, so it
// splits the two cases without any hardware work.
#include <Arduino.h>
#include <usb/usb_host.h>

static void client_task(void* arg) {
  usb_host_client_handle_t client;
  usb_host_client_config_t cfg = {};
  cfg.is_synchronous = false;
  cfg.async.client_event_callback = [](const usb_host_client_event_msg_t* msg, void* arg) {
    switch (msg->event) {
      case USB_HOST_CLIENT_EVENT_NEW_DEV:
        Serial.printf("[idf] NEW_DEV addr=%u\n", msg->new_dev.address);
        break;
      case USB_HOST_CLIENT_EVENT_DEV_GONE:
        Serial.println("[idf] DEV_GONE");
        break;
      default: break;
    }
  };
  esp_err_t err = usb_host_client_register(&cfg, &client);
  Serial.printf("[idf] client register: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) { vTaskDelete(NULL); return; }

  while (true) {
    usb_host_client_handle_events(client, portMAX_DELAY);
  }
}

static void lib_task(void* arg) {
  uint32_t last_report = 0;
  while (true) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(500), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
      Serial.println("[idf] no clients");
    }
    if (millis() - last_report > 3000) {
      last_report = millis();
      usb_host_lib_info_t info = {};
      if (usb_host_lib_info(&info) == ESP_OK) {
        Serial.printf("[idf] devices=%d clients=%d\n", info.num_devices, info.num_clients);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println();
  Serial.println("[idf] USB host probe (ESP-IDF stack, independent of TinyUSB)");

  usb_host_config_t cfg = {};
  cfg.intr_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.skip_phy_setup = false;
  esp_err_t err = usb_host_install(&cfg);
  Serial.printf("[idf] host install: %s\n", esp_err_to_name(err));
  if (err != ESP_OK) {
    Serial.println("[idf] cannot continue - another USB driver probably owns the PHY");
    return;
  }

  xTaskCreate(lib_task, "usb_lib", 4096, nullptr, 2, nullptr);
  xTaskCreate(client_task, "usb_cli", 4096, nullptr, 3, nullptr);
  Serial.println("[idf] waiting for a device - insert the stick now");
}

void loop() {
  delay(1000);
}
