/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include <cstring>

// Debug PC via ST-LINK (USART2)
UnbufferedSerial pc(USBTX, USBRX, 115200);

// ESP32 -> STM32 sur USART1 : TX=PA9(D8), RX=PA10(D2)
UnbufferedSerial esp(PA_9, PA_10, 115200);

DigitalOut led_rx(LED1);

int main() {
    const char *boot = "STM32 ready - listening on D2 (PA10)\r\n";
    pc.write(boot, strlen(boot));

    char c;
    while (true) {
        while (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1) {
                pc.write(&c, 1);
            }
        }
    }
}