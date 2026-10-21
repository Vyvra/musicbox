// Fragmented-file test: the same source the firmware runs, against an image whose file
// chain deliberately jumps.
//
// Every other image in this harness allocates contiguously, which means the cluster-chain
// follower in fat_reader was never exercised across a discontinuity — and a used stick is
// full of them. `makeimage.py fragmented` frees an interior run of clusters and then writes a
// file across it, so the chain runs 41..64, jumps over a live file, and resumes at 85.
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

static const char* kPattern = "FRAGMENTED!";
static const size_t kRepetitions = 3000;

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: %s <fragmented.img>\n", argv[0]); return 2; }

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

  fat::Volume volume;
  if (!volume.mount(reader)) { printf("FAIL  mount\n"); return 1; }

  const size_t patternLen = strlen(kPattern);
  const size_t expected = patternLen * kRepetitions;

  fat::Entry entry;
  expect(volume.stat("/frag.mp3", entry), "stat the fragmented file");
  expect(entry.size == expected,
         "size matches what was written (" + std::to_string(expected) + " bytes)");

  // Reading the whole file walks the chain from start to end, including the jump.
  std::string body;
  expect(volume.readFile("/frag.mp3", body), "read the fragmented file");
  expect(body.size() == expected, "read length matches size");

  bool contentOk = (body.size() == expected);
  for (size_t i = 0; contentOk && i < expected; i++) {
    if (body[i] != kPattern[i % patternLen]) contentOk = false;
  }
  expect(contentOk, "content is byte-exact across the discontinuity");

  // Random access landing past the jump: the chain walk must still skip correctly.
  uint8_t buf[32];
  const size_t pastJump = 20000;    // past the 12 KB hole, still inside the 33 KB file
  uint32_t got = volume.readAt(entry, (uint32_t)pastJump, buf, sizeof(buf));
  bool offsetOk = (got == sizeof(buf));
  for (uint32_t i = 0; offsetOk && i < got; i++) {
    if ((char)buf[i] != kPattern[(pastJump + i) % patternLen]) offsetOk = false;
  }
  expect(offsetOk, "random access past the discontinuity is correct");

  // The last byte must be reachable - an off-by-one in the chain walk shows up here.
  got = volume.readAt(entry, (uint32_t)(expected - 1), buf, sizeof(buf));
  expect(got == 1 && (char)buf[0] == kPattern[(expected - 1) % patternLen],
         "final byte is readable");

  // The files that survived the hole must be intact too.
  std::string tail;
  expect(volume.readFile("/tail.mp3", tail) && tail.size() == 8000,
         "the file after the hole still reads");
  std::string blocker;
  expect(volume.readFile("/blocker.mp3", blocker) && blocker.size() == 14000,
         "the file before the hole still reads");

  printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL PASS", failures,
         failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
