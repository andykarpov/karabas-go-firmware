

#pragma once

#include <Arduino.h>
#ifdef ARDUINO_ARCH_RP2040
#include <api/HardwareSPI.h>

#include "Rp2040Spi.pio.h"

class PioSpi : public arduino::HardwareSPI {
 public:
  PioSpi(pin_size_t rx, pin_size_t sck, pin_size_t tx)
      : _rxPin(rx), _sckPin(sck), _txPin(tx) {}

  PioSpi() : _rxPin(MISO), _sckPin(SCK), _txPin(MOSI) {}
  ~PioSpi();
  
  // check if resources were allocated for pio state machines. 
  operator bool() { return _pio; }
  
  virtual void begin() override;
  virtual void end() override;

  void beginTransaction(SPISettings settings) override;
  void endTransaction(void) override;

  byte transfer(uint8_t data) override;
  uint16_t transfer16(uint16_t data) override;
  void transfer(void *buf, size_t count) { transfer(buf, buf, count); }
  void transfer(const void *txbuf, void *rxbuf, size_t count) override;

  // Unimplemented
  virtual void usingInterrupt(int interruptNumber) override {
    (void)interruptNumber;
  }
  virtual void notUsingInterrupt(int interruptNumber) override {
    (void)interruptNumber;
  }
  virtual void attachInterrupt() override { /* noop */
  }
  virtual void detachInterrupt() override { /* noop */
  }


 private:
  SPISettings _settings = SPISettings();
  pio_program_t _rxProg;
  uint16_t _rxInst[sizeof(rx_gpio_program_instructions)];
  pin_size_t _rxPin;
  pin_size_t _sckPin;
  pin_size_t _txPin;
  PIO _pio = nullptr;
  int _rxSm = -1;
  int _txSm = -1;
  int _rxOff = -1;
  int _txOffCpha0 = -1;
  int _txOffCpha1 = -1;
  io_rw_8 *_txFifo;
  io_rw_8 *_rxFifo;
  io_rw_32 *_fstat;
  uint32_t _rxFifoEmpty;

  bool claimSm(PIO pio);
  void freeSm(PIO pio);
  void init(pin_size_t rx, pin_size_t sck, pin_size_t tx);
  __force_inline void waitWhileRxFifoEmpty() {
    while (*_fstat & _rxFifoEmpty) {
    }
  }
};
#endif  // ARDUINO_ARCH_RP2040
