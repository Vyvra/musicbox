#include "usb_fs.h"
#include <FSImpl.h>   // FileImpl/FSImpl are only forward-declared in FS.h

#include <cstring>

namespace mbx {

// Arduino's FS types all live in namespace fs.
using fs::FileImpl;
using fs::FileImplPtr;
using fs::FSImpl;
using fs::SeekMode;

namespace {

std::string baseName(const std::string& path) {
  size_t slash = path.find_last_of('/');
  return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

std::string joinPath(const std::string& dir, const std::string& name) {
  if (dir.empty() || dir == "/") return "/" + name;
  return dir + "/" + name;
}

// One open file or directory on the FAT volume.
class FatFileImpl : public FileImpl {
 public:
  FatFileImpl(const fat::Volume* volume, fat::Entry entry, std::string path,
              std::string name, bool isDirectory)
      : volume_(volume), entry_(std::move(entry)), path_(std::move(path)),
        name_(std::move(name)), isDir_(isDirectory) {
    if (isDir_) {
      // Listed up front: the interface is an iterator with no notion of "reopen", and a
      // directory here is small enough that this costs nothing.
      volume_->list(path_, children_);
    }
  }

  // ---- reading
  size_t read(uint8_t* buf, size_t size) override {
    if (isDir_ || !volume_) return 0;
    uint32_t got = volume_->readAt(entry_, (uint32_t)pos_, buf, (uint32_t)size);
    pos_ += got;
    return got;
  }

  size_t write(const uint8_t*, size_t) override { return 0; }   // read-only
  void flush() override {}

  bool seek(uint32_t pos, SeekMode mode) override {
    if (isDir_) return false;
    // The parameter is unsigned, so a backwards seek arrives as a large unsigned value.
    // Do the maths signed, or "seek(-5, SeekCur)" wraps into a clamp to end-of-file -
    // which is exactly the bug this arithmetic fixes.
    int32_t delta = (int32_t)pos;
    int64_t target;
    if (mode == fs::SeekCur)      target = (int64_t)pos_ + delta;
    else if (mode == fs::SeekEnd) target = (int64_t)entry_.size + delta;
    else                          target = (int64_t)pos;
    if (target < 0) target = 0;
    // Seeking past the end is legal for streams; clamp and report success.
    if (target > (int64_t)entry_.size) target = (int64_t)entry_.size;
    pos_ = (size_t)target;
    return true;
  }

  size_t position() const override { return pos_; }
  size_t size() const override { return isDir_ ? 0 : entry_.size; }
  bool setBufferSize(size_t) override { return false; }
  void close() override {}
  time_t getLastWrite() override { return 0; }

  const char* path() const override { return path_.c_str(); }
  const char* name() const override { return name_.c_str(); }
  boolean isDirectory(void) override { return isDir_; }

  // ---- directory iteration
  FileImplPtr openNextFile(const char* mode) override {
    (void)mode;
    if (!isDir_ || nextChild_ >= children_.size()) return FileImplPtr();
    const fat::Entry& child = children_[nextChild_++];
    std::string childPath = joinPath(path_, child.name);
    return std::make_shared<FatFileImpl>(volume_, child, childPath, child.name,
                                         child.isDir);
  }

  boolean seekDir(long position) override {
    if (!isDir_ || position < 0) return false;
    nextChild_ = (size_t)position;
    return true;
  }

  String getNextFileName(void) override {
    bool ignored = false;
    return getNextFileName(&ignored);
  }

  String getNextFileName(bool* isDir) override {
    if (!isDir_ || nextChild_ >= children_.size()) return String();
    const fat::Entry& child = children_[nextChild_++];
    if (isDir) *isDir = child.isDir;
    return String(child.name.c_str());
  }

  void rewindDirectory(void) override { nextChild_ = 0; }
  operator bool() override { return volume_ != nullptr; }

 private:
  const fat::Volume* volume_;
  fat::Entry entry_;
  std::string path_;
  std::string name_;
  bool isDir_;
  size_t pos_ = 0;
  std::vector<fat::Entry> children_;
  size_t nextChild_ = 0;
};

class FatFSImpl : public FSImpl {
 public:
  explicit FatFSImpl(const fat::Volume* volume) : volume_(volume) {}

  FileImplPtr open(const char* path, const char* mode, const bool create) override {
    if (!volume_ || !volume_->mounted() || path == nullptr) return FileImplPtr();
    // Anything that could modify the stick is refused outright rather than half-honoured.
    const char* m = mode ? mode : "r";
    if (create || strchr(m, 'w') || strchr(m, 'a') || strchr(m, '+')) {
      return FileImplPtr();
    }

    fat::Entry entry;
    if (!volume_->stat(path, entry)) return FileImplPtr();
    std::string p(path);
    return std::make_shared<FatFileImpl>(volume_, entry, p, baseName(p), entry.isDir);
  }

  bool exists(const char* path) override {
    fat::Entry entry;
    return volume_ && volume_->stat(path, entry);
  }

  bool rename(const char*, const char*) override { return false; }
  bool remove(const char*) override { return false; }
  bool mkdir(const char*) override { return false; }
  bool rmdir(const char*) override { return false; }

 private:
  const fat::Volume* volume_;
};

}  // namespace

UsbStorage::UsbStorage() = default;
UsbStorage::~UsbStorage() = default;

bool UsbStorage::mount(const fat::BlockReader& reader) {
  if (!volume_.mount(reader)) return false;
  impl_ = std::make_shared<FatFSImpl>(&volume_);
  fs_ = std::make_unique<fs::FS>(impl_);
  return true;
}

void UsbStorage::unmount() {
  fs_.reset();
  impl_.reset();
  volume_ = fat::Volume();      // back to unmounted
}

bool UsbStorage::mounted() const { return volume_.mounted(); }

fs::FS& UsbStorage::fs() { return *fs_; }

}  // namespace mbx
