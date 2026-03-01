#include <cstdint>
#include <cstdio>
#include <cstring>
#include <chrono>

#include "mbed.h"
#include "events/EventQueue.h"
#include "lorawan/LoRaWANInterface.h"
#include "SX1276_LoRaRadio.h"

#include "protocol_uart_v1.h"
#include "ttn_credentials.h"

using namespace events;

namespace {

constexpr uint8_t LORAWAN_FPORT = 15U;
constexpr auto UART_POLL_PERIOD = 20ms;
constexpr auto JOIN_STATUS_PERIOD = 10s;
constexpr auto JOIN_RETRY_DELAY = 30s;
constexpr auto JOIN_STUCK_TIMEOUT = 120s;
constexpr auto JOIN_RESTART_DELAY = 2s;
constexpr auto TX_RETRY_DELAY = 3s;
constexpr auto TX_STATUS_PERIOD = 5s;
constexpr auto TX_STUCK_TIMEOUT = 20s;
constexpr bool HEARTBEAT_THROTTLE_ENABLED = false;
constexpr auto HEARTBEAT_MIN_UPLINK_PERIOD = 10s;
constexpr uint8_t PERSON_COUNT_STABLE_FRAMES = 5U;

UnbufferedSerial pc(USBTX, USBRX, 115200);
UnbufferedSerial esp(PA_9, PA_10, 115200);

static EventQueue ev_queue;
SX1276_LoRaRadio radio;
LoRaWANInterface lorawan(radio);
lorawan_app_callbacks_t lora_callbacks = {};

volatile bool lora_joined = false;
volatile bool join_in_progress = false;
uint32_t join_attempt = 0;
lorawan_connect_t join_params = {};
Kernel::Clock::time_point join_started_at{};

enum parser_state_t {
    PARSER_WAIT_SOF1 = 0,
    PARSER_WAIT_SOF2,
    PARSER_WAIT_LEN,
    PARSER_READ_PAYLOAD,
    PARSER_READ_CRC
};

parser_state_t parser_state = PARSER_WAIT_SOF1;
uint8_t parser_len = 0U;
uint8_t payload[UART_V1_PAYLOAD_LEN] = {0};
uint8_t payload_index = 0U;
uint8_t crc_rx[2] = {0};
uint8_t crc_index = 0U;
uint32_t last_counter_by_node[256] = {0};

bool tx_in_flight = false;
bool pending_payload_valid = false;
bool pending_payload_is_heartbeat = false;
uint8_t pending_payload[UART_V1_PAYLOAD_LEN] = {0};
int tx_retry_event_id = 0;
int join_retry_event_id = 0;
bool heartbeat_sent_once = false;
Kernel::Clock::time_point last_heartbeat_sent_at{};
Kernel::Clock::time_point tx_started_at{};
bool last_tx_has_payload = false;
uint32_t last_tx_counter = 0;
uint8_t last_tx_node_id = 0;
uint8_t last_tx_msg_type = 0;
uint8_t last_tx_occupied = 0;
bool person_count_candidate_valid = false;
uint8_t person_count_candidate = 0U;
uint8_t person_count_candidate_streak = 0U;
bool person_count_last_sent_valid = false;
uint8_t person_count_last_sent = 0U;

void pc_puts(const char *s)
{
    pc.write(s, strlen(s));
}

void pc_logf(const char *fmt, int v)
{
    char line[64];
    int n = snprintf(line, sizeof(line), fmt, v);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }
}

void parser_reset()
{
    parser_state = PARSER_WAIT_SOF1;
    parser_len = 0U;
    payload_index = 0U;
    crc_index = 0U;
}

void queue_latest_payload(const uint8_t *pl, bool is_heartbeat)
{
    memcpy(pending_payload, pl, UART_V1_PAYLOAD_LEN);
    pending_payload_valid = true;
    pending_payload_is_heartbeat = is_heartbeat;
}

void flush_pending_payload();
void start_join_attempt();

void schedule_join_retry(Kernel::Clock::duration delay)
{
    if (join_retry_event_id != 0) {
        return;
    }

    join_retry_event_id = ev_queue.call_in(delay, []() {
        join_retry_event_id = 0;
        start_join_attempt();
    });
}

