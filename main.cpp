// ============================================================================
// CODE STM32 MBED - Test UART avec interruption RX
// Version C++ Mbed OS 6 - Pour Keil Studio Cloud
// ============================================================================

#include "mbed.h"
#include <cstring>

// Debug PC via ST-LINK (USART2)
UnbufferedSerial pc(USBTX, USBRX, 115200);

// ESP32 -> STM32 sur USART1 : RX=PA10(D2), TX=PA9(D8)
UnbufferedSerial esp(PA_10, PA_9, 115200);

// LED pour feedback
DigitalOut led(LED1);

// ============================================================================
// Buffer circulaire pour interruption RX
// ============================================================================
#define RX_BUFFER_SIZE 512
volatile uint8_t rx_circular_buffer[RX_BUFFER_SIZE];
volatile uint16_t rx_write_idx = 0;
volatile uint16_t rx_read_idx = 0;
volatile uint32_t rx_overflow_count = 0;

// ============================================================================
// Callback interruption RX UART (appelé automatiquement à chaque octet)
// ============================================================================
void esp_rx_irq() {
    while (esp.readable()) {
        uint8_t byte;
        if (esp.read(&byte, 1) == 1) {
            uint16_t next_idx = (rx_write_idx + 1) % RX_BUFFER_SIZE;
            if (next_idx != rx_read_idx) {  // Buffer pas plein
                rx_circular_buffer[rx_write_idx] = byte;
                rx_write_idx = next_idx;
            } else {
                rx_overflow_count++;  // Compteur overflow
            }
        }
    }
}

// ============================================================================
// MAIN
// ============================================================================
int main() {
    // Banner de démarrage
    const char *banner = 
        "\r\n╔════════════════════════════════════════╗\r\n"
        "║  STM32 UART Test - Interrupt Mode     ║\r\n"
        "║  ESP32-P4 → STM32 (Mbed OS 6)         ║\r\n"
        "╚════════════════════════════════════════╝\r\n\r\n";
    pc.write(banner, strlen(banner));
    
    const char *msg = "Listening on PA10 (RX from ESP32) - IRQ enabled...\r\n";
    pc.write(msg, strlen(msg));
    
    // Activer interruption RX sur UART ESP32
    esp.attach(&esp_rx_irq, UnbufferedSerial::RxIrq);
    
    led = 1;  // LED ON = prêt
    
    uint32_t byte_count = 0;
    Timer stats_timer;
    stats_timer.start();
    
    while (true) {
        // Lire du buffer circulaire (rempli par interruption)
        while (rx_read_idx != rx_write_idx) {
            uint8_t byte = rx_circular_buffer[rx_read_idx];
            rx_read_idx = (rx_read_idx + 1) % RX_BUFFER_SIZE;
            
            byte_count++;
            
            // Afficher sur PC
            pc.write(&byte, 1);
        }
        
        // Stats toutes les 5 secondes
        if (stats_timer.elapsed_time().count() >= 5000000) {  // 5 sec en microsecondes
            char stats_msg[100];
            int len = snprintf(stats_msg, sizeof(stats_msg), 
                             "\r\n[Stats] Bytes: %lu | Overflows: %lu\r\n",
                             byte_count, rx_overflow_count);
            pc.write(stats_msg, len);
            
            byte_count = 0;
            rx_overflow_count = 0;
            stats_timer.reset();
            
            led = !led;  // Toggle LED
        }
    }
}
