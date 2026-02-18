/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "protocol_uart_v1.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>

namespace {

constexpr uint8_t LORAWAN_FPORT = 15;

UnbufferedSerial pc(USBTX, USBRX, 115200);
UnbufferedSerial esp(PA_9, PA_10, 115200);
DigitalOut led_rx(LED1, 0);

struct RuntimeStats {
    uint32_t rx_ok;
    uint32_t drop_crc;
    uint32_t drop_len;
    uint32_t drop_replay;
    uint32_t drop_ver;
    uint32_t tx_ok;
    uint32_t tx_fail;
};

RuntimeStats stats = {};
uint32_t last_counter_by_node[256] = {0};

enum class ParserState : uint8_t {
    WAIT_SOF1 = 0,
    WAIT_SOF2,
    WAIT_LEN,
    READ_PAYLOAD,
    READ_CRC
};

ParserState parser_state = ParserState::WAIT_SOF1;
uint8_t parser_len = 0;
uint8_t payload[UART_V1_PAYLOAD_LEN] = {0};
uint8_t payload_index = 0;
uint8_t crc_rx[2] = {0};
uint8_t crc_index = 0;

void log_line(const char *fmt, ...)
{
    char line[160];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }
    size_t to_write = (size_t)((n < (int)sizeof(line)) ? n : (int)sizeof(line) - 1);
    pc.write(line, to_write);
}

bool lorawan_send(uint8_t fport, const uint8_t *buf, uint8_t len)
{
    (void)fport;
    (void)buf;
    (void)len;
    return true;
}

void parser_reset(void)
{
    parser_state = ParserState::WAIT_SOF1;
    parser_len = 0;
    payload_index = 0;
    crc_index = 0;
}

void on_payload_valid(const uint8_t *payload_bytes)
{
    vision_uart_payload_v1_t frame = {};
    deserialize_payload_v1(&frame, payload_bytes);

    if (frame.ver != UART_V1_VERSION) {
        stats.drop_ver++;
        log_line("[UART_DROP] reason=ver\r\n");
        return;
    }

    uint32_t last_counter = last_counter_by_node[frame.node_id];
    if (frame.counter <= last_counter) {
        stats.drop_replay++;
        log_line("[UART_DROP] reason=replay\r\n");
        return;
    }
    last_counter_by_node[frame.node_id] = frame.counter;

    stats.rx_ok++;
    led_rx = !led_rx;
    log_line("[UART_OK] t=%lu ctr=%lu occ=%u l=%u\r\n",
             (unsigned long)frame.uptime_s,
             (unsigned long)frame.counter,
             frame.occupied,
             frame.luma);

    bool sent = lorawan_send(LORAWAN_FPORT, payload_bytes, UART_V1_PAYLOAD_LEN);
    if (sent) {
        stats.tx_ok++;
    } else {
        stats.tx_fail++;
    }
    log_line("[LORA_TX] port=%u len=%u ok=%u\r\n", LORAWAN_FPORT, UART_V1_PAYLOAD_LEN, sent ? 1U : 0U);
}

void handle_uart_byte(uint8_t byte)
{
    switch (parser_state) {
    case ParserState::WAIT_SOF1:
        if (byte == UART_V1_SOF1) {
            parser_state = ParserState::WAIT_SOF2;
        }
        break;

    case ParserState::WAIT_SOF2:
        if (byte == UART_V1_SOF2) {
            parser_state = ParserState::WAIT_LEN;
        } else if (byte != UART_V1_SOF1) {
            parser_state = ParserState::WAIT_SOF1;
        }
        break;

    case ParserState::WAIT_LEN:
        parser_len = byte;
        if (parser_len != UART_V1_PAYLOAD_LEN) {
            stats.drop_len++;
            log_line("[UART_DROP] reason=len\r\n");
            parser_state = (byte == UART_V1_SOF1) ? ParserState::WAIT_SOF2 : ParserState::WAIT_SOF1;
            break;
        }
        payload_index = 0;
        parser_state = ParserState::READ_PAYLOAD;
        break;

    case ParserState::READ_PAYLOAD:
        payload[payload_index++] = byte;
        if (payload_index >= UART_V1_PAYLOAD_LEN) {
            crc_index = 0;
            parser_state = ParserState::READ_CRC;
        }
        break;

    case ParserState::READ_CRC:
        crc_rx[crc_index++] = byte;
        if (crc_index >= 2) {
            uint8_t crc_input[1 + UART_V1_PAYLOAD_LEN];
            crc_input[0] = parser_len;
            memcpy(&crc_input[1], payload, UART_V1_PAYLOAD_LEN);
            uint16_t crc_calc = uart_v1_crc16_ccitt(crc_input, sizeof(crc_input));
            uint16_t crc_recv = ((uint16_t)crc_rx[0] << 8) | crc_rx[1];
            if (crc_calc != crc_recv) {
                stats.drop_crc++;
                log_line("[UART_DROP] reason=crc\r\n");
            } else {
                on_payload_valid(payload);
            }
            parser_reset();
        }
        break;
    }
}

}  // namespace

int main()
{
    log_line("[BOOT] parser ready fport=%u\r\n", LORAWAN_FPORT);

    while (true) {
        while (esp.readable()) {
            char byte = 0;
            if (esp.read(&byte, 1) == 1) {
                handle_uart_byte((uint8_t)byte);
            }
        }
        ThisThread::sleep_for(std::chrono::milliseconds(2));
    }
}
