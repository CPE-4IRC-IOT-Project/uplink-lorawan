#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "mbed.h"
#include "events/EventQueue.h"
#include "lorawan/LoRaWANInterface.h"
#include "lorawan/system/lorawan_data_structures.h"
#include "SX1276_LoRaRadio.h"

#include "protocol_uart_v1.h"
#include "ttn_credentials.h"

using namespace events;

namespace {

constexpr uint8_t LORAWAN_FPORT = 15U;
constexpr int UART_BAUDRATE = 115200;
constexpr size_t MAX_EVENTS = 32;

typedef struct {
    uint32_t rx_ok;
    uint32_t drop_crc;
    uint32_t drop_len;
    uint32_t drop_replay;
    uint32_t drop_ver;
    uint32_t tx_ok;
    uint32_t tx_fail;
} runtime_stats_t;

enum parser_state_t {
    PARSER_WAIT_SOF1 = 0,
    PARSER_WAIT_SOF2,
    PARSER_WAIT_LEN,
    PARSER_READ_PAYLOAD,
    PARSER_READ_CRC
};

UnbufferedSerial pc(USBTX, USBRX, UART_BAUDRATE);
BufferedSerial esp(PA_9, PA_10, UART_BAUDRATE);

EventQueue ev_queue(MAX_EVENTS * EVENTS_EVENT_SIZE);
SX1276_LoRaRadio radio;
LoRaWANInterface lorawan(radio);
lorawan_app_callbacks_t callbacks = {};

runtime_stats_t stats = {};
uint32_t last_counter_by_node[256] = {0};
bool lora_joined = false;
volatile bool uart_poll_scheduled = false;
bool rx_seen_once = false;

parser_state_t parser_state = PARSER_WAIT_SOF1;
uint8_t parser_len = 0U;
uint8_t payload[UART_V1_PAYLOAD_LEN] = {0};
uint8_t payload_index = 0U;
uint8_t crc_rx[2] = {0};
uint8_t crc_index = 0U;

bool drop_logged_len = false;
bool drop_logged_crc = false;
bool drop_logged_replay = false;
bool drop_logged_ver = false;

void pc_log(const char *fmt, ...)
{
    char line[200];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }
    size_t out_len = (static_cast<size_t>(n) < sizeof(line)) ? static_cast<size_t>(n) : (sizeof(line) - 1U);
    pc.write(line, out_len);
}

void parser_reset()
{
    parser_state = PARSER_WAIT_SOF1;
    parser_len = 0U;
    payload_index = 0U;
    crc_index = 0U;
}

bool lorawan_send(uint8_t fport, const uint8_t *buf, uint8_t len)
{
    if (!lora_joined) {
        pc_log("Not joined yet\r\n");
        return false;
    }

    int16_t status = lorawan.send(fport, const_cast<uint8_t *>(buf), len, MSG_UNCONFIRMED_FLAG);

    switch (status) {
        case LORAWAN_STATUS_OK:
            pc_log("Uplink queued\r\n");
            return true;
        case LORAWAN_STATUS_WOULD_BLOCK:
            pc_log("LoRa busy\r\n");
            return false;
        case LORAWAN_STATUS_DUTYCYCLE_RESTRICTED:
            pc_log("Duty cycle restricted\r\n");
            return false;
        default:
            pc_log("LoRa send error: %d\r\n", (int)status);
            return false;
    }
}

void on_payload_valid(const uint8_t *payload_bytes)
{
    vision_uart_payload_v1_t frame;
    deserialize_payload_v1(&frame, payload_bytes);

    if (frame.ver != UART_V1_VERSION) {
        stats.drop_ver++;
        if (!drop_logged_ver) {
            drop_logged_ver = true;
            pc_log("[UART_DROP] reason=ver\r\n");
        }
        return;
    }

    uint32_t last_counter = last_counter_by_node[frame.node_id];
    if (frame.counter <= last_counter) {
        stats.drop_replay++;
        if (!drop_logged_replay) {
            drop_logged_replay = true;
            pc_log("[UART_DROP] reason=replay\r\n");
        }
        return;
    }
    last_counter_by_node[frame.node_id] = frame.counter;

    stats.rx_ok++;
    pc_log("[UART_OK] t=%lu ctr=%lu occ=%u l=%u\r\n",
           (unsigned long)frame.uptime_s,
           (unsigned long)frame.counter,
           (unsigned)frame.occupied,
           (unsigned)frame.luma);

    bool sent = lorawan_send(LORAWAN_FPORT, payload_bytes, UART_V1_PAYLOAD_LEN);
    if (sent) {
        stats.tx_ok++;
    } else {
        stats.tx_fail++;
    }
    pc_log("[LORA_TX] port=%u len=%u ok=%u\r\n",
           (unsigned)LORAWAN_FPORT,
           (unsigned)UART_V1_PAYLOAD_LEN,
           sent ? 1U : 0U);
}