void schedule_tx_retry()
{
    if (tx_retry_event_id == 0) {
        tx_retry_event_id = ev_queue.call_in(TX_RETRY_DELAY, flush_pending_payload);
    }
}

bool send_uart_payload(const uint8_t *pl, bool is_heartbeat)
{
    if (!lora_joined) {
        return false;
    }

    if (HEARTBEAT_THROTTLE_ENABLED && is_heartbeat && heartbeat_sent_once) {
        auto now = Kernel::Clock::now();
        auto min_period = std::chrono::duration_cast<Kernel::Clock::duration>(HEARTBEAT_MIN_UPLINK_PERIOD);
        if ((now - last_heartbeat_sent_at) < min_period) {
            // Drop frequent heartbeats; occupancy changes are still forwarded.
            pc_puts("Heartbeat throttled\r\n");
            return false;
        }
    }

    int16_t status = lorawan.send(LORAWAN_FPORT, const_cast<uint8_t *>(pl), UART_V1_PAYLOAD_LEN, MSG_UNCONFIRMED_FLAG);
    if (status >= 0) {
        tx_in_flight = true;
        tx_started_at = Kernel::Clock::now();
        last_tx_has_payload = true;
        last_tx_msg_type = pl[1];
        last_tx_node_id = pl[2];
        last_tx_occupied = pl[5];
        last_tx_counter = uart_v1_read_be32(&pl[8]);
        if (is_heartbeat) {
            heartbeat_sent_once = true;
            last_heartbeat_sent_at = Kernel::Clock::now();
        }
        pc_puts("Trame UART envoyee a la radio (queued)\r\n");
        return true;
    }

    if (status == LORAWAN_STATUS_WOULD_BLOCK ||
        status == LORAWAN_STATUS_BUSY ||
        status == LORAWAN_STATUS_DUTYCYCLE_RESTRICTED ||
        status == LORAWAN_STATUS_NO_FREE_CHANNEL_FOUND) {
        if (status == LORAWAN_STATUS_DUTYCYCLE_RESTRICTED) {
            pc_puts("LoRa duty-cycle restricted\r\n");
        } else if (status == LORAWAN_STATUS_NO_FREE_CHANNEL_FOUND) {
            pc_puts("LoRa no free channel\r\n");
        } else if (status == LORAWAN_STATUS_BUSY || status == LORAWAN_STATUS_WOULD_BLOCK) {
            pc_puts("LoRa busy/would_block\r\n");
        }
        queue_latest_payload(pl, is_heartbeat);
        schedule_tx_retry();
    }

    char line[64];
    int n = snprintf(line, sizeof(line), "UART uplink err: %d\r\n", (int)status);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }
    return false;
}

void flush_pending_payload()
{
    tx_retry_event_id = 0;

    if (!pending_payload_valid || !lora_joined) {
        if (pending_payload_valid) {
            schedule_tx_retry();
        }
        return;
    }

    uint8_t pl[UART_V1_PAYLOAD_LEN];
    memcpy(pl, pending_payload, UART_V1_PAYLOAD_LEN);
    bool is_heartbeat = pending_payload_is_heartbeat;
    pending_payload_valid = false;
    pc_puts("Tentative d'envoi de la trame en attente\r\n");

    bool sent = send_uart_payload(pl, is_heartbeat);
    if (!sent && pending_payload_valid) {
        schedule_tx_retry();
    }
}

