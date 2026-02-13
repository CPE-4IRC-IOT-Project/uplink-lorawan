#ifndef PROTOCOL_UART_H
#define PROTOCOL_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// Définitions du protocole UART ESP32-P4 → STM32
// ============================================================================

#define SOF_BYTE            0xAA
#define FRAME_TYPE_EVENT    0x01

// Event IDs
#define EVENT_PERSON_COUNT         0x10
#define EVENT_PERSON_COUNT_CHANGE  0x11

// Flags
#define FLAG_DAYLIGHT       (1 << 0)  // Bit 0: lumière du jour
#define FLAG_NIGHT          (1 << 1)  // Bit 1: mode nuit
#define FLAG_ALARM          (1 << 2)  // Bit 2: alarme déclenchée
#define FLAG_MOTION         (1 << 3)  // Bit 3: mouvement détecté

// ============================================================================
// Structure de la trame (16 octets total)
// ============================================================================
typedef struct __attribute__((packed)) {
    uint8_t  sof;          // 0xAA
    uint8_t  len;          // longueur hors SOF (= 15)
    uint8_t  type;         // 0x01 = event
    uint8_t  node_id;      // ID du noeud (ex: 0x01)
    uint32_t timestamp;    // Unix timestamp (secondes)
    uint8_t  event_id;     // 0x10 = PERSON_COUNT
    uint8_t  count;        // nombre de personnes (0-255)
    uint8_t  confidence;   // confiance (0-100%)
    uint8_t  flags;        // bits de contexte
    uint16_t counter;      // compteur anti-rejeu monotone
    uint16_t crc;          // CRC-16
} uart_event_frame_t;

// ============================================================================
// Fonction CRC-16 (CRC-16-CCITT, polynôme 0x1021)
// ============================================================================
static inline uint16_t crc16_ccitt(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    
    return crc;
}

// ============================================================================
// Fonction pour construire et calculer le CRC d'une trame
// ============================================================================
static inline void build_event_frame(uart_event_frame_t *frame,
                                     uint8_t node_id,
                                     uint32_t timestamp,
                                     uint8_t event_id,
                                     uint8_t count,
                                     uint8_t confidence,
                                     uint8_t flags,
                                     uint16_t counter) {
    frame->sof = SOF_BYTE;
    frame->len = sizeof(uart_event_frame_t) - 1;  // 15 (sans le SOF)
    frame->type = FRAME_TYPE_EVENT;
    frame->node_id = node_id;
    frame->timestamp = timestamp;
    frame->event_id = event_id;
    frame->count = count;
    frame->confidence = confidence;
    frame->flags = flags;
    frame->counter = counter;
    
    // Calcul du CRC sur tout sauf le CRC lui-même (14 premiers octets)
    frame->crc = crc16_ccitt((uint8_t*)frame, sizeof(uart_event_frame_t) - 2);
}

// ============================================================================
// Fonction pour valider une trame reçue (côté STM32)
// ============================================================================
static inline bool validate_frame(const uart_event_frame_t *frame, uint16_t last_counter) {
    // Vérifier SOF
    if (frame->sof != SOF_BYTE) {
        return false;
    }
    
    // Vérifier longueur
    if (frame->len != 15) {
        return false;
    }
    
    // Vérifier CRC
    uint16_t calculated_crc = crc16_ccitt((uint8_t*)frame, sizeof(uart_event_frame_t) - 2);
    if (calculated_crc != frame->crc) {
        return false;
    }
    
    // Vérifier que le compteur est strictement croissant (anti-rejeu)
    if (frame->counter <= last_counter) {
        return false;
    }
    
    return true;
}

#endif // PROTOCOL_UART_H
