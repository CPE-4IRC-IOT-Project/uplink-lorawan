/**
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdio.h>
#include <string.h>

#include "mbed.h"
#include "lorawan/LoRaWANInterface.h"
#include "lorawan/system/lorawan_data_structures.h"
#include "events/EventQueue.h"

// Application helpers
#include "DummySensor.h"
#include "trace_helper.h"
#include "lora_radio_helper.h"
#include "protocol_uart_v1.h"

using namespace events;

// Max payload size can be LORAMAC_PHY_MAXPAYLOAD.
// This example only communicates with much shorter messages (<30 bytes).
// If longer messages are used, these buffers must be changed accordingly.
uint8_t tx_buffer[30];
uint8_t rx_buffer[30];

/*
 * Sets up an application dependent transmission timer in ms. Used only when Duty Cycling is off for testing
 */
#define TX_TIMER                        10000

/**
 * Maximum number of events for the event queue.
 * 10 is the safe number for the stack events, however, if application
 * also uses the queue for whatever purposes, this number should be increased.
 */
#define MAX_NUMBER_OF_EVENTS            10

/**
 * Maximum number of retries for CONFIRMED messages before giving up
 */
#define CONFIRMED_MSG_RETRY_COUNTER     3
#define UART_RETRY_TIMER_MS             500
#define UART_BAUDRATE                   115200

/**
 * Dummy pin for dummy sensor
 */
#define PC_9                            0

/**
 * Dummy sensor class object
 */
DS1820  ds1820(PC_9);

/**
* This event queue is the global event queue for both the
* application and stack. To conserve memory, the stack is designed to run
* in the same thread as the application and the application is responsible for
* providing an event queue to the stack that will be used for ISR deferment as
* well as application information event queuing.
*/
static EventQueue ev_queue(MAX_NUMBER_OF_EVENTS *EVENTS_EVENT_SIZE);

/**
 * Event handler.
 *
 * This will be passed to the LoRaWAN stack to queue events for the
 * application which in turn drive the application.
 */
static void lora_event_handler(lorawan_event_t event);
static bool read_next_uart_payload(uint8_t out_payload[UART_V1_PAYLOAD_LEN]);

/**
 * Constructing Mbed LoRaWANInterface and passing it the radio object from lora_radio_helper.
 */
static LoRaWANInterface lorawan(radio);

/**
 * Application specific callbacks
 */
static lorawan_app_callbacks_t callbacks;

/**
 * UART link from external board:
 * RX on PA_10 (D2), TX on PA_9
 */
static UnbufferedSerial uart_link(PA_9, PA_10, UART_BAUDRATE);

static uint8_t parser_state = 0;
static uint8_t parser_len = 0;
static uint8_t parser_payload[UART_V1_PAYLOAD_LEN] = {0};
static uint8_t parser_payload_index = 0;
static uint8_t parser_crc[2] = {0};
static uint8_t parser_crc_index = 0;

static void parser_reset(void)
{
    parser_state = 0;
    parser_len = 0;
    parser_payload_index = 0;
    parser_crc_index = 0;
}

static bool read_next_uart_payload(uint8_t out_payload[UART_V1_PAYLOAD_LEN])
{
    uint8_t byte = 0;
    size_t consumed = 0;

    while (consumed < 128) {
        ssize_t n = uart_link.read(&byte, 1);
        if (n != 1) {
            break;
        }
        consumed++;

        switch (parser_state) {
            case 0: // WAIT SOF1
                if (byte == UART_V1_SOF1) {
                    parser_state = 1;
                }
                break;

            case 1: // WAIT SOF2
                if (byte == UART_V1_SOF2) {
                    parser_state = 2;
                } else if (byte != UART_V1_SOF1) {
                    parser_state = 0;
                }
                break;

            case 2: // WAIT LEN
                parser_len = byte;
                if (parser_len != UART_V1_PAYLOAD_LEN) {
                    printf("\r\n UART frame drop: bad len=%u (expected %u)\r\n",
                           parser_len, UART_V1_PAYLOAD_LEN);
                    parser_state = (byte == UART_V1_SOF1) ? 1 : 0;
                    break;
                }
                parser_payload_index = 0;
                parser_state = 3;
                break;

            case 3: // READ PAYLOAD
                parser_payload[parser_payload_index++] = byte;
                if (parser_payload_index >= UART_V1_PAYLOAD_LEN) {
                    parser_crc_index = 0;
                    parser_state = 4;
                }
                break;

            case 4: // READ CRC
                parser_crc[parser_crc_index++] = byte;
                if (parser_crc_index >= 2) {
                    uint8_t crc_input[1 + UART_V1_PAYLOAD_LEN];
                    crc_input[0] = parser_len;
                    memcpy(&crc_input[1], parser_payload, UART_V1_PAYLOAD_LEN);
                    uint16_t crc_calc = uart_v1_crc16_ccitt(crc_input, sizeof(crc_input));
                    uint16_t crc_recv = ((uint16_t)parser_crc[0] << 8) | (uint16_t)parser_crc[1];
                    if (crc_calc == crc_recv) {
                        memcpy(out_payload, parser_payload, UART_V1_PAYLOAD_LEN);
                        parser_reset();
                        return true;
                    }
                    printf("\r\n UART frame drop: bad crc\r\n");
                    parser_reset();
                }
                break;

            default:
                parser_reset();
                break;
        }
    }

    return false;
}

/**
 * Entry point for application
 */
