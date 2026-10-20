// Host test for the USB filesystem adapter (src/usb_fs.cpp), against a real FAT image.
//
// This is the layer that turns the tested FAT reader into something audioI2S can open:
// File semantics (read/seek/position/size), directory iteration, and the read-only
// refusals. It needs no hardware, and the Arduino FS types are stubbed from the same
// contract the core uses.
//
// Build (see run.sh): g++ -std=c++17 -I stubs -I ../../src test_fs_adapter.cpp
//                       ../../src/usb_fs.cpp ../../src/fat_reader.cpp
#include "../../src/usb_fs.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using fs::File;   // the core puts File in namespace fs

static int failures = 0;

static void expect(bool ok, const std::string& what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) failures++;
}

static std::string slurp(File& file) {
  std::string out;
  uint8_t buf[64];
  while (true) {
    size_t n = file.read(buf, sizeof(buf));
    if (n == 0) break;
    out.append((const char*)buf, n);
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: %s <image.img>\n", argv[0]); return 2; }
  std::ifstream in(argv[1], std::ios::binary);
  if (!in) { printf("cannot open %s\n", argv[1]); return 2; }
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());

  auto reader = [&image](uint32_t lba, uint8_t* buf, uint32_t count) -> bool {
    size_t offset = (size_t)lba * 512, length = (size_t)count * 512;
    if (offset + length > image.size()) return false;
    memcpy(buf, image.data() + offset, length);
    return true;
  };

  mbx::UsbStorage storage;
  expect(storage.mount(reader), "UsbStorage::mount");
  expect(storage.mounted(), "mounted() reports true");
  fs::FS& vfs = storage.fs();

  // ---- whole-file read, the path audioI2S takes for a local track
  File cfg = vfs.open("/musicbox.txt");
  expect((bool)cfg, "open /musicbox.txt through fs::FS");
  size_t declared = cfg.size();
  std::string body = slurp(cfg);
  expect(declared > 0, "size() is non-zero");
  expect(body.size() == declared, "read() returned exactly size() bytes");
  expect(body.find("shuffle=\"on\"") != std::string::npos, "file content intact");

  // ---- seeking, which is how resume-at-position works
  File track = vfs.open("/10 - Tenth Song.mp3");
  expect((bool)track, "open a track");
  uint8_t buf[16];
  expect(track.seek(1000, fs::SeekSet) && track.position() == 1000, "seek(Set) moves position");
  track.read(buf, sizeof(buf));
  expect(track.position() == 1016, "position advances by bytes read");
  track.seek(-5, fs::SeekCur);
  expect(track.position() == 1011, "seek(Cur) is relative");
  track.seek(0, fs::SeekEnd);
  expect(track.position() == track.size(), "seek(End) lands at the end");
  // past the end must clamp rather than wrap
  track.seek(track.size() + 100, fs::SeekSet);
  expect(track.position() == track.size(), "seek past end clamps");

  // ---- directory iteration, which is how scanMedia() finds tracks
  File dir = vfs.open("/");
  expect((bool)dir && dir.isDirectory(), "open root as a directory");
  int entries = 0;
  bool sawNested = false, sawTrack = false;
  for (File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    std::string name = entry.name();
    if (entry.isDirectory() && name == "Sub Folder") sawNested = true;
    if (name == "01 - First Song.mp3") sawTrack = true;
    entries++;
  }
  expect(entries >= 6, "root lists its entries");
  expect(sawTrack, "a file is visible via openNextFile()");
  expect(sawNested, "a subdirectory is visible and reports isDirectory()");

  // ---- nested path resolution
  File deep = vfs.open("/Sub Folder/deep track.mp3");
  expect((bool)deep && deep.size() == 200, "nested file opens with the right size");

  // ---- refusals: this volume is read-only on purpose
  expect(!vfs.open("/nope.mp3"), "missing file yields no handle");
  expect(!vfs.open("/musicbox.txt", "w"), "write mode is refused");
  expect(!vfs.open("/musicbox.txt", "a"), "append mode is refused");
  expect(vfs.exists("/musicbox.txt"), "exists() true for a real file");
  expect(!vfs.exists("/nope.mp3"), "exists() false for a missing file");

  // ---- unmount must invalidate the view rather than crash
  storage.unmount();
  expect(!storage.mounted(), "unmount() clears mounted()");
  expect(!fs::FS().open("/anything"), "an empty fs::FS yields nothing");

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
