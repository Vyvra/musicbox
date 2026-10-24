// Read-only FAT16/FAT32 reader.
//
// TinyUSB's MSC host class hands out raw 512-byte blocks; it has no idea what a file is.
// audioI2S and our scanner want paths and reads, so something has to understand the
// filesystem. This is that something.
//
// Deliberately free of Arduino, ESP-IDF and C++23: it is plain C++17 over a block-read
// callback, which means the exact same source can be compiled for the host and tested
// against real images built by mkfs.vfat - the only part of this port that can be proven
// without a board.
//
// READ-ONLY and deliberately so: the box must never write to a child's music stick.
//
// ⚠️ FAT only. exFAT is a different, much larger filesystem and is NOT supported -
// a stick formatted exFAT must be reformatted as FAT32 to work here.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fat {

using BlockReader = std::function<bool(uint32_t lba, uint8_t* buffer, uint32_t count)>;

struct Entry {
  std::string name;          // long name when the image has one, else 8.3
  bool        isDir = false;
  uint32_t    firstCluster = 0;
  uint32_t    size = 0;
};

class Volume {
 public:
  // `blockSize` must match what the MSC device reports (512 in practice).
  bool mount(const BlockReader& reader, uint32_t blockSize = 512);
  bool mounted() const { return mounted_; }

  const char* type() const { return type_; }        // "FAT16" / "FAT32"
  uint32_t volumeStartLba() const { return startLba_; }  // 0 for a superfloppy
  std::string label() const { return label_; }
  uint32_t    clusterBytes() const { return clusterBytes_; }

  // Non-recursive listing. Path "" or "/" means the root directory.
  bool list(const std::string& path, std::vector<Entry>& out) const;

  // Resolve a full path to its entry (works for files and directories).
  bool stat(const std::string& path, Entry& out) const;

  // Read a whole file into `out`.
  bool readFile(const std::string& path, std::string& out) const;

  // Random-access read, which is what a File implementation needs. Returns the number of
  // bytes produced (0 at end of file).
  uint32_t readAt(const Entry& file, uint32_t offset, uint8_t* buffer,
                  uint32_t length) const;

 private:
  bool parseBootSector(const uint8_t* boot);
  bool readFatEntry(uint32_t cluster, uint32_t& next) const;
  bool readCluster(uint32_t cluster, uint8_t* buffer) const;
  bool readDirCluster(uint32_t cluster, std::vector<Entry>& out) const;
  bool findInDir(uint32_t dirCluster, const std::string& name, Entry& out) const;
  bool findInRootDir(const std::string& name, Entry& out) const;
  bool readFixedRootDir(std::vector<Entry>& out) const;
  bool parseDirBuffer(const uint8_t* data, size_t length, std::u16string& lfn,
                      std::vector<Entry>& out) const;
  bool isEndOfChain(uint32_t cluster) const;

  BlockReader base_;                 // the caller's reader, unoffset
  BlockReader reader_;               // base_ plus the partition offset
  uint32_t    startLba_ = 0;         // first sector of the volume
  bool        mounted_ = false;
  uint32_t    blockSize_ = 512;

  uint16_t    bytesPerSector_ = 0;
  uint8_t     sectorsPerCluster_ = 0;
  uint32_t    reservedSectors_ = 0;
  uint8_t     fatCount_ = 0;
  uint32_t    fatSizeSectors_ = 0;
  uint32_t    rootEntryCount_ = 0;      // FAT16 only; 0 for FAT32
  uint32_t    totalSectors_ = 0;
  uint32_t    rootDirSectors_ = 0;
  uint32_t    firstDataSector_ = 0;
  uint32_t    clusterCount_ = 0;
  uint32_t    rootCluster_ = 0;         // FAT32 only
  uint32_t    clusterBytes_ = 0;
  uint32_t    fatStartSector_ = 0;
  const char* type_ = "none";
  std::string label_;
};

}  // namespace fat