int main(void)
{
    // setup tracing
    setup_trace();
    uart_link.set_blocking(false);
    printf("\r\n UART bridge ready (RX=PA10/D2, TX=PA9, %d bps)\r\n", UART_BAUDRATE);

    // stores the status of a call to LoRaWAN protocol
    lorawan_status_t retcode;

    // Initialize LoRaWAN stack
    if (lorawan.initialize(&ev_queue) != LORAWAN_STATUS_OK) {
        printf("\r\n LoRa initialization failed! \r\n");
        return -1;
    }

    printf("\r\n Mbed LoRaWANStack initialized \r\n");

    // prepare application callbacks
    callbacks.events = mbed::callback(lora_event_handler);
    lorawan.add_app_callbacks(&callbacks);

    // Set number of retries in case of CONFIRMED messages
    if (lorawan.set_confirmed_msg_retries(CONFIRMED_MSG_RETRY_COUNTER)
            != LORAWAN_STATUS_OK) {
        printf("\r\n set_confirmed_msg_retries failed! \r\n\r\n");
        return -1;
    }

    printf("\r\n CONFIRMED message retries : %d \r\n",
           CONFIRMED_MSG_RETRY_COUNTER);

    // Enable adaptive data rate
    if (lorawan.enable_adaptive_datarate() != LORAWAN_STATUS_OK) {
        printf("\r\n enable_adaptive_datarate failed! \r\n");
        return -1;
    }

    printf("\r\n Adaptive data  rate (ADR) - Enabled \r\n");

    retcode = lorawan.connect();

    if (retcode == LORAWAN_STATUS_OK ||
            retcode == LORAWAN_STATUS_CONNECT_IN_PROGRESS) {
    } else {
        printf("\r\n Connection error, code = %d \r\n", retcode);
        return -1;
    }

    printf("\r\n Connection - In Progress ...\r\n");

    // make your event queue dispatching events forever
    ev_queue.dispatch_forever();

    return 0;
}

/**
 * Sends a message to the Network Server
 */
static void send_message()
{
    uint16_t packet_len = 0;
    int16_t retcode;
    uint8_t payload16[UART_V1_PAYLOAD_LEN];

    if (!read_next_uart_payload(payload16)) {
        printf("\r\n No complete UART frame yet on PA10/D2, retrying... \r\n");
        if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
            ev_queue.call_in(UART_RETRY_TIMER_MS, send_message);
        }
        return;
    }

    packet_len = UART_V1_PAYLOAD_LEN;
    memcpy(tx_buffer, payload16, packet_len);

    printf("\r\n UART payload ready (%u bytes): ", packet_len);
    for (uint16_t i = 0; i < packet_len; i++) {
        printf("%02x ", tx_buffer[i]);
    }
    printf("\r\n");

    retcode = lorawan.send(MBED_CONF_LORA_APP_PORT, tx_buffer, packet_len,
                           MSG_UNCONFIRMED_FLAG);

    if (retcode < 0) {
        retcode == LORAWAN_STATUS_WOULD_BLOCK ? printf("send - WOULD BLOCK\r\n")
        : printf("\r\n send() - Error code %d \r\n", retcode);

        if (retcode == LORAWAN_STATUS_WOULD_BLOCK) {
            //retry in 3 seconds
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                ev_queue.call_in(3000, send_message);
            }
        } else {
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                ev_queue.call_in(UART_RETRY_TIMER_MS, send_message);
            }
        }
        return;
    }

    printf("\r\n %d bytes scheduled for transmission on port %d \r\n", retcode, MBED_CONF_LORA_APP_PORT);
    memset(tx_buffer, 0, sizeof(tx_buffer));
}

/**
 * Receive a message from the Network Server
 */
static void receive_message()
{
    uint8_t port;
    int flags;
    int16_t retcode = lorawan.receive(rx_buffer, sizeof(rx_buffer), port, flags);

    if (retcode < 0) {
        printf("\r\n receive() - Error code %d \r\n", retcode);
        return;
    }

    printf(" RX Data on port %u (%d bytes): ", port, retcode);
    for (uint8_t i = 0; i < retcode; i++) {
        printf("%02x ", rx_buffer[i]);
    }
    printf("\r\n");
    
    memset(rx_buffer, 0, sizeof(rx_buffer));
}

/**
 * Event handler
 */
static void lora_event_handler(lorawan_event_t event)
{
    switch (event) {
        case CONNECTED:
            printf("\r\n Connection - Successful (joined TTN) \r\n");
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                send_message();
            } else {
                ev_queue.call_every(TX_TIMER, send_message);
            }

            break;
        case DISCONNECTED:
            ev_queue.break_dispatch();
            printf("\r\n Disconnected Successfully \r\n");
            break;
        case TX_DONE:
            printf("\r\n Message Sent to Network Server \r\n");
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                send_message();
            }
            break;
        case TX_TIMEOUT:
        case TX_ERROR:
        case TX_CRYPTO_ERROR:
        case TX_SCHEDULING_ERROR:
            printf("\r\n Transmission Error - EventCode = %d \r\n", event);
            // try again
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                send_message();
            }
            break;
        case RX_DONE:
            printf("\r\n Received message from Network Server \r\n");
            receive_message();
            break;
        case RX_TIMEOUT:
        case RX_ERROR:
            printf("\r\n Error in reception - Code = %d \r\n", event);
            break;
        case JOIN_FAILURE:
            printf("\r\n OTAA Failed - Check Keys \r\n");
            break;
        case UPLINK_REQUIRED:
            printf("\r\n Uplink required by NS \r\n");
            if (MBED_CONF_LORA_DUTY_CYCLE_ON) {
                send_message();
            }
            break;
        default:
            MBED_ASSERT("Unknown Event");
    }
}

// EOF
