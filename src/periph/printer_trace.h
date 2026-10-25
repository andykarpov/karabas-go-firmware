#pragma once
#include <cstddef>
#include <cstdint>

// Each instance belongs to one task. Emit whole lines, not one log call per
// byte: this reduces log traffic and prevents RX/TX text from interleaving.
// Offsets reveal lost log lines. Hex preserves ESC, CR, LF and high-bit bytes.
class PrinterTrace {
 public:
  using Sink = void (*)(uint32_t, const char *);
  explicit PrinterTrace(Sink sink) : sink_(sink) {}
  void reset() { offset_ = 0; used_ = 0; }
  void append(const uint8_t *data, size_t length) {
    static constexpr char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
      const size_t pos = used_ * 3;
      hex_[pos] = digits[data[i] >> 4];
      hex_[pos + 1] = digits[data[i] & 15];
      hex_[pos + 2] = ' ';
      if (++used_ == 32) flush();
    }
  }
  void flush() {
    if (!used_) return;
    hex_[used_ * 3 - 1] = 0;
    sink_(offset_, hex_);
    offset_ += used_;
    used_ = 0;
  }
 private:
  Sink sink_;
  uint32_t offset_ = 0;
  size_t used_ = 0;
  char hex_[96]{};
};
