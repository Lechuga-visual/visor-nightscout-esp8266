#pragma once

#include <stddef.h>
#include <stdint.h>

namespace nightscout_http {
enum class BodyError { None, Truncated, Chunk, Limit, Trailing };
inline const char* errorName(BodyError error) {
  switch (error) {
    case BodyError::Truncated: return "incomplete_body";
    case BodyError::Chunk: return "invalid_chunk";
    case BodyError::Limit: return "body_too_large";
    case BodyError::Trailing: return "trailing_data";
    default: return "none";
  }
}

// HTTPClient::getStream() exposes the wire body, including chunk framing.
// Decode it on demand with a fixed buffer; never retain the full response.
// Input supplies available() and timed readBytes(), including short reads.
template <typename Input> class BodyReader {
 public:
  static constexpr size_t MAX_BODY = 256 * 1024;
  BodyReader(Input& input, bool chunked, int length)
      : input_(input), chunked_(chunked), remaining_(length) {}

  int read() {
    if (error_ != BodyError::None || ended_) return -1;
    if (chunked_) {
      if (!chunkRemaining_ && !nextChunk()) return -1;
    } else if (remaining_ == 0) { ended_ = true; return -1; }
    if (bytes_ >= MAX_BODY) { error_ = BodyError::Limit; return -1; }
    const int value = raw();
    if (value < 0) {
      if (chunked_ || remaining_ > 0) error_ = BodyError::Truncated;
      ended_ = true;
      return -1;
    }
    if (chunked_) --chunkRemaining_;
    else if (remaining_ > 0) --remaining_;
    if (first_ < 0 && value != ' ' && value != '\r' && value != '\n' && value != '\t') first_ = value;
    ++bytes_;
    return value;
  }
  size_t readBytes(char* buffer, size_t length) {
    size_t used = 0;
    while (used < length) {
      const int value = read();
      if (value < 0) break;
      buffer[used++] = static_cast<char>(value);
    }
    return used;
  }
  // Validate the final chunk/trailers or Content-Length before committing data.
  bool finish() {
    size_t padding = 0;
    for (int value = read(); value >= 0; value = read()) {
      if (++padding > 128 || (value != ' ' && value != '\r' && value != '\n' && value != '\t')) {
        error_ = BodyError::Trailing;
        return false;
      }
    }
    return error_ == BodyError::None;
  }
  BodyError error() const { return error_; }
  size_t bytes() const { return bytes_; }
  int firstByte() const { return first_; }

 private:
  int raw() {
    if (at_ == used_) {
      size_t count = input_.available();
      if (!count) count = 1; // Wait for the next byte, not a full 512-byte block.
      if (count > sizeof(buffer_)) count = sizeof(buffer_);
      used_ = input_.readBytes(buffer_, count);
      at_ = 0;
      if (!used_) return -1;
    }
    return static_cast<unsigned char>(buffer_[at_++]);
  }
  bool crlf() {
    const int first = raw(), second = raw();
    if (first < 0 || second < 0) error_ = BodyError::Truncated;
    else if (first != '\r' || second != '\n') error_ = BodyError::Chunk;
    return error_ == BodyError::None;
  }
  bool nextChunk() {
    if (started_ && !crlf()) return false;
    started_ = true;
    size_t length = 0;
    bool digit = false, extension = false;
    for (size_t i = 0; i < 128; ++i) {
      const int value = raw();
      if (value < 0) { error_ = BodyError::Truncated; return false; }
      if (value == '\r') {
        const int lf = raw();
        if (lf != '\n' || !digit) {
          error_ = lf < 0 ? BodyError::Truncated : BodyError::Chunk;
          return false;
        }
        if (length > MAX_BODY - bytes_) { error_ = BodyError::Limit; return false; }
        chunkRemaining_ = length;
        if (length) return true;
        // Trailer lines are bounded and never interpreted as trusted headers.
        size_t lineLength = 0;
        for (size_t trailer = 0; trailer < 2048; ++trailer) {
          const int c = raw();
          if (c < 0) { error_ = BodyError::Truncated; return false; }
          if (c == '\r') {
            const int end = raw();
            if (end != '\n') { error_ = end < 0 ? BodyError::Truncated : BodyError::Chunk; return false; }
            if (!lineLength) { ended_ = true; return false; }
            lineLength = 0;
          } else if (c == '\n' || c < 32 || c > 126) {
            error_ = BodyError::Chunk; return false;
          } else ++lineLength;
        }
        error_ = BodyError::Limit; return false;
      }
      if (extension) {
        if (value < 32 || value > 126) { error_ = BodyError::Chunk; return false; }
        continue;
      }
      if (value == ';' && digit) { extension = true; continue; }
      const int hex = value >= '0' && value <= '9' ? value - '0' :
                      value >= 'a' && value <= 'f' ? value - 'a' + 10 :
                      value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
      if (hex < 0) { error_ = BodyError::Chunk; return false; }
      if (length > (MAX_BODY - static_cast<size_t>(hex)) / 16) { error_ = BodyError::Limit; return false; }
      length = length * 16 + hex;
      digit = true;
    }
    error_ = BodyError::Limit;
    return false;
  }
  Input& input_;
  bool chunked_, started_ = false, ended_ = false;
  int remaining_, first_ = -1;
  size_t chunkRemaining_ = 0, bytes_ = 0, at_ = 0, used_ = 0;
  BodyError error_ = BodyError::None;
  char buffer_[512];
};
}  // namespace nightscout_http
