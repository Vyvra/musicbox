#include "fat_reader.h"
#include <cstring>
#include <cctype>

namespace fat {

// ------------------------------------------------------------------ little-endian helpers

static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t le32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

// Read `length` bytes from an arbitrary byte offset. Reads are performed in whole blocks
// because that is all the MSC device offers.
static bool readBytes(const BlockReader& reader, uint32_t blockSize, uint64_t offset,
                      uint8_t* out, uint32_t length) {
  while (length > 0) {
    uint32_t lba = (uint32_t)(offset / blockSize);
    uint32_t within = (uint32_t)(offset % blockSize);
    uint32_t chunk = blockSize - within;
    if (chunk > length) chunk = length;

    uint8_t block[4096];
    if (blockSize > sizeof(block)) return false;
    if (!reader(lba, block, 1)) return false;
    memcpy(out, block + within, chunk);

    out += chunk;
    offset += chunk;
    length -= chunk;
  }
  return true;
}

// ------------------------------------------------------------------ mount

bool Volume::mount(const BlockReader& reader, uint32_t blockSize) {
  reader_ = reader;
  blockSize_ = blockSize;
  mounted_ = false;

  uint8_t boot[512];
  if (!reader_(0, boot, 1)) return false;
  if (boot[510] != 0x55 || boot[511] != 0xAA) return false;   // not a boot sector

  bytesPerSector_    = le16(boot + 0x0B);
  sectorsPerCluster_ = boot[0x0D];
  reservedSectors_   = le16(boot + 0x0E);
  fatCount_          = boot[0x10];
  rootEntryCount_    = le16(boot + 0x11);
  totalSectors_      = le16(boot + 0x13);
  if (totalSectors_ == 0) totalSectors_ = le32(boot + 0x20);

  uint32_t fatSize16 = le16(boot + 0x16);
  fatSizeSectors_    = fatSize16 ? fatSize16 : le32(boot + 0x24);
  rootCluster_       = le32(boot + 0x2C);

  // The numbers a malformed sector would send us into the weeds with.
  if (bytesPerSector_ < 512 || bytesPerSector_ > 4096) return false;
  if (sectorsPerCluster_ == 0 || (sectorsPerCluster_ & (sectorsPerCluster_ - 1)) != 0) {
    return false;    // must be a power of two
  }
  if (fatCount_ == 0 || fatSizeSectors_ == 0) return false;

  fatStartSector_   = reservedSectors_;
  rootDirSectors_   = ((rootEntryCount_ * 32) + (bytesPerSector_ - 1)) / bytesPerSector_;
  firstDataSector_  = reservedSectors_ + (fatCount_ * fatSizeSectors_) + rootDirSectors_;
  if (totalSectors_ <= firstDataSector_) return false;

  clusterCount_  = (totalSectors_ - firstDataSector_) / sectorsPerCluster_;
  clusterBytes_  = (uint32_t)sectorsPerCluster_ * bytesPerSector_;

  // The classic (and only reliable) way to tell them apart: cluster count, not the
  // "FAT16"/"FAT32" string some formatters write, which is frequently wrong.
  if (clusterCount_ < 4085)       type_ = "FAT12";
  else if (clusterCount_ < 65525) type_ = "FAT16";
  else                            type_ = "FAT32";

  if (rootDirSectors_ == 0 && rootCluster_ == 0) return false;   // FAT32 without root

  // Volume label: the boot sector copy, falling back to nothing.
  const uint8_t* labelAt = (type_[3] == '3') ? boot + 0x47 : boot + 0x2B;
  char label[12];
  memcpy(label, labelAt, 11);
  label[11] = '\0';
  for (int i = 10; i >= 0 && label[i] == ' '; i--) label[i] = '\0';
  label_ = label;

  mounted_ = true;
  return true;
}

// ------------------------------------------------------------------ FAT traversal

bool Volume::readFatEntry(uint32_t cluster, uint32_t& next) const {
  uint8_t buf[4];
  if (type_[3] == '3') {                      // FAT32: 4 bytes per entry
    uint64_t off = (uint64_t)fatStartSector_ * bytesPerSector_ + (uint64_t)cluster * 4;
    if (!readBytes(reader_, blockSize_, off, buf, 4)) return false;
    next = le32(buf) & 0x0FFFFFFF;
  } else if (type_[3] == '2') {               // FAT12: 1.5 bytes per entry
    uint64_t off = (uint64_t)fatStartSector_ * bytesPerSector_ + (uint64_t)cluster * 3 / 2;
    if (!readBytes(reader_, blockSize_, off, buf, 2)) return false;
    uint16_t raw = le16(buf);
    next = (cluster & 1) ? (raw >> 4) : (raw & 0x0FFF);
  } else {                                    // FAT16: 2 bytes per entry
    uint64_t off = (uint64_t)fatStartSector_ * bytesPerSector_ + (uint64_t)cluster * 2;
    if (!readBytes(reader_, blockSize_, off, buf, 2)) return false;
    next = le16(buf);
  }
  return true;
}

bool Volume::isEndOfChain(uint32_t cluster) const {
  if (type_[3] == '3') return cluster >= 0x0FFFFFF8;
  if (type_[3] == '2') return cluster >= 0xFFF8;
  return cluster >= 0xFFF8;
}

bool Volume::readCluster(uint32_t cluster, uint8_t* buffer) const {
  uint64_t sector = firstDataSector_ + (uint64_t)(cluster - 2) * sectorsPerCluster_;
  uint64_t off = sector * bytesPerSector_;
  return readBytes(reader_, blockSize_, off, buffer, clusterBytes_);
}

// ------------------------------------------------------------------ directory parsing

// Length of a long-name fragment, and where in the name it belongs.
static int lfnChars(const uint8_t* e, uint16_t* out) {
  static const int kOffsets[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
  for (int i = 0; i < 13; i++) out[i] = (uint16_t)(e[kOffsets[i]] | (e[kOffsets[i] + 1] << 8));
  return 13;
}

static std::string utf16ToUtf8(const std::u16string& in) {
  std::string out;
  for (char16_t c : in) {
    // Long names are assembled from fragments that may not fill the last one, so the
    // buffer is NUL-padded; 0x0000 is the terminator in the format too.
    if (c == 0) break;
    if (c < 0x80) {
      out.push_back((char)c);
    } else if (c < 0x800) {
      out.push_back((char)(0xC0 | (c >> 6)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    } else {
      out.push_back((char)(0xE0 | (c >> 12)));
      out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

static std::string shortName(const uint8_t* e) {
  char base[9], ext[4];
  memcpy(base, e, 8);
  memcpy(ext, e + 8, 3);
  base[8] = '\0';
  ext[3] = '\0';
  for (int i = 7; i >= 0 && base[i] == ' '; i--) base[i] = '\0';
  for (int i = 2; i >= 0 && ext[i] == ' '; i--) ext[i] = '\0';
  if (base[0] == '\x05') base[0] = (char)0xE5;   // 0xE5 is escaped in the first byte
  std::string name = base;
  if (ext[0]) { name += "."; name += ext; }
  return name;
}

bool Volume::parseDirBuffer(const uint8_t* data, size_t length, std::u16string& lfn,
                            std::vector<Entry>& out) const {
  for (size_t off = 0; off + 32 <= length; off += 32) {
    const uint8_t* e = data + off;
    if (e[0] == 0x00) return true;              // end of directory
    if (e[0] == 0xE5) { lfn.clear(); continue; }  // deleted

    uint8_t attr = e[0x0B];
    if (attr == 0x0F) {                         // long-name fragment
      uint16_t chars[13];
      lfnChars(e, chars);
      int seq = e[0] & 0x1F;
      size_t base = (size_t)(seq - 1) * 13;
      if (lfn.size() < base + 13) lfn.resize(base + 13, 0);
      for (int i = 0; i < 13; i++) {
        uint16_t c = chars[i];
        if (c == 0x0000 || c == 0xFFFF) break;  // padding / terminator
        lfn[base + i] = (char16_t)c;
      }
      continue;
    }
    if (attr & 0x08) { lfn.clear(); continue; }   // volume label: not a file

    Entry entry;
    entry.isDir        = (attr & 0x10) != 0;
    entry.firstCluster = (uint32_t)le16(e + 0x1A) |
                         ((type_[3] == '3') ? ((uint32_t)le16(e + 0x14) << 16) : 0u);
    entry.size         = le32(e + 0x1C);
    entry.name         = lfn.empty() ? shortName(e) : utf16ToUtf8(lfn);
    lfn.clear();

    if (entry.name == "." || entry.name == "..") continue;
    out.push_back(entry);
  }
  return true;
}

bool Volume::readDirCluster(uint32_t cluster, std::vector<Entry>& out) const {
  std::vector<uint8_t> clusterData(clusterBytes_);
  std::u16string lfn;
  uint32_t current = cluster;
  uint32_t guard = clusterCount_ + 2;

  while (!isEndOfChain(current) && guard--) {
    if (!readCluster(current, clusterData.data())) return false;
    if (!parseDirBuffer(clusterData.data(), clusterData.size(), lfn, out)) return false;
    if (!readFatEntry(current, current)) return false;
  }
  return true;
}

bool Volume::list(const std::string& path, std::vector<Entry>& out) const {
  out.clear();
  if (!mounted_) return false;

  Entry dir;
  if (!stat(path, dir)) return false;
  if (!dir.isDir) return false;
  // On FAT12/16 the root directory is a fixed run of sectors, not a cluster chain, and
  // stat() marks it with cluster 0. Walking it as a chain would read cluster 0 - which
  // is metadata, not data - and produce garbage entries.
  if (type_[3] != '3' && dir.firstCluster == 0) return readFixedRootDir(out);
  return readDirCluster(dir.firstCluster, out);
}

bool Volume::findInDir(uint32_t dirCluster, const std::string& name, Entry& out) const {
  std::vector<Entry> entries;
  if (!readDirCluster(dirCluster, entries)) return false;

  auto equalIgnoreCase = [](const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
      if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
  };
  for (const Entry& e : entries) {
    if (equalIgnoreCase(e.name, name)) { out = e; return true; }
  }
  return false;
}

bool Volume::stat(const std::string& path, Entry& out) const {
  if (!mounted_) return false;
  out = Entry();
  out.isDir = true;
  out.firstCluster = (type_[3] == '3') ? rootCluster_ : 0;

  // Root of a FAT12/16 volume is a fixed region, not a cluster chain - cluster 0 is the
  // conventional marker and readDirCluster is never asked to walk it.
  std::string rest = path;
  while (!rest.empty() && rest.front() == '/') rest.erase(rest.begin());
  if (rest.empty()) return true;

  size_t pos = 0;
  while (pos <= rest.size()) {
    size_t slash = rest.find('/', pos);
    std::string part = rest.substr(pos, (slash == std::string::npos) ? std::string::npos
                                                                     : slash - pos);
    if (part.empty()) break;
    if (!out.isDir) return false;

    Entry next;
    if (out.firstCluster == 0 && type_[3] != '3') {
      // FAT12/16 root: scan the fixed root region. Reported to the caller as cluster 0
      // with isDir, and handled by treating it as the root cluster.
      if (!findInRootDir(part, next)) return false;
    } else {
      if (!findInDir(out.firstCluster, part, next)) return false;
    }
    out = next;
    if (slash == std::string::npos) break;
    pos = slash + 1;
  }
  return true;
}

bool Volume::readFixedRootDir(std::vector<Entry>& out) const {
  std::vector<uint8_t> data(rootDirSectors_ * bytesPerSector_);
  uint64_t off = ((uint64_t)reservedSectors_ + (uint64_t)fatCount_ * fatSizeSectors_) *
                 bytesPerSector_;
  if (!readBytes(reader_, blockSize_, off, data.data(), (uint32_t)data.size())) return false;
  std::u16string lfn;
  return parseDirBuffer(data.data(), data.size(), lfn, out);
}

bool Volume::findInRootDir(const std::string& name, Entry& out) const {
  std::vector<Entry> entries;
  if (!readFixedRootDir(entries)) return false;

  for (const Entry& e : entries) {
    if (e.name.size() == name.size()) {
      bool same = true;
      for (size_t i = 0; i < name.size(); i++) {
        if (tolower((unsigned char)e.name[i]) != tolower((unsigned char)name[i])) {
          same = false;
          break;
        }
      }
      if (same) { out = e; return true; }
    }
  }
  return false;
}

// ------------------------------------------------------------------ file reading

uint32_t Volume::readAt(const Entry& file, uint32_t offset, uint8_t* buffer,
                        uint32_t length) const {
  if (!mounted_ || file.isDir || offset >= file.size) return 0;
  if (offset + length > file.size) length = file.size - offset;

  uint32_t cluster = file.firstCluster;
  if (cluster < 2) return 0;

  // Skip whole clusters up to the offset.
  uint32_t skip = offset / clusterBytes_;
  while (skip-- > 0) {
    uint32_t next;
    if (!readFatEntry(cluster, next) || isEndOfChain(next)) return 0;
    cluster = next;
  }

  std::vector<uint8_t> clusterData(clusterBytes_);
  uint32_t within = offset % clusterBytes_;
  uint32_t produced = 0;

  while (produced < length) {
    if (!readCluster(cluster, clusterData.data())) break;
    uint32_t take = clusterBytes_ - within;
    if (take > length - produced) take = length - produced;
    memcpy(buffer + produced, clusterData.data() + within, take);
    produced += take;
    within = 0;
    if (produced >= length) break;

    uint32_t next;
    if (!readFatEntry(cluster, next) || isEndOfChain(next)) break;
    cluster = next;
  }
  return produced;
}

bool Volume::readFile(const std::string& path, std::string& out) const {
  Entry entry;
  if (!stat(path, entry) || entry.isDir) return false;
  out.assign(entry.size, '\0');
  if (entry.size == 0) return true;
  return readAt(entry, 0, (uint8_t*)&out[0], entry.size) == entry.size;
}

}  // namespace fat
