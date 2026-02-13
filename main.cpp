/* mbed Microcontroller Library
 * Copyright (c) 2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "lorawan/LoRaWANInterface.h"
#include "SX1276_LoRaRadio.h"
#include "events/EventQueue.h"
#include <cstring>

/* ================= CONFIG ================= */
#define PROTO_SOF 0xAA
#define FRAME_LEN 16

/* ================= UART ================= */
UnbufferedSerial pc(USBTX, USBRX, 115200);
UnbufferedSerial esp(PA_9, PA_10, 115200); // RX = D2
DigitalOut led_rx(LED1);

/* ================= LoRaWAN ================= */
static EventQueue ev_queue;
SX1276_LoRaRadio radio;
LoRaWANInterface lorawan(radio);
volatile bool lora_joined = false;

/* ================= TTN KEYS ================= */
static uint8_t DEV_EUI[] = { 0x70,0xB3,0xD5,0x7E,0xD0,0x07,0x5A,0x34 };
static uint8_t APP_EUI[] = { 0,0,0,0,0,0,0,0 };
static uint8_t APP_KEY[] = {
    0x92,0xA8,0xAA,0x1A,0x93,0x0E,0x10,0xF4,
    0xB7,0x80,0x97,0xC2,0x9B,0x07,0xC1,0xB5
};

/* ================= CRC16 CCITT ================= */
uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
        }
    }
    return crc;
}

/* ================= SEND TO TTN ================= */
void send_payload(uint8_t event_id, uint8_t count, uint8_t conf)
{
    if (!lora_joined) return;

    uint8_t payload[4];
    payload[0] = 0x00;
    payload[1] = (count > 0) ? 1 : 0;   // occupied = au moins 1 personne
    payload[2] = count;
    payload[3] = conf;

    /* Log TTN uplink */
    char line[64];
    int n = snprintf(line, sizeof(line),
        "[TX LoRa] port=15 | ");
    pc.write(line, n);
    for (int i = 0; i < 4; i++) {
        char hx[4];
        snprintf(hx, sizeof(hx), "%02X ", payload[i]);
        pc.write(hx, 3);
    }
    n = snprintf(line, sizeof(line),
        "| occ=%u cnt=%u conf=%u%%\r\n",
        payload[1], payload[2], payload[3]);
    pc.write(line, n);

    lorawan.send(15, payload, sizeof(payload), MSG_UNCONFIRMED_FLAG);
    led_rx = !led_rx;
}

/* ================= UART FRAME PARSER ================= */
void process_frame(uint8_t *f)
{
    /* Affichage hex complet de la trame recue */
    pc.write("[RX UART] ", 10);
    for (int i = 0; i < FRAME_LEN; i++) {
        char hx[4];
        snprintf(hx, sizeof(hx), "%02X ", f[i]);
        pc.write(hx, 3);
    }
    pc.write("\r\n", 2);

    /* Verification CRC */
    uint16_t rx_crc = (f[14] << 8) | f[15];
    uint16_t calc_crc = crc16_ccitt(&f[1], 13);

    if (rx_crc != calc_crc) {
        char err[48];
        int n = snprintf(err, sizeof(err),
            "[RX UART] CRC FAIL rx=%04X calc=%04X\r\n", rx_crc, calc_crc);
        pc.write(err, n);
        return;
    }

    uint8_t event_id = f[8];
    uint8_t count    = f[9];
    uint8_t conf     = f[10];
    uint16_t pkt_cnt = (f[12] << 8) | f[13];

    char info[80];
    int n = snprintf(info, sizeof(info),
        "[RX UART] CRC OK | evt=0x%02X cnt=%u conf=%u%% pkt#%u\r\n",
        event_id, count, conf, pkt_cnt);
    pc.write(info, n);

    ev_queue.call(send_payload, event_id, count, conf);
}


/* ================= LoRa EVENTS ================= */
void lora_event_handler(lorawan_event_t event)
{
    if (event == CONNECTED) {
        pc.write("LoRaWAN JOIN OK\r\n", 18);
        lora_joined = true;
    }
}

/* ================= MAIN ================= */
int main()
{
    pc.write("STM32 UART BIN → LoRaWAN\r\n", 28);

    /* LoRa init */
    lorawan.initialize(&ev_queue);
    static lorawan_app_callbacks_t cb;
    cb.events = lora_event_handler;
    lorawan.add_app_callbacks(&cb);

    lorawan_connect_t params;
    params.connect_type = LORAWAN_CONNECTION_OTAA;
    params.connection_u.otaa.dev_eui = DEV_EUI;
    params.connection_u.otaa.app_eui = APP_EUI;
    params.connection_u.otaa.app_key = APP_KEY;
    params.connection_u.otaa.nb_trials = 3;
    lorawan.connect(params);

    /* UART reception */
    uint8_t buf[FRAME_LEN];
    uint8_t idx = 0;
    bool sync = false;

    while (true) {

        while (esp.readable()) {
            uint8_t b;
            esp.read(&b, 1);

            if (!sync) {
                if (b == PROTO_SOF) {
                    buf[0] = b;
                    idx = 1;
                    sync = true;
                }
            } else if (idx == 1 && b != 0x0F) {
                /* LEN invalide → faux SOF, on resync */
                sync = false;
                idx = 0;
                /* Ce byte pourrait etre un vrai SOF */
                if (b == PROTO_SOF) {
                    buf[0] = b;
                    idx = 1;
                    sync = true;
                }
            } else {
                buf[idx++] = b;
                if (idx == FRAME_LEN) {
                    process_frame(buf);
                    sync = false;
                    idx = 0;
                }
            }
        }

        ev_queue.dispatch_for(10ms);
    }
}
