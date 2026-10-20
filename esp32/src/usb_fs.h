// Bridge from the (host-tested) FAT reader to Arduino's fs::FS.
//
// audioI2S opens files through fs::FS / File, and the scanner in storage.cpp does too, so
// the USB stick only becomes usable once it speaks that interface. This layer is kept
// deliberately thin: it translates calls and does no FAT work itself, so the logic that
// the host tests cover is exactly the logic the board runs.
//
// Read-only on purpose - the box must never write to a child's music stick.
#pragma once
#include <FS.h>
#include <memory>
#include <string>
#include <vector>

#include "fat_reader.h"

namespace mbx {

// Holds the volume and exposes it as a mountable fs::FS.
class UsbStorage {
 public:
  UsbStorage();
  ~UsbStorage();

  // Call once the MSC device reports its capacity; the reader does the rest.
  bool mount(const fat::BlockReader& reader);
  void unmount();
  bool mounted() const;

  fs::FS& fs();
  const fat::Volume& volume() const { return volume_; }

 private:
  fat::Volume volume_;
  std::shared_ptr<fs::FSImpl> impl_;
  std::unique_ptr<fs::FS> fs_;
};

}  // namespace mbx
