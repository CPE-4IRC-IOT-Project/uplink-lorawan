/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "lorawan/LoRaWANInterface.h"
#include "SX1276_LoRaRadio.h"
#include "events/EventQueue.h"
#include <cstring>

// ---------------- UART ----------------
UnbufferedSerial pc(USBTX, USBRX, 115200);
UnbufferedSerial esp(PA_9, PA_10, 115200);

// LED
DigitalOut led_rx(LED1);

// ---------------- LoRaWAN ----------------
static EventQueue ev_queue;
SX1276_LoRaRadio radio;
LoRaWANInterface lorawan(radio);

// TTN credentials
static uint8_t DEV_EUI[] = {0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x07, 0x5A, 0x34};
static uint8_t APP_EUI[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static uint8_t APP_KEY[] = {0x92, 0xA8, 0xAA, 0x1A, 0x93, 0x0E, 0x10, 0xF4,
                            0xB7, 0x80, 0x97, 0xC2, 0x9B, 0x07, 0xC1, 0xB5};

volatile bool lora_joined = false;

// ---------------- Buffer UART ----------------
char line[64];
int idx = 0;

// ---------------- LoRaWAN event handler ----------------
void lora_event_handler(lorawan_event_t event)
{
    switch (event) {

        case CONNECTED:
            pc.write("LoRaWAN: JOIN SUCCESS\r\n", 23);
            lora_joined = true;
            break;

        case DISCONNECTED:
            pc.write("LoRaWAN: DISCONNECTED\r\n", 24);
            lora_joined = false;
            break;

        case TX_DONE:
            pc.write("LoRaWAN: TX DONE\r\n", 18);
            break;

        case TX_TIMEOUT:
            pc.write("LoRaWAN: TX TIMEOUT\r\n", 21);
            break;

        case TX_ERROR:
            pc.write("LoRaWAN: TX ERROR\r\n", 19);
            break;

        case RX_DONE:
            pc.write("LoRaWAN: RX DONE (downlink)\r\n", 30);
            break;

        case JOIN_FAILURE:
            pc.write("LoRaWAN: JOIN FAILED\r\n", 24);
            break;

        default:
            pc.write("LoRaWAN: UNKNOWN EVENT\r\n", 25);
            break;
    }
}

// FIX ✅ structure de callbacks
static lorawan_app_callbacks_t callbacks = {
    .events = lora_event_handler
};

// ---------------- Send uplink ----------------
void send_payload_to_ttn(uint8_t* payload, size_t len)
{
    if (!lora_joined) {
        pc.write("LoRaWAN not joined yet, uplink skipped\r\n", 44);
        return;
    }

    pc.write("Sending uplink...\r\n", 19);

    int16_t status = lorawan.send(15, payload, len, MSG_UNCONFIRMED_FLAG);

    if (status == LORAWAN_STATUS_OK) {
        pc.write("LoRaWAN send accepted\r\n", 24);
    } else {
        pc.write("LoRaWAN send FAILED, code=", 28);
        char buf[6];
        snprintf(buf, sizeof(buf), "%d", status);
        pc.write(buf, strlen(buf));
        pc.write("\r\n", 2);
    }
}

int main()
{
    pc.write("STM32 ready - listening on D2 (PA10)\r\n", 40);

    // ---------------- LoRaWAN init ----------------
    lorawan.initialize(&ev_queue);
    lorawan.add_app_callbacks(&callbacks);   // FIX ✅

    lorawan_connect_t connect_params;
    connect_params.connect_type = LORAWAN_CONNECTION_OTAA;
    connect_params.connection_u.otaa.dev_eui = DEV_EUI;
    connect_params.connection_u.otaa.app_eui = APP_EUI;
    connect_params.connection_u.otaa.app_key = APP_KEY;

    lorawan.connect(connect_params);
    pc.write("Joining TTN network...\r\n", 25);

    // ---------------- Main loop ----------------
    while (true) {

        while (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1) {

                pc.write(&c, 1);

                if (c == '\n' || idx >= sizeof(line) - 1) {
                    line[idx] = '\0';

                    // Format: occupied,3,95
                    char* token = strtok(line, ",");
                    bool occupied = false;
                    uint8_t nb_persons = 0;
                    uint8_t confidence = 0;

                    if (token) {
                        occupied = (strcmp(token, "occupied") == 0);
                        token = strtok(NULL, ",");
                    }
                    if (token) {
                        nb_persons = atoi(token);
                        token = strtok(NULL, ",");
                    }
                    if (token) {
                        confidence = atoi(token);
                    }

                    uint8_t payload[4];
                    payload[0] = 0x00;
                    payload[1] = occupied ? 0x01 : 0x00;
                    payload[2] = nb_persons;
                    payload[3] = confidence;

                    send_payload_to_ttn(payload, 4);

                    led_rx = !led_rx;
                    idx = 0;
                }
                else {
                    line[idx++] = c;
                }
            }
        }

        ThisThread::sleep_for(10ms);
    }
}
