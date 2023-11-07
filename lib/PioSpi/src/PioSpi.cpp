#ifdef ARDUINO_ARCH_RP2040
#include "PioSpi.h"
// #define PIO_SPI_FAIL {Serial.print("PioSpi fail.");Serial.println(__LINE__);}
// #define PIO_SPI_ASSERT(test) {if (!test) PIO_SPI_FAIL;}
//------------------------------------------------------------------------------
PioSpi::~PioSpi() { end(); }
//------------------------------------------------------------------------------
void PioSpi::begin() {
  if (!_pio) {
    pio_make_rx_program(&_rxProg, _rxInst, _sckPin);
    if (!claimSm(pio0) && !claimSm(pio1)) {
      return;
    }
    beginTransaction(_settings);
  }
}
//------------------------------------------------------------------------------
void PioSpi::beginTransaction(SPISettings settings) {
  if (settings != _settings && _pio) {
    _settings = settings;
    uint f_sys = clock_get_hz(clk_sys);
    // Clock divisor will be multiple of 0.5.
    float clkDiv = 0.5 * ((f_sys / 2) / (_settings.getClockFreq() + 1) + 1);
    // Adjust for out of range.
    clkDiv = clkDiv < 1.0 ? 1.0 : clkDiv > 65535.0 ? 65535.0 : clkDiv;
    uint mask = (1ul << _rxSm) | (1ul << _txSm);
    pio_set_sm_mask_enabled(_pio, mask, false);
    // Use integer part for RX clock.
    pio_rx_program_init(_pio, _rxSm, _rxOff, _rxPin, int(clkDiv));
    pio_tx_cpha0_program_init(_pio, _txSm, _txOffCpha0, _txPin, _sckPin,
                              clkDiv);
    pio_set_sm_mask_enabled(_pio, mask, true);
  }
}
//------------------------------------------------------------------------------
bool PioSpi::claimSm(PIO pio) {
  _rxSm = pio_claim_unused_sm(pio, false);
  if (_rxSm < 0) goto fail;
  _txSm = pio_claim_unused_sm(pio, false);
  if (_txSm < 0) goto fail;
  _rxOff = pio_add_program(pio, &_rxProg);
  if (_rxOff < 0) goto fail;
  _txOffCpha0 = pio_add_program(pio, &tx_cpha0_program);
  if (_txOffCpha0 < 0) goto fail;
  _pio = pio;
  _txFifo = (io_rw_8 *)&pio->txf[_txSm];
  _rxFifo = (io_rw_8 *)&pio->rxf[_rxSm];
  _fstat = (io_rw_32 *)&pio->fstat;
  _rxFifoEmpty = (1u << (PIO_FSTAT_RXEMPTY_LSB + _rxSm));
  return true;

fail:
  freeSm(pio);
  return false;
}
//------------------------------------------------------------------------------
void PioSpi::end() {
  if (_pio) {
    freeSm(_pio);
    _pio = nullptr;
    gpio_set_function(_rxPin, GPIO_FUNC_SIO);
    gpio_set_function(_sckPin, GPIO_FUNC_SIO);
    gpio_set_function(_txPin, GPIO_FUNC_SIO);
  }
}
//------------------------------------------------------------------------------
void PioSpi::endTransaction() {}
//------------------------------------------------------------------------------
void PioSpi::freeSm(PIO pio) {
  if (_rxSm >= 0) {
    pio_sm_set_enabled(pio, _rxSm, false);
    pio_sm_unclaim(pio, _rxSm);
    _rxSm = -1;
  }
  if (_txSm >= 0) {
    pio_sm_set_enabled(pio, _txSm, false);
    pio_sm_unclaim(pio, _txSm);
    _txSm = -1;
  }
  if (_rxOff >= 0) {
    pio_remove_program(pio, &_rxProg, _rxOff);
    _rxOff = -1;
  }
  if (_txOffCpha0 >= 0) {
    pio_remove_program(pio, &tx_cpha0_program, _txOffCpha0);
    _txOffCpha0 = -1;
  }
}
//------------------------------------------------------------------------------
uint8_t __time_critical_func(PioSpi::transfer)(uint8_t data) {
  if (!_pio) {
    return 0XFF;
  }
  *_txFifo = data;
  waitWhileRxFifoEmpty();
  return *_rxFifo;
}
//------------------------------------------------------------------------------
uint16_t __time_critical_func(PioSpi::transfer16)(uint16_t data) { // rewrite ////////////
  union {
    uint16_t val;
    struct {
      uint8_t lsb;
      uint8_t msb;
    };
  } t;
  t.val = data;
  t.msb = transfer(t.msb);
  t.lsb = transfer(t.lsb);
  return t.val;
}
//------------------------------------------------------------------------------
void __time_critical_func(PioSpi::transfer)(const void *send, void *recv,
                                            size_t count) {
  const uint8_t *txBuf = (const uint8_t *)send;
  uint8_t *rxBuf = (uint8_t *)recv;
  size_t ir = 0;
  size_t it = 0;
  if (!_pio || (!send && !recv)) {
    return;
  }
  // Fill TX FIFO.
  for (; it < 8 && it < count; it++) {  // symbol for FIFO_DEPTH///////////////////////////////////
    *_txFifo = send ? txBuf[it] : 0XFF;
  }
  // Optimized loops end only, receive only and send/receive.
  if (!recv) {
    // Send only.
    for (; it < count; it++, ir++) {
      waitWhileRxFifoEmpty();
      (void)*_rxFifo;
      *_txFifo = txBuf[it];
    }
  } else if (!send) {
    // Receive only.
    for (; it < count; it++, ir++) {
      waitWhileRxFifoEmpty();
      rxBuf[ir] = *_rxFifo;
      *_txFifo = 0XFF;
    }
  } else {
    // Send and receive.
    for (; it < count; it++, ir++) {
      waitWhileRxFifoEmpty();
      rxBuf[ir] = *_rxFifo;
      *_txFifo = txBuf[it];
    }
  }
  // Finish receive.
  for (; ir < count; ir++) {
    waitWhileRxFifoEmpty();
    if (!recv) {
      (void)*_rxFifo;
    } else {
      rxBuf[ir] = *_rxFifo;
    }
  }
}

//==============================================================================
// private members


#endif  // ARDUINO_ARCH_RP2040