#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>

#include "mbed.h"
#include "events/EventQueue.h"
#include "lorawan/LoRaWANInterface.h"
#include "lorawan/system/lorawan_data_structures.h"

#include "lora_radio_helper.h"
#include "protocol_uart_v1.h"

using namespace events;

namespace {

constexpr std::chrono::milliseconds UART_POLL_PERIOD(5);
constexpr PinName UART_ESP_TX = PA_9;   // USART1_TX
constexpr PinName UART_ESP_RX = PA_10;  // USART1_RX
constexpr int UART_BAUDRATE = 115200;
constexpr uint8_t LORAWAN_FPORT = MBED_CONF_LORA_APP_PORT;
constexpr uint8_t RX_BUFFER_LEN = 32;
constexpr size_t MAX_EVENTS = 12;

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

runtime_stats_t s_stats = {};
uint32_t s_last_counter_by_node[256] = {0};
uint32_t s_rx_bytes_total = 0U;
bool s_rx_seen_once = false;
bool s_lora_connected = false;

parser_state_t s_parser_state = PARSER_WAIT_SOF1;
uint8_t s_parser_len = 0U;
uint8_t s_payload[UART_V1_PAYLOAD_LEN] = {0};
uint8_t s_payload_index = 0U;
uint8_t s_crc_rx[2] = {0};
uint8_t s_crc_index = 0U;

bool s_drop_logged_len = false;
bool s_drop_logged_crc = false;
bool s_drop_logged_replay = false;
bool s_drop_logged_ver = false;
bool s_drop_logged_not_connected = false;

UnbufferedSerial s_uart_esp(UART_ESP_TX, UART_ESP_RX, UART_BAUDRATE);
EventQueue s_ev_queue(MAX_EVENTS * EVENTS_EVENT_SIZE);
LoRaWANInterface s_lorawan(radio);
lorawan_app_callbacks_t s_lora_callbacks = {};

void log_line(const char *fmt, ...)
{
    char line[192];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }
    printf("%s", line);
}

bool lorawan_send(uint8_t fport, const uint8_t *buf, uint8_t len)
{
    if (!s_lora_connected) {
        if (!s_drop_logged_not_connected) {
            s_drop_logged_not_connected = true;
            log_line("[LORA_TX] drop=not_connected\n");
        }
        return false;
    }

    int16_t ret = s_lorawan.send(fport, const_cast<uint8_t *>(buf), len, MSG_UNCONFIRMED_FLAG);
    if (ret < 0) {
        log_line("[LORA_TX] err=%d\n", (int)ret);
        return false;
    }

    return true;
}

void parser_reset()
{
    s_parser_state = PARSER_WAIT_SOF1;
    s_parser_len = 0U;
    s_payload_index = 0U;
    s_crc_index = 0U;
}

void on_payload_valid(const uint8_t *payload_bytes)
{
    vision_uart_payload_v1_t frame;
    deserialize_payload_v1(&frame, payload_bytes);

    if (frame.ver != UART_V1_VERSION) {
        s_stats.drop_ver++;
        if (!s_drop_logged_ver) {
            s_drop_logged_ver = true;
            log_line("[UART_DROP] reason=ver\n");
        }
        return;
    }

    uint32_t last_counter = s_last_counter_by_node[frame.node_id];
    if (frame.counter <= last_counter) {
        s_stats.drop_replay++;
        if (!s_drop_logged_replay) {
            s_drop_logged_replay = true;
            log_line("[UART_DROP] reason=replay\n");
        }
        return;
    }
    s_last_counter_by_node[frame.node_id] = frame.counter;

    s_stats.rx_ok++;
    log_line("[UART_OK] t=%lu ctr=%lu occ=%u l=%u\n",
             (unsigned long)frame.uptime_s,
             (unsigned long)frame.counter,
             (unsigned)frame.occupied,
             (unsigned)frame.luma);

    bool sent = lorawan_send(LORAWAN_FPORT, payload_bytes, UART_V1_PAYLOAD_LEN);
    if (sent) {
        s_stats.tx_ok++;
    } else {
        s_stats.tx_fail++;
    }
    log_line("[LORA_TX] port=%u len=%u ok=%u\n",
             (unsigned)LORAWAN_FPORT,
             (unsigned)UART_V1_PAYLOAD_LEN,
             sent ? 1U : 0U);
}

void handle_uart_byte(uint8_t byte)
{
    s_rx_bytes_total++;
    if (!s_rx_seen_once) {
        s_rx_seen_once = true;
        log_line("[UART_RX] first_byte=0x%02X\n", (unsigned)byte);
    }

    switch (s_parser_state) {
        case PARSER_WAIT_SOF1:
            if (byte == UART_V1_SOF1) {
                s_parser_state = PARSER_WAIT_SOF2;
            }
            break;

        case PARSER_WAIT_SOF2:
            if (byte == UART_V1_SOF2) {
                s_parser_state = PARSER_WAIT_LEN;
            } else if (byte != UART_V1_SOF1) {
                s_parser_state = PARSER_WAIT_SOF1;
            }
            break;

        case PARSER_WAIT_LEN:
            s_parser_len = byte;
            if (s_parser_len != UART_V1_PAYLOAD_LEN) {
                s_stats.drop_len++;
                if (!s_drop_logged_len) {
                    s_drop_logged_len = true;
                    log_line("[UART_DROP] reason=len\n");
                }
                s_parser_state = (byte == UART_V1_SOF1) ? PARSER_WAIT_SOF2 : PARSER_WAIT_SOF1;
                break;
            }
            s_payload_index = 0U;
            s_parser_state = PARSER_READ_PAYLOAD;
            break;

        case PARSER_READ_PAYLOAD:
            s_payload[s_payload_index++] = byte;
            if (s_payload_index >= UART_V1_PAYLOAD_LEN) {
                s_crc_index = 0U;
                s_parser_state = PARSER_READ_CRC;
            }
            break;

        case PARSER_READ_CRC:
            s_crc_rx[s_crc_index++] = byte;
            if (s_crc_index >= 2U) {
                uint8_t crc_input[1U + UART_V1_PAYLOAD_LEN];
                crc_input[0] = s_parser_len;
                memcpy(&crc_input[1], s_payload, UART_V1_PAYLOAD_LEN);

                uint16_t crc_calc = uart_v1_crc16_ccitt(crc_input, sizeof(crc_input));
                uint16_t crc_recv = ((uint16_t)s_crc_rx[0] << 8) | (uint16_t)s_crc_rx[1];

                if (crc_calc != crc_recv) {
                    s_stats.drop_crc++;
                    if (!s_drop_logged_crc) {
                        s_drop_logged_crc = true;
                        log_line("[UART_DROP] reason=crc\n");
                    }
                } else {
                    on_payload_valid(s_payload);
                }
                parser_reset();
            }
            break;
    }
}

void uart_poll()
{
    uint8_t byte = 0U;
    while (true) {
        const ssize_t ret = s_uart_esp.read(&byte, 1U);
        if (ret == 1) {
            handle_uart_byte(byte);
            continue;
        }
        break;
    }
}

void receive_message()
{
    uint8_t rx_buffer[RX_BUFFER_LEN] = {0};
    uint8_t port = 0;
    int flags = 0;

    int16_t ret = s_lorawan.receive(rx_buffer, sizeof(rx_buffer), port, flags);
    if (ret < 0) {
        log_line("[LORA_RX] err=%d\n", (int)ret);
        return;
    }

    log_line("[LORA_RX] port=%u len=%d\n", (unsigned)port, (int)ret);
}

void lora_event_handler(lorawan_event_t event)
{
    switch (event) {
        case CONNECTED:
            s_lora_connected = true;
            log_line("[LORA] connected\n");
            break;
        case DISCONNECTED:
            s_lora_connected = false;
            log_line("[LORA] disconnected\n");
            break;
        case TX_DONE:
            log_line("[LORA] tx_done\n");
            break;
        case TX_TIMEOUT:
        case TX_ERROR:
        case TX_CRYPTO_ERROR:
        case TX_SCHEDULING_ERROR:
            log_line("[LORA] tx_error event=%d\n", (int)event);
            break;
        case RX_DONE:
            receive_message();
            break;
        case RX_TIMEOUT:
        case RX_ERROR:
            log_line("[LORA] rx_error event=%d\n", (int)event);
            break;
        case JOIN_FAILURE:
            s_lora_connected = false;
            log_line("[LORA] join_failed (check OTAA credentials)\n");
            break;
        case UPLINK_REQUIRED:
            log_line("[LORA] uplink_required\n");
            break;
        default:
            log_line("[LORA] event=%d\n", (int)event);
            break;
    }
}

}  // namespace

int main()
{
    s_uart_esp.set_blocking(false);

    log_line("[BOOT] uart=%d fport=%u\n",
             UART_BAUDRATE,
             (unsigned)LORAWAN_FPORT);

    if (s_lorawan.initialize(&s_ev_queue) != LORAWAN_STATUS_OK) {
        log_line("[LORA] init_failed\n");
        return -1;
    }

    s_lora_callbacks.events = mbed::callback(lora_event_handler);
    s_lorawan.add_app_callbacks(&s_lora_callbacks);

    if (s_lorawan.enable_adaptive_datarate() != LORAWAN_STATUS_OK) {
        log_line("[LORA] adr_enable_failed\n");
        return -1;
    }

    lorawan_status_t ret = s_lorawan.connect();
    if (ret != LORAWAN_STATUS_OK && ret != LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
        log_line("[LORA] connect_failed code=%d\n", (int)ret);
        return -1;
    }

    log_line("[LORA] join_in_progress\n");
    s_ev_queue.call_every(UART_POLL_PERIOD, uart_poll);
    s_ev_queue.dispatch_forever();
    return 0;
}
