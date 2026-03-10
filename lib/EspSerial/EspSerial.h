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
    using m_cb = void (*)(uint8_t cmd, uint8_t addr, uint8_t data); // alias function pointer
    public:
        EspSerial();
        ~EspSerial();

        void begin(unsigned long baud = 115200) override;
        void begin(unsigned long baud, uint16_t config) override;
        void begin(m_cb act);
        void end() override;

        virtual int peek() override;
        virtual int read() override;
        virtual int available() override;
        virtual int availableForWrite() override;
        virtual void flush() override;
        virtual size_t write(uint8_t c) override;
        bool overflow();
        void handle();
        using Print::write;
        operator bool() override;

        // spi to/from fifo
        void rx_queue_push(uint8_t c);
        int tx_queue_pull();

        // rx/tx fifos
        GyverFIFO<uint8_t, 4096> rx_fifo;
        GyverFIFO<uint8_t, 4096> tx_fifo;

    protected:
        bool _running = false;

    private:
        m_cb _action;
};
