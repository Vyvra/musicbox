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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <memory>
#include <string>

typedef bool boolean;

class String {
 public:
  String() = default;
  String(const char* text) : value_(text ? text : "") {}
  String(const std::string& text) : value_(text) {}
  explicit String(unsigned int value) : value_(std::to_string(value)) {}
  explicit String(int value) : value_(std::to_string(value)) {}
  explicit String(unsigned long value) : value_(std::to_string(value)) {}
  // Templated so String(uint32_t, HEX) is an exact match for the integer form rather than
  // a tie with the float constructor - which is what Arduino's own overload set resolves to.
  template <typename T,
            typename = typename std::enable_if<std::is_integral<T>::value>::type>
  explicit String(T value, unsigned char base) {
    char buf[24];
    if (base == 16) snprintf(buf, sizeof(buf), "%lx", (unsigned long)value);
    else snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    value_ = buf;
  }
  explicit String(float value, unsigned char decimals) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimals, (double)value);
    value_ = buf;
  }

  void trim() {
    size_t begin = value_.find_first_not_of(" \t\r\n");
    size_t end = value_.find_last_not_of(" \t\r\n");
    value_ = (begin == std::string::npos) ? "" : value_.substr(begin, end - begin + 1);
  }
  void toLowerCase() {
    for (char& c : value_) c = (char)tolower((unsigned char)c);
  }
  bool startsWith(const char* prefix) const {
    return value_.rfind(prefix, 0) == 0;
  }
  bool endsWith(const char* suffix) const {
    std::string tail(suffix ? suffix : "");
    return value_.size() >= tail.size() &&
           value_.compare(value_.size() - tail.size(), tail.size(), tail) == 0;
  }
  int lastIndexOf(char needle) const {
    size_t at = value_.rfind(needle);
    return at == std::string::npos ? -1 : (int)at;
  }
  int indexOf(char needle, int from) const {
    if (from < 0) from = 0;
    size_t at = value_.find(needle, (size_t)from);
    return at == std::string::npos ? -1 : (int)at;
  }
  void remove(int index) {
    if (index >= 0 && (size_t)index <= value_.size()) value_.erase((size_t)index);
  }
  void remove(int index, int count) {
    if (index >= 0 && count > 0 && (size_t)index <= value_.size()) {
      value_.erase((size_t)index, (size_t)count);
    }
  }
  int indexOf(const char* needle) const {
    size_t at = value_.find(needle ? needle : "");
    return at == std::string::npos ? -1 : (int)at;
  }
  int indexOf(const String& needle) const { return indexOf(needle.value_.c_str()); }
  int indexOf(char needle) const {
    size_t at = value_.find(needle);
    return at == std::string::npos ? -1 : (int)at;
  }
  String substring(int from) const {
    if (from < 0 || (size_t)from > value_.size()) return String();
    return String(value_.substr((size_t)from));
  }
  String substring(int from, int to) const {
    if (from < 0 || to < from || (size_t)from > value_.size()) return String();
    size_t count = (size_t)(to - from);
    return String(value_.substr((size_t)from, count));
  }
  bool equalsIgnoreCase(const char* other) const {
    std::string a = value_, b = other ? other : "";
    for (char& c : a) c = (char)tolower((unsigned char)c);
    for (char& c : b) c = (char)tolower((unsigned char)c);
    return a == b;
  }
  float toFloat() const { return (float)atof(value_.c_str()); }
  char operator[](size_t i) const { return value_[i]; }
  const char* c_str() const { return value_.c_str(); }
  size_t length() const { return value_.size(); }
  bool operator==(const String& other) const { return value_ == other.value_; }
  // Arduino's String is concatenable from either side; the firmware relies on both.
  String& operator+=(const char* other) { value_ += (other ? other : ""); return *this; }
  String& operator+=(const String& other) { value_ += other.value_; return *this; }
  String& operator+=(char c) { value_ += c; return *this; }
  String operator+(const String& other) const { return String(value_ + other.value_); }
  String operator+(const char* other) const { return String(value_ + (other ? other : "")); }
  friend String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs ? lhs : "") + rhs.value_);
  }
  std::string str() const { return value_; }

 private:
  std::string value_;
};

class Stream {};

// The core's file mode constants.
#define HEX 16
#define DEC 10
#define FILE_READ  "r"
#define FILE_WRITE "w"
#define FILE_APPEND "a" 

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
  // For a read-only handle this is simply what is left.
  int available() { return impl_ ? (int)(impl_->size() - impl_->position()) : 0; }
  String readStringUntil(char terminator) {
    String out;
    uint8_t c;
    while (read(&c, 1) == 1) {
      if ((char)c == terminator) break;
      out += (char)c;
    }
    return out;
  }
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

// The core makes File/FS reachable unqualified; firmware sources rely on that.
using fs::File;
using fs::FS;
using fs::FSImpl;
using fs::FileImpl;
