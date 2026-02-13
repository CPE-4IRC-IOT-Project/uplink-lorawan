// ============================================================================
// EXEMPLE CODE STM32 - Réception et validation des trames UART
// À intégrer dans votre projet STM32/LoRaWAN
// ============================================================================

#include "protocol_uart.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// ============================================================================
// Variables globales pour la réception UART
// ============================================================================
static uint8_t rx_buffer[sizeof(uart_event_frame_t)];
static size_t rx_index = 0;
static uint16_t last_valid_counter = 0;

// ============================================================================
// Machine à états pour la réception
// ============================================================================
typedef enum {
    STATE_WAIT_SOF,      // Attente du Start Of Frame (0xAA)
    STATE_RECEIVE_DATA   // Réception des données
} rx_state_t;

static rx_state_t rx_state = STATE_WAIT_SOF;

// ============================================================================
// Fonction appelée pour chaque octet reçu sur UART
// À appeler depuis votre IRQ UART ou polling
// ============================================================================
void uart_rx_process_byte(uint8_t byte) {
    switch (rx_state) {
        case STATE_WAIT_SOF:
            if (byte == SOF_BYTE) {
                rx_buffer[0] = byte;
                rx_index = 1;
                rx_state = STATE_RECEIVE_DATA;
            }
            break;
            
        case STATE_RECEIVE_DATA:
            rx_buffer[rx_index++] = byte;
            
            // Trame complète reçue ?
            if (rx_index >= sizeof(uart_event_frame_t)) {
                // Valider et traiter la trame
                uart_event_frame_t *frame = (uart_event_frame_t *)rx_buffer;
                
                if (validate_frame(frame, last_valid_counter)) {
                    // Trame valide ! Mettre à jour le compteur
                    last_valid_counter = frame->counter;
                    
                    // Traiter l'événement
                    process_event_frame(frame);
                } else {
                    // Trame invalide (mauvais CRC, compteur invalide, etc.)
                    // Log d'erreur ou compteur de trames rejetées
                }
                
                // Réinitialiser pour la prochaine trame
                rx_state = STATE_WAIT_SOF;
                rx_index = 0;
            }
            break;
    }
}

// ============================================================================
// Fonction pour traiter une trame valide
// ============================================================================
void process_event_frame(const uart_event_frame_t *frame) {
    // Extraire les données
    uint8_t node_id = frame->node_id;
    uint32_t timestamp = frame->timestamp;
    uint8_t event_id = frame->event_id;
    uint8_t count = frame->count;
    uint8_t confidence = frame->confidence;
    uint8_t flags = frame->flags;
    
    // Traitement selon le type d'événement
    switch (event_id) {
        case EVENT_PERSON_COUNT:
            // Comptage périodique (heartbeat)
            handle_person_count(node_id, count, confidence, flags, timestamp);
            break;
            
        case EVENT_PERSON_COUNT_CHANGE:
            // Changement de comptage détecté
            handle_person_count_change(node_id, count, confidence, flags, timestamp);
            break;
            
        default:
            // Event ID inconnu
            break;
    }
}

// ============================================================================
// Handlers spécifiques (à implémenter selon votre logique)
// ============================================================================
void handle_person_count(uint8_t node_id, uint8_t count, uint8_t confidence, 
                        uint8_t flags, uint32_t timestamp) {
    // Exemple: préparer un uplink LoRaWAN
    // Format simplifié pour LoRaWAN (peut être encore plus compact)
    uint8_t lorawan_payload[8];
    lorawan_payload[0] = node_id;
    lorawan_payload[1] = count;
    lorawan_payload[2] = confidence;
    lorawan_payload[3] = flags;
    lorawan_payload[4] = (timestamp >> 24) & 0xFF;
    lorawan_payload[5] = (timestamp >> 16) & 0xFF;
    lorawan_payload[6] = (timestamp >> 8) & 0xFF;
    lorawan_payload[7] = timestamp & 0xFF;
    
    // Envoyer via LoRaWAN (fonction à implémenter)
    // lorawan_send(lorawan_payload, sizeof(lorawan_payload));
}

void handle_person_count_change(uint8_t node_id, uint8_t count, uint8_t confidence,
                                uint8_t flags, uint32_t timestamp) {
    // Changement détecté: traitement prioritaire
    // Peut déclencher un envoi LoRaWAN immédiat avec port différent
    handle_person_count(node_id, count, confidence, flags, timestamp);
}

// ============================================================================
// Fonction d'initialisation UART (pseudo-code STM32)
// ============================================================================
void uart_init_stm32(void) {
    // Configuration UART (exemple pour STM32)
    // - 115200 bauds
    // - 8 bits de données
    // - 1 bit de stop
    // - Pas de parité
    // - Interruption sur réception
    
    // Exemple avec HAL STM32:
    /*
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart1);
    
    // Activer la réception IT
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    */
}

// ============================================================================
// Callback IRQ UART (exemple HAL STM32)
// ============================================================================
/*
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        // Traiter l'octet reçu
        uart_rx_process_byte(rx_byte);
        
        // Relancer la réception IT
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}
*/

// ============================================================================
// Statistiques / Monitoring (optionnel)
// ============================================================================
typedef struct {
    uint32_t total_frames;
    uint32_t valid_frames;
    uint32_t invalid_crc;
    uint32_t invalid_counter;
    uint32_t invalid_sof;
} uart_stats_t;

static uart_stats_t stats = {0};

void update_stats(bool valid, uint8_t error_type) {
    stats.total_frames++;
    
    if (valid) {
        stats.valid_frames++;
    } else {
        switch (error_type) {
            case 1: stats.invalid_crc++; break;
            case 2: stats.invalid_counter++; break;
            case 3: stats.invalid_sof++; break;
        }
    }
}
