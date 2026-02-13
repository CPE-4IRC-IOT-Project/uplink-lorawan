#include "mbed.h"
#include "lorawan/LoRaWANInterface.h"
#include "SX1276_LoRaRadio.h"
#include "events/EventQueue.h"
#include <cstring>

// Debug PC via ST-LINK (USART2)
UnbufferedSerial pc(USBTX, USBRX, 115200);

// ESP32 -> STM32 sur USART1 : TX=PA9(D8), RX=PA10(D2)
UnbufferedSerial esp(PA_9, PA_10, 115200);

DigitalOut led_rx(LED1);

static EventQueue ev_queue;
SX1276_LoRaRadio radio;
LoRaWANInterface lorawan(radio);
volatile bool lora_joined = false;

static uint8_t DEV_EUI[] = { 0x70,0xB3,0xD5,0x7E,0xD0,0x07,0x5A,0x34 };
static uint8_t APP_EUI[] = { 0,0,0,0,0,0,0,0 };
static uint8_t APP_KEY[] = {
    0x92,0xA8,0xAA,0x1A,0x93,0x0E,0x10,0xF4,
    0xB7,0x80,0x97,0xC2,0x9B,0x07,0xC1,0xB5
};

static void lora_event_handler(lorawan_event_t event)
{
    if (event == CONNECTED) {
        const char *msg = "LoRaWAN JOIN OK\r\n";
        pc.write(msg, strlen(msg));
        lora_joined = true;
    }
}

static void send_line(const char *line, size_t len)
{
    if (!lora_joined || len == 0) return;

    // Envoie la ligne telle quelle (ASCII) sur le port 15
    lorawan.send(15, reinterpret_cast<const uint8_t *>(line), len, MSG_UNCONFIRMED_FLAG);
    led_rx = !led_rx;
}

int main() {
    const char *boot = "STM32 ready - listening on D2 (PA10)\r\n";
    pc.write(boot, strlen(boot));

    // Init LoRaWAN (OTAA)
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

    // Buffer de ligne pour envoyer sur TTN
    char line_buf[48];
    size_t line_len = 0;

    while (true) {
        while (esp.readable()) {
            char c;
            if (esp.read(&c, 1) == 1) {
                pc.write(&c, 1);

                // Accumule jusqu'a fin de ligne
                if (c == '\n') {
                    ev_queue.call(send_line, line_buf, line_len);
                    line_len = 0;
                } else if (line_len < sizeof(line_buf)) {
                    line_buf[line_len++] = c;
                } else {
                    line_len = 0;
                }
            }
        }

        ev_queue.dispatch_for(10ms);
    }
}