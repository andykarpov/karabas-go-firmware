#include "EspSerial.h"
#include "CoreMutex.h"
#include <hardware/gpio.h>
#include <map>
#include <GyverFIFO.h>

EspSerial::EspSerial() {

}

EspSerial::~EspSerial() {
    end();
}

void EspSerial::begin(unsigned long baud) {
    _running = true;
    rx_fifo.clear();
    tx_fifo.clear();
}

void EspSerial::begin(unsigned long baud, uint16_t config) {
    _running = true;
    rx_fifo.clear();
    tx_fifo.clear();
}

void EspSerial::end() {
    _running = false;
    rx_fifo.clear();
    tx_fifo.clear();
}

int EspSerial::peek() {
    if (!_running) {
        return -1;
    }
    if (rx_fifo.available()) {
        return rx_fifo.peek();
    }
    return -1;
}

int EspSerial::read() {
    if (!_running) {
        return -1;
    }
    if (rx_fifo.available()) {
        return rx_fifo.read();
    }
    return -1;
}

bool EspSerial::overflow() {
    if (!_running) {
        return false;
    }
    return !rx_fifo.availableForWrite();
}

int EspSerial::available() {
    if (!_running) {
        return 0;
    }
    return rx_fifo.available();
}

int EspSerial::availableForWrite() {
    if (!_running) {
        return 0;
    }
    return 4096 - tx_fifo.available();
}

void EspSerial::flush() {
    // todo ?
}

size_t EspSerial::write(uint8_t c) {
    if (!_running) {
        return 0;
    }

    if (tx_fifo.availableForWrite()) {
        tx_fifo.write(c);
    }

    return 1;
}

EspSerial::operator bool() {
    return _running;
}

void EspSerial::rx_queue_push(uint8_t c) {
    if (_running && rx_fifo.availableForWrite()) {
        rx_fifo.write(c);
    }
}

int EspSerial::tx_queue_pull() {
    if (_running && tx_fifo.available()) {
        return tx_fifo.read();
    }
    return -1;
}
