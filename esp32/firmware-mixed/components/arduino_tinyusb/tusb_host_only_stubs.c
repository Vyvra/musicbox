// Host-only TinyUSB build: stub out the device-side application callbacks.
//
// The prebuilt libarduino_tinyusb.a contains BOTH stacks. Calling tuh_init() pulls in
// tusb.c, which references the device stack, whose class drivers in turn expect these
// application-supplied callbacks - normally provided by Arduino's USB library
// (USBMSC/USBHID/USBCDC/USBNet), which a host-only sketch never instantiates.
//
// Nothing here is ever reached: tud_init() is never called, so the device stack is dead
// code kept alive only by the linker. Returning "refuse" values makes that explicit.

#include <tusb.h>

// --- HID -------------------------------------------------------------------------------
uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) { return NULL; }
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t* buffer,
                               uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const* buffer,
                           uint16_t bufsize) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

// --- MSC --------------------------------------------------------------------------------
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
  (void)lun;
  *block_count = 0;
  *block_size = 0;
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { (void)lun; return false; }
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer,
                          uint32_t bufsize) {
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  return -1;
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer,
                           uint32_t bufsize) {
  (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
  return -1;
}
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer,
                        uint16_t bufsize) {
  (void)lun; (void)scsi_cmd; (void)buffer; (void)bufsize;
  return -1;
}

// --- DFU --------------------------------------------------------------------------------
void tud_dfu_download_cb(uint8_t alt, uint16_t block_num, uint8_t const* data,
                         uint16_t length) {
  (void)alt; (void)block_num; (void)data; (void)length;
}
uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state) {
  (void)alt; (void)state;
  return 0;
}
void tud_dfu_manifest_cb(uint8_t alt) { (void)alt; }
void tud_dfu_runtime_reboot_to_dfu_cb(void) {}

// --- Network (NCM/RNDIS) ----------------------------------------------------------------
bool tud_network_recv_cb(const uint8_t* src, uint16_t size) {
  (void)src; (void)size;
  return false;
}