void on_payload_valid(const uint8_t *payload_bytes)
{
    vision_uart_payload_v1_t frame;
    deserialize_payload_v1(&frame, payload_bytes);

    if (frame.ver != UART_V1_VERSION) {
        pc_puts("[UART_DROP] reason=ver\r\n");
        return;
    }

    uint32_t last_counter = last_counter_by_node[frame.node_id];
    if (frame.counter <= last_counter) {
        pc_puts("[UART_DROP] reason=replay\r\n");
        return;
    }
    last_counter_by_node[frame.node_id] = frame.counter;

    char line[96];
    int n = snprintf(line,
                     sizeof(line),
                     "[UART_OK] t=%lu ctr=%lu occ=%u raw=%u stable=%u l=%u\r\n",
                     (unsigned long)frame.uptime_s,
                     (unsigned long)frame.counter,
                     (unsigned)frame.occupied,
                     (unsigned)frame.raw_count,
                     (unsigned)frame.stable_count,
                     (unsigned)frame.luma);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }

    uint8_t persons = frame.raw_count;

    if (!person_count_candidate_valid || persons != person_count_candidate) {
        person_count_candidate_valid = true;
        person_count_candidate = persons;
        person_count_candidate_streak = 1U;
    } else if (person_count_candidate_streak < PERSON_COUNT_STABLE_FRAMES) {
        person_count_candidate_streak++;
    }

    bool stable_ready = (person_count_candidate_streak >= PERSON_COUNT_STABLE_FRAMES);
    if (!stable_ready) {
        n = snprintf(line,
                     sizeof(line),
                     "[UART_FILTER] persons=%u streak=%u/%u sent_last=%u\r\n",
                     (unsigned)persons,
                     (unsigned)person_count_candidate_streak,
                     (unsigned)PERSON_COUNT_STABLE_FRAMES,
                     person_count_last_sent_valid ? (unsigned)person_count_last_sent : 255U);
        if (n > 0) {
            pc.write(line, static_cast<size_t>(n));
        }
        return;
    }

    n = snprintf(line,
                 sizeof(line),
                 "[UART_STABLE] persons=%u confirmed -> uplink\r\n",
                 (unsigned)person_count_candidate);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }

    if (!person_count_last_sent_valid && !SEND_INITIAL_STABLE_UPLINK) {
        person_count_last_sent = person_count_candidate;
        person_count_last_sent_valid = true;
        person_count_candidate_valid = false;
        person_count_candidate_streak = 0U;
        n = snprintf(line,
                     sizeof(line),
                     "[UART_BASELINE] persons=%u stored (no uplink at boot)\r\n",
                     (unsigned)person_count_last_sent);
        if (n > 0) {
            pc.write(line, static_cast<size_t>(n));
        }
        return;
    }

    bool is_heartbeat = (frame.msg_type == UART_V1_MSG_HEARTBEAT);
    bool sent = send_uart_payload(payload_bytes, is_heartbeat);
    if (sent) {
        person_count_last_sent = person_count_candidate;
        person_count_last_sent_valid = true;
        person_count_candidate_valid = false;
        person_count_candidate_streak = 0U;
    }
    n = snprintf(line,
                 sizeof(line),
                 "[LORA_TX] port=%u len=%u ok=%u\r\n",
                 (unsigned)LORAWAN_FPORT,
                 (unsigned)UART_V1_PAYLOAD_LEN,
                 sent ? 1U : 0U);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }
}

void handle_uart_byte(uint8_t byte)
{
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
                pc_puts("[UART_DROP] reason=len\r\n");
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
                    pc_puts("[UART_DROP] reason=crc\r\n");
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
    if (!lora_joined) {
        return;
    }

    uint8_t byte = 0;
    while (true) {
        ssize_t n = esp.read(&byte, 1);
        if (n == 1) {
            handle_uart_byte(byte);
            continue;
        }
        break;
    }
}

void start_join_attempt()
{
    if (lora_joined || join_in_progress) {
        return;
    }

    join_attempt++;
    join_started_at = Kernel::Clock::now();
    char line[64];
    int n = snprintf(line, sizeof(line), "Join attempt #%lu\r\n", (unsigned long)join_attempt);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }

    lorawan_status_t ret = lorawan.connect(join_params);
    n = snprintf(line, sizeof(line), "connect() ret=%d\r\n", (int)ret);
    if (n > 0) {
        pc.write(line, static_cast<size_t>(n));
    }

    join_in_progress = (ret == LORAWAN_STATUS_OK || ret == LORAWAN_STATUS_CONNECT_IN_PROGRESS);
    if (!join_in_progress) {
        // Stack is not ready yet (e.g. BUSY), try again later.
        schedule_join_retry(JOIN_RETRY_DELAY);
    }
}

void restart_join_after_timeout()
{
    pc_puts("JOIN watchdog timeout, retry join\r\n");
    join_in_progress = false;
    schedule_join_retry(std::chrono::duration_cast<Kernel::Clock::duration>(JOIN_RESTART_DELAY));
}

