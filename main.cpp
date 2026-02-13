// ============================================================================
// CODE STM32 MBED - Réception et validation des trames UART
// Version C++ Mbed OS 6
// ============================================================================

#include "mbed.h"
#include "protocol_uart.h"
#include <cstring>

// Debug PC via ST-LINK (USART2)
UnbufferedSerial pc(USBTX, USBRX, 115200);

// ESP32 -> STM32 sur USART1 : TX=PA9(D8), RX=PA10(D2)
UnbufferedSerial esp(PA_9, PA_10, 115200);

// LED pour feedback
DigitalOut led(LED1);

// ============================================================================
// Variables globales pour la réception UART
// ============================================================================
static uint8_t rx_buffer[sizeof(uart_event_frame_t)];
static size_t rx_index = 0;
static uint16_t last_valid_counter = 0;

// Machine à états pour la réception
typedef enum {
    STATE_WAIT_SOF,
    STATE_RECEIVE_DATA
} rx_state_t;

static rx_state_t rx_state = STATE_WAIT_SOF;

// Statistiques
typedef struct {
    uint32_t total_frames;
    uint32_t valid_frames;
    uint32_t invalid_crc;
    uint32_t invalid_counter;
} uart_stats_t;

static uart_stats_t stats = {0};

// ============================================================================
// Fonction pour traiter un octet reçu
// ============================================================================
void uart_rx_process_byte(uint8_t byte) {
    switch (rx_state) {
        case STATE_WAIT_SOF:
            if (byte == SOF_BYTE) {
                rx_buffer[0] = byte;
                rx_index = 1;
                rx_state = STATE_RECEIVE_DATA;
                
                // Debug: SOF trouvé
                const char *sof_msg = "\r\n[SOF]\r\n";
                pc.write(sof_msg, strlen(sof_msg));
            }
            break;
            
        case STATE_RECEIVE_DATA:
            rx_buffer[rx_index++] = byte;
            
            if (rx_index >= sizeof(uart_event_frame_t)) {
                uart_event_frame_t *frame = (uart_event_frame_t *)rx_buffer;
                
                if (validate_frame(frame, last_valid_counter)) {
                    // Trame valide
                    last_valid_counter = frame->counter;
                    stats.valid_frames++;
                    
                    // Toggle LED
                    led = !led
;
                    
                    // Afficher sur PC
                    char msg[100];
                    int len = snprintf(msg, sizeof(msg),
                                      "[VALID] Node:%u Count:%u Conf:%u%% Counter:%u\r\n",
                                      frame->node_id, frame->count, 
                                      frame->confidence, frame->counter);
                    if (len > 0) {
                        pc.write(msg, len);
                    }
                } else {
                    // Trame invalide
                    stats.invalid_crc++;
                    const char *err = "[INVALID]\r\n";
                    pc.write(err, strlen(err));
                }
                
                stats.total_frames++;
                rx_state = STATE_WAIT_SOF;
                rx_index = 0;
            }
            break;
    }
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    // Message de démarrage
    const char *banner = 
        "\r\n╔════════════════════════════════════════╗\r\n"
        "║  STM32 UART Receiver - Protocol v1.0  ║\r\n"
        "║  ESP32-P4 → STM32 → LoRaWAN (Mbed)   ║\r\n"
        "╚════════════════════════════════════════╝\r\n\r\n";
    pc.write(banner, strlen(banner));
    
    const char *msg = "Listening on PA10 (RX from ESP32)...\r\n";
    pc.write(msg, strlen(msg));
    
    Timer stats_timer;
    stats_timer.start();
    
    uint8_t byte;
    char line_buffer[128];
    int line_idx = 0;
    
    while (true) {
        // Lire depuis ESP32 (RAPIDE - pas d'écriture PC)
        if (esp.readable()) {
            if (esp.read(&byte, 1) == 1) {
                // Bufferiser localement
                if (byte == '\n') {
                    // Fin de ligne - afficher tout d'un coup
                    line_buffer[line_idx++] = '\r';
                    line_buffer[line_idx++] = '\n';
                    pc.write(line_buffer, line_idx);
                    line_idx = 0;
                } else if (byte != '\r' && line_idx < sizeof(line_buffer) - 2) {
                    // Accumuler caractère
                    line_buffer[line_idx++] = byte;
                }
                
                // Traiter dans le protocole
                uart_rx_process_byte(byte);
            }
        }
        
        // Afficher stats toutes les 30s
        if (stats_timer.elapsed_time().count() > 30000000) {  // 30s en microsecondes
            char stats_msg[100];
            int len = snprintf(stats_msg, sizeof(stats_msg),
                             "\r\n=== Stats ===\r\nTotal: %lu | Valid: %lu | CRC err: %lu\r\n",
                             stats.total_frames, stats.valid_frames, stats.invalid_crc);
            if (len > 0) {
                pc.write(stats_msg, len);
            }
            stats_timer.reset();
        }
        
        ThisThread::sleep_for(1ms);
    }
}
