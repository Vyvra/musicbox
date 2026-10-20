// Minimal stand-ins for the Arduino FS types, so the USB filesystem adapter can be
// compiled and RUN on the host.
//
// Only the surface usb_fs.cpp actually touches is modelled, and the semantics come
// straight from the core's headers (FS.h / FSImpl.h) so the adapter is exercised against
// the same contract it will meet on the board.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>
#include <string>

typedef bool boolean;

class String {
 public:
  String() = default;
  String(const char* text) : value_(text ? text : "") {}
  String(const std::string& text) : value_(text) {}
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool operator==(const String& other) const { return value_ == other.value_; }
  std::string str() const { return value_; }

 private:
  std::string value_;
};

class Stream {};

namespace fs {

// The core declares SeekMode inside namespace fs.
enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };


class FileImpl {
 public:
  virtual ~FileImpl() {}
  virtual size_t write(const uint8_t* buf, size_t size) = 0;
  virtual size_t read(uint8_t* buf, size_t size) = 0;
  virtual void flush() = 0;
  virtual bool seek(uint32_t pos, SeekMode mode) = 0;
  virtual size_t position() const = 0;
  virtual size_t size() const = 0;
  virtual bool setBufferSize(size_t size) = 0;
  virtual void close() = 0;
  virtual time_t getLastWrite() = 0;
  virtual const char* path() const = 0;
  virtual const char* name() const = 0;
  virtual boolean isDirectory(void) = 0;
  virtual std::shared_ptr<FileImpl> openNextFile(const char* mode) = 0;
  virtual boolean seekDir(long position) = 0;
  virtual String getNextFileName(void) = 0;
  virtual String getNextFileName(bool* isDir) = 0;
  virtual void rewindDirectory(void) = 0;
  virtual operator bool() = 0;
};

typedef std::shared_ptr<FileImpl> FileImplPtr;

class FSImpl {
 public:
  virtual ~FSImpl() {}
  virtual FileImplPtr open(const char* path, const char* mode, const bool create) = 0;
  virtual bool exists(const char* path) = 0;
  virtual bool rename(const char* from, const char* to) = 0;
  virtual bool remove(const char* path) = 0;
  virtual bool mkdir(const char* path) = 0;
  virtual bool rmdir(const char* path) = 0;
};

typedef std::shared_ptr<FSImpl> FSImplPtr;

class File : public Stream {
 public:
  File() = default;
  explicit File(FileImplPtr impl) : impl_(impl) {}

  size_t read(uint8_t* buf, size_t size) { return impl_ ? impl_->read(buf, size) : 0; }
  size_t write(const uint8_t* buf, size_t size) {
    return impl_ ? impl_->write(buf, size) : 0;
  }
  bool seek(uint32_t pos, SeekMode mode = SeekSet) {
    return impl_ ? impl_->seek(pos, mode) : false;
  }
  size_t position() const { return impl_ ? impl_->position() : 0; }
  size_t size() const { return impl_ ? impl_->size() : 0; }
  void close() { if (impl_) impl_->close(); }
  const char* name() const { return impl_ ? impl_->name() : ""; }
  const char* path() const { return impl_ ? impl_->path() : ""; }
  boolean isDirectory(void) { return impl_ ? impl_->isDirectory() : false; }
  File openNextFile(const char* mode = "r") {
    return impl_ ? File(impl_->openNextFile(mode)) : File();
  }
  void rewindDirectory(void) { if (impl_) impl_->rewindDirectory(); }
  explicit operator bool() const { return impl_ && *impl_; }

  FileImplPtr impl() const { return impl_; }

 private:
  FileImplPtr impl_;
};

class FS : public Stream {
 public:
  FS() = default;
  explicit FS(FSImplPtr impl) : impl_(impl) {}

  File open(const char* path, const char* mode = "r", const bool create = false) {
    return impl_ ? File(impl_->open(path, mode, create)) : File();
  }
  bool exists(const char* path) { return impl_ ? impl_->exists(path) : false; }
  bool remove(const char* path) { return impl_ ? impl_->remove(path) : false; }
  bool mkdir(const char* path) { return impl_ ? impl_->mkdir(path) : false; }
  bool rename(const char* from, const char* to) {
    return impl_ ? impl_->rename(from, to) : false;
  }

 private:
  FSImplPtr impl_;
};

}  // namespace fs
