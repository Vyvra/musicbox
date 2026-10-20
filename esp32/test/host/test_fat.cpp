// Host test for fat_reader: the same .cpp the firmware compiles, pointed at an image
// file. This is the only part of the ESP32 port that can be proven without hardware.
//
// Build (see run.sh):  g++ -std=c++17 -O2 -o test_fat test_fat.cpp ../../src/fat_reader.cpp
#include "../../src/fat_reader.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

static int failures = 0;

static void expect(bool ok, const std::string& what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
  if (!ok) failures++;
}

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: %s <image.img>\n", argv[0]); return 2; }
  const char* path = argv[1];

  std::ifstream file(path, std::ios::binary);
  if (!file) { printf("cannot open %s\n", path); return 2; }
  std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());

  // Block device: exactly what tuh_msc_read10 will hand over on the real board.
  auto reader = [&image](uint32_t lba, uint8_t* buf, uint32_t count) -> bool {
    size_t offset = (size_t)lba * 512;
    size_t length = (size_t)count * 512;
    if (offset + length > image.size()) return false;
    memcpy(buf, image.data() + offset, length);
    return true;
  };

  fat::Volume volume;
  if (!volume.mount(reader)) { printf("FAIL  mount\n"); return 1; }
  printf("      type=%s label='%s' cluster=%u bytes\n", volume.type(),
         volume.label().c_str(), volume.clusterBytes());

  std::vector<fat::Entry> root;
  expect(volume.list("/", root), "list root");

  auto has = [&root](const std::string& name, bool wantDir = false) {
    for (const auto& e : root) {
      if (e.name == name) return e.isDir == wantDir;
    }
    return false;
  };
  expect(has("musicbox.txt"), "root contains musicbox.txt");
  expect(has("01 - First Song.mp3"), "root contains '01 - First Song.mp3'");
  expect(has("Een bestand met een lange naam.mp3"), "long filename survives (LFN)");
  expect(has("Sub Folder", true), "root contains directory 'Sub Folder'");

  // Content, read through the cluster chain.
  std::string text;
  expect(volume.readFile("/musicbox.txt", text), "read musicbox.txt");
  expect(text.find("shuffle=\"on\"") != std::string::npos, "musicbox.txt content intact");

  std::string first;
  expect(volume.readFile("/01 - First Song.mp3", first), "read a track");
  expect(first.size() == 1000 && first.compare(0, 5, "FIRST") == 0,
         "track size and header correct");

  std::string deep;
  expect(volume.readFile("/Sub Folder/deep track.mp3", deep), "read nested file");
  expect(deep.compare(0, 4, "DEEP") == 0, "nested file content correct");

  std::string spare;
  expect(!volume.readFile("/nope.mp3", spare), "missing file reports failure");

  // Random access, which is what the File implementation will sit on.
  fat::Entry track;
  expect(volume.stat("/10 - Tenth Song.mp3", track), "stat a track");
  uint8_t buf[64];
  uint32_t n = volume.readAt(track, 1000, buf, sizeof(buf));
  bool matches = (n == sizeof(buf));
  for (uint32_t i = 0; i < n && matches; i++) {
    // "TENTH" repeats, so the byte at offset 1000 is 'TENTH'[1000 % 5]
    matches = buf[i] == "TENTH"[(1000 + i) % 5];
  }
  expect(matches, "readAt honours the offset across a cluster boundary");

  // Reading past the end must clamp, not crash or wrap.
  n = volume.readAt(track, track.size - 10, buf, sizeof(buf));
  expect(n == 10, "readAt clamps at end of file");

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