void lora_event_handler(lorawan_event_t event)
{
    switch (event) {
        case CONNECTED:
            pc_puts("LoRaWAN JOIN SUCCESS\r\n");
            lora_joined = true;
            join_in_progress = false;
            if (pending_payload_valid) {
                ev_queue.call(flush_pending_payload);
            }
            break;

        case TX_DONE:
            if (last_tx_has_payload) {
                char line[128];
                int n = snprintf(line,
                                 sizeof(line),
                                 "[LORA_SENT] TX_DONE node=%u ctr=%lu msg=%u occ=%u\r\n",
                                 (unsigned)last_tx_node_id,
                                 (unsigned long)last_tx_counter,
                                 (unsigned)last_tx_msg_type,
                                 (unsigned)last_tx_occupied);
                if (n > 0) {
                    pc.write(line, static_cast<size_t>(n));
                }
                last_tx_has_payload = false;
            } else {
                pc_puts("TX DONE\r\n");
            }
            tx_in_flight = false;
            if (pending_payload_valid) {
                ev_queue.call(flush_pending_payload);
            }
            break;

        case TX_TIMEOUT:
        case TX_ERROR:
        case TX_CRYPTO_ERROR:
        case TX_SCHEDULING_ERROR:
            tx_in_flight = false;
            schedule_tx_retry();
            break;

        case JOIN_FAILURE:
            pc_puts("JOIN FAILED\r\n");
            lora_joined = false;
            join_in_progress = false;
            tx_in_flight = false;
            schedule_join_retry(std::chrono::duration_cast<Kernel::Clock::duration>(JOIN_RETRY_DELAY));
            break;

        case DISCONNECTED:
            pc_puts("DISCONNECTED\r\n");
            lora_joined = false;
            join_in_progress = false;
            tx_in_flight = false;
            schedule_join_retry(std::chrono::duration_cast<Kernel::Clock::duration>(JOIN_RETRY_DELAY));
            break;

        default:
            pc_logf("LORA EVENT=%d\r\n", (int)event);
            break;
    }
}

void join_status_tick()
{
    if (join_in_progress && !lora_joined) {
        auto elapsed = Kernel::Clock::now() - join_started_at;
        auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        char line[64];
        int n = snprintf(line, sizeof(line), "JOIN PENDING... %lus\r\n", (unsigned long)elapsed_s);
        if (n > 0) {
            pc.write(line, static_cast<size_t>(n));
        }

        auto timeout = std::chrono::duration_cast<Kernel::Clock::duration>(JOIN_STUCK_TIMEOUT);
        if (elapsed >= timeout) {
            restart_join_after_timeout();
        }
    }
}

void tx_status_tick()
{
    if (!tx_in_flight) {
        return;
    }

    auto elapsed = Kernel::Clock::now() - tx_started_at;
    auto timeout = std::chrono::duration_cast<Kernel::Clock::duration>(TX_STUCK_TIMEOUT);
    if (elapsed >= timeout) {
        pc_puts("TX watchdog timeout, release queue\r\n");
        tx_in_flight = false;
        schedule_tx_retry();
    }
}

} // namespace

int main()
{
    pc.set_blocking(true);
    esp.set_blocking(false);

    pc_puts("STM32 ready - UART -> TTN bridge\r\n");

    lorawan_status_t init = lorawan.initialize(&ev_queue);
    if (init != LORAWAN_STATUS_OK) {
        pc_logf("LoRa init failed: %d\r\n", (int)init);
        return -1;
    }

    lora_callbacks.events = mbed::callback(lora_event_handler);
    lorawan.add_app_callbacks(&lora_callbacks);

    join_params.connect_type = LORAWAN_CONNECTION_OTAA;
    join_params.connection_u.otaa.dev_eui = TTN_DEV_EUI;
    join_params.connection_u.otaa.app_eui = TTN_APP_EUI;
    join_params.connection_u.otaa.app_key = TTN_APP_KEY;
    join_params.connection_u.otaa.nb_trials = 12;

    pc_puts("Joining TTN...\r\n");
    start_join_attempt();

    ev_queue.call_every(UART_POLL_PERIOD, poll_uart_esp);
    ev_queue.call_every(JOIN_STATUS_PERIOD, join_status_tick);
    ev_queue.call_every(TX_STATUS_PERIOD, tx_status_tick);
    ev_queue.dispatch_forever();
    return 0;
}
