#pragma once

#include <Arduino.h>
#include "api/HardwareSerial.h"
#include <stdarg.h>
#include <queue>
#include <hardware/uart.h>
#include <CoreMutex.h>
#include <GyverFIFO.h>

extern "C" typedef struct uart_inst uart_inst_t;

class EspSerial : public arduino::HardwareSerial {
    public:
        EspSerial();
        ~EspSerial();

        void begin(unsigned long baud = 115200) override;
        void begin(unsigned long baud, uint16_t config) override;
        void end() override;

        virtual int peek() override;
        virtual int read() override;
        virtual int available() override;
        virtual int availableForWrite() override;
        virtual void flush() override;
        virtual size_t write(uint8_t c) override;
        bool overflow();
        using Print::write;
        operator bool() override;

        // spi to/from fifo
        void rx_queue_push(uint8_t c);
        int tx_queue_pull();

    protected:
        bool _running = false;
        GyverFIFO<uint8_t, 4096> rx_fifo;
        GyverFIFO<uint8_t, 4096> tx_fifo;
};