void handle_uart_byte(uint8_t byte)
{
    if (!rx_seen_once) {
        rx_seen_once = true;
        pc_log("[UART_RX] first_byte=0x%02X\r\n", (unsigned)byte);
    }

    switch (parser_state) {
        case PARSER_WAIT_SOF1:
            if (byte == UART_V1_SOF1) {
                parser_state = PARSER_WAIT_SOF2;
            }
            break;

        case PARSER_WAIT_SOF2:
            if (byte == UART_V1_SOF2) {
                parser_state = PARSER_WAIT_LEN;
            } else if (byte != UART_V1_SOF1) {
                parser_state = PARSER_WAIT_SOF1;
            }
            break;

        case PARSER_WAIT_LEN:
            parser_len = byte;
            if (parser_len != UART_V1_PAYLOAD_LEN) {
                stats.drop_len++;
                if (!drop_logged_len) {
                    drop_logged_len = true;
                    pc_log("[UART_DROP] reason=len\r\n");
                }
                parser_state = (byte == UART_V1_SOF1) ? PARSER_WAIT_SOF2 : PARSER_WAIT_SOF1;
                break;
            }
            payload_index = 0U;
            parser_state = PARSER_READ_PAYLOAD;
            break;

        case PARSER_READ_PAYLOAD:
            payload[payload_index++] = byte;
            if (payload_index >= UART_V1_PAYLOAD_LEN) {
                crc_index = 0U;
                parser_state = PARSER_READ_CRC;
            }
            break;

        case PARSER_READ_CRC:
            crc_rx[crc_index++] = byte;
            if (crc_index >= 2U) {
                uint8_t crc_input[1U + UART_V1_PAYLOAD_LEN];
                crc_input[0] = parser_len;
                memcpy(&crc_input[1], payload, UART_V1_PAYLOAD_LEN);
                uint16_t crc_calc = uart_v1_crc16_ccitt(crc_input, sizeof(crc_input));
                uint16_t crc_recv = ((uint16_t)crc_rx[0] << 8) | (uint16_t)crc_rx[1];
                if (crc_calc != crc_recv) {
                    stats.drop_crc++;
                    if (!drop_logged_crc) {
                        drop_logged_crc = true;
                        pc_log("[UART_DROP] reason=crc\r\n");
                    }
                } else {
                    on_payload_valid(payload);
                }
                parser_reset();
            }
            break;
    }
}

void poll_uart_esp()
{
    uint8_t byte = 0;
    while (true) {
        ssize_t n = esp.read(&byte, 1);
        if (n == 1) {
            handle_uart_byte(byte);
            continue;
        }
        break;
    }
    uart_poll_scheduled = false;
}

void on_uart_esp_activity()
{
    if (!uart_poll_scheduled) {
        uart_poll_scheduled = true;
        int id = ev_queue.call(poll_uart_esp);
        if (id == 0) {
            uart_poll_scheduled = false;
        }
    }
}

void lora_event_handler(lorawan_event_t event)
{
    switch (event) {
        case CONNECTED:
            lora_joined = true;
            pc_log("LoRaWAN JOIN SUCCESS\r\n");
            break;
        case TX_DONE:
            pc_log("TX DONE\r\n");
            break;
        case JOIN_FAILURE:
            lora_joined = false;
            pc_log("JOIN FAILED\r\n");
            break;
        case DISCONNECTED:
            lora_joined = false;
            pc_log("DISCONNECTED\r\n");
            break;
        case RX_DONE:
            pc_log("RX DONE\r\n");
            break;
        case TX_TIMEOUT:
        case TX_ERROR:
        case TX_CRYPTO_ERROR:
        case TX_SCHEDULING_ERROR:
            pc_log("TX ERROR event=%d\r\n", (int)event);
            break;
        default:
            break;
    }
}

}  // namespace

int main()
{
    esp.set_blocking(false);
    esp.sigio(mbed::callback(on_uart_esp_activity));

    pc_log("STM32 ready - UART -> TTN bridge\r\n");

    lorawan_status_t init = lorawan.initialize(&ev_queue);
    if (init != LORAWAN_STATUS_OK) {
        pc_log("LoRa init failed: %d\r\n", (int)init);
        return -1;
    }
    callbacks.events = mbed::callback(lora_event_handler);
    lorawan.add_app_callbacks(&callbacks);
    lorawan_status_t adr = lorawan.enable_adaptive_datarate();
    if (adr != LORAWAN_STATUS_OK) {
        pc_log("ADR enable failed: %d\r\n", (int)adr);
    }

    lorawan_connect_t params;
    params.connect_type = LORAWAN_CONNECTION_OTAA;
    params.connection_u.otaa.dev_eui = TTN_DEV_EUI;
    params.connection_u.otaa.app_eui = TTN_APP_EUI;
    params.connection_u.otaa.app_key = TTN_APP_KEY;
    params.connection_u.otaa.nb_trials = 3;

    pc_log("Joining TTN...\r\n");
    lorawan_status_t ret = lorawan.connect(params);
    if (ret != LORAWAN_STATUS_OK && ret != LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
        pc_log("Join start failed: %d\r\n", (int)ret);
    }

    ev_queue.dispatch_forever();
    return 0;
}
