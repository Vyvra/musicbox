// Shared helper: mount a FAT image through the same adapter the board uses.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../../src/usb_fs.h"

struct Mounted {
  std::vector<uint8_t> image;
  mbx::UsbStorage storage;
  bool ok = false;

  explicit Mounted(const char* path) {
    FILE* fh = fopen(path, "rb");
    if (!fh) return;
    fseek(fh, 0, SEEK_END);
    long size = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    image.resize((size_t)size);
    if (fread(image.data(), 1, image.size(), fh) != image.size()) {
      fclose(fh);
      return;
    }
    fclose(fh);
    ok = storage.mount([this](uint32_t lba, uint8_t* buf, uint32_t count) -> bool {
      size_t offset = (size_t)lba * 512, length = (size_t)count * 512;
      if (offset + length > image.size()) return false;
      memcpy(buf, image.data() + offset, length);
      return true;
    });
  }
};
