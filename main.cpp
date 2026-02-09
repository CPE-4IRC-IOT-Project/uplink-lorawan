/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "lorawan/LoRaWANInterface.h"
#include "SX1276_LoRaRadio.h"
#include "events/EventQueue.h"
#include <cstring>

/* ================= UART ================= */
UnbufferedSerial pc(USBTX, USBRX, 115200);
UnbufferedSerial esp(PA_9, PA_10, 115200); // gardé pour plus tard
DigitalOut led_rx(LED1);

/* ================= LoRaWAN ================= */
static EventQueue ev_queue;
SX1276_LoRaRadio radio;
LoRaWANInterface lorawan(radio);

volatile bool lora_joined = false;
Ticker test_ticker;

/* ================= TTN KEYS ================= */
static uint8_t DEV_EUI[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x07, 0x5A, 0x34 };
static uint8_t APP_EUI[] = { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static uint8_t APP_KEY[] = {
    0x92,0xA8,0xAA,0x1A,
    0x93,0x0E,0x10,0xF4,
    0xB7,0x80,0x97,0xC2,
    0x9B,0x07,0xC1,0xB5
};

/* ================= UPLINK ================= */
void send_payload()
{
    if (!lora_joined) {
        pc.write("Not joined yet\r\n", 15);
        return;
    }

    uint8_t payload[4] = {
        0x00, // réservé
        0x01, // occupied
        3,    // persons
        95    // confidence
    };

    pc.write("Sending TEST uplink\r\n", 21);

    int16_t status = lorawan.send(15, payload, sizeof(payload), MSG_UNCONFIRMED_FLAG);

    switch (status) {
        case LORAWAN_STATUS_OK:
            pc.write("Uplink queued\r\n", 15);
            break;

        case LORAWAN_STATUS_WOULD_BLOCK:
            pc.write("LoRa busy, uplink queued\r\n", 27);
            break;

        case LORAWAN_STATUS_DUTYCYCLE_RESTRICTED:
            pc.write("Duty cycle restricted\r\n", 23);
            break;

        default:
            pc.write("LoRa send error\r\n", 17);
            break;
    }


    led_rx = !led_rx;
}

/* Appelé depuis ISR → on POSTE dans l’EventQueue */
void ticker_callback()
{
    ev_queue.call(send_payload);
}

/* ================= EVENTS ================= */
void lora_event_handler(lorawan_event_t event)
{
    switch (event) {

        case CONNECTED:
            pc.write("LoRaWAN JOIN SUCCESS\r\n", 24);
            lora_joined = true;

            // Ticker = déclencheur uniquement
            test_ticker.attach(ticker_callback, 30s);
            break;

        case TX_DONE:
            pc.write("TX DONE\r\n", 9);
            break;

        case JOIN_FAILURE:
            pc.write("JOIN FAILED\r\n", 13);
            break;

        default:
            break;
    }
}

/* ================= MAIN ================= */
int main()
{
    pc.write("STM32 ready - LoRaWAN TEST MODE\r\n", 36);

    lorawan.initialize(&ev_queue);

    static lorawan_app_callbacks_t callbacks;
    callbacks.events = lora_event_handler;
    lorawan.add_app_callbacks(&callbacks);

    lorawan_connect_t params;
    params.connect_type = LORAWAN_CONNECTION_OTAA;
    params.connection_u.otaa.dev_eui = DEV_EUI;
    params.connection_u.otaa.app_eui = APP_EUI;
    params.connection_u.otaa.app_key = APP_KEY;
    params.connection_u.otaa.nb_trials = 3;

    pc.write("Joining TTN...\r\n", 16);
    lorawan.connect(params);

    ev_queue.dispatch_forever();
}
