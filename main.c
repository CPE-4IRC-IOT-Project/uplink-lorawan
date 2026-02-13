// ============================================================================
// CODE STM32 COMPLET - Réception et validation des trames UART
// Adapté depuis Mbed C++ vers HAL STM32 C
// ============================================================================

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "protocol_uart.h"
#include "main.h"
#include "stm32l4xx_hal.h"

// ============================================================================
// Handlers UART (déclarés globalement)
// ============================================================================
UART_HandleTypeDef huart1;  // ESP32 -> STM32 (PA9=TX, PA10=RX)
UART_HandleTypeDef huart2;  // Debug PC via ST-LINK (USBTX, USBRX)

// Buffer pour la réception IT
static uint8_t rx_byte = 0;

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
                    
                    // Statistiques
                    update_stats(true, 0);
                    
                    // Toggle LED pour indiquer réception valide
                    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
                    
                    // Traiter l'événement
                    process_event_frame(frame);
                } else {
                    // Trame invalide
                    update_stats(false, 1);  // Error type 1 = CRC/validation error
                    
                    // Debug: afficher erreur
                    const char *err = "[ERR] Invalid frame\r\n";
                    HAL_UART_Transmit(&huart2, (uint8_t*)err, strlen(err), 100);
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
    // Debug: afficher les données reçues
    char debug_msg[128];
    int len = snprintf(debug_msg, sizeof(debug_msg),
                      "[RX] Node:%u Count:%u Conf:%u%% Flags:0x%02X Time:%lu\r\n",
                      node_id, count, confidence, flags, timestamp);
    if (len > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t*)debug_msg, len, 100);
    }
    
    // Préparer un uplink LoRaWAN
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
    
    // TODO: Envoyer via LoRaWAN (fonction à implémenter)
    // lorawan_send(lorawan_payload, sizeof(lorawan_payload));
}

void handle_person_count_change(uint8_t node_id, uint8_t count, uint8_t confidence,
                                uint8_t flags, uint32_t timestamp) {
    // Changement détecté: traitement prioritaire
    // Peut déclencher un envoi LoRaWAN immédiat avec port différent
    handle_person_count(node_id, count, confidence, flags, timestamp);
}

// ============================================================================
// Fonction d'initialisation GPIO pour UART
// ============================================================================
static void MX_GPIO_UART_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Enable clocks
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    
    // USART1 GPIO Configuration (ESP32 communication)
    // PA9  -> USART1_TX
    // PA10 -> USART1_RX
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // USART2 GPIO Configuration (Debug PC via ST-LINK)
    // PA2  -> USART2_TX
    // PA3  -> USART2_RX
    GPIO_InitStruct.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // Configure USART1 interrupt
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

// ============================================================================
// Fonction d'initialisation UART1 (ESP32 -> STM32)
// ============================================================================
static void MX_USART1_UART_Init(void) {
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&huart1) != HAL_OK) {
        Error_Handler();
    }
}

// ============================================================================
// Fonction d'initialisation UART2 (Debug PC)
// ============================================================================
static void MX_USART2_UART_Init(void) {
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

// ============================================================================
// Initialisation complète UART
// ============================================================================
void uart_init_stm32(void) {
    // Init GPIO pour UART
    MX_GPIO_UART_Init();
    
    // Init USART1 (ESP32)
    MX_USART1_UART_Init();
    
    // Init USART2 (Debug PC)
    MX_USART2_UART_Init();
    
    // Message de boot
    const char *boot_msg = "STM32 ready - listening on USART1 PA10 (RX from ESP32)\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)boot_msg, strlen(boot_msg), 1000);
    
    // Démarrer la réception IT sur USART1
    HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
}

// ============================================================================
// Callback IRQ UART (HAL STM32)
// ============================================================================
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) {
        // Traiter l'octet reçu depuis ESP32
        uart_rx_process_byte(rx_byte);
        
        // Debug: afficher l'octet sur UART2 (PC)
        HAL_UART_Transmit(&huart2, &rx_byte, 1, 10);
        
        // Relancer la réception IT
        HAL_UART_Receive_IT(&huart1, &rx_byte, 1);
    }
}

// ============================================================================
// IRQ Handler USART1
// ============================================================================
void USART1_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart1);
}

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

// ============================================================================
// Fonction pour afficher les statistiques
// ============================================================================
void print_stats(void) {
    char buffer[100];
    int len = snprintf(buffer, sizeof(buffer), 
                      "\r\n=== Stats ===\r\nTotal: %lu | Valid: %lu | CRC err: %lu | Cntr err: %lu\r\n",
                      stats.total_frames, stats.valid_frames, 
                      stats.invalid_crc, stats.invalid_counter);
    if (len > 0) {
        HAL_UART_Transmit(&huart2, (uint8_t*)buffer, len, 1000);
    }
}

// ============================================================================
// System Clock Configuration
// ============================================================================
void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Configure the main internal regulator output voltage
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) {
        Error_Handler();
    }

    // Initializes the RCC Oscillators
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.MSIState = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM = 1;
    RCC_OscInitStruct.PLL.PLLN = 40;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    // Initializes the CPU, AHB and APB buses clocks
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) {
        Error_Handler();
    }
}

// ============================================================================
// Error Handler
// ============================================================================
void Error_Handler(void) {
    __disable_irq();
    while (1) {
        // Stay here
    }
}

// ============================================================================
// MAIN - Programme principal
// ============================================================================
int main(void) {
    // Reset de tous les périphériques, init Flash et Systick
    HAL_Init();
    
    // Configuration de l'horloge système
    SystemClock_Config();
    
    // Initialisation LED (optionnel)
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5;  // LED1 sur PA5 (Nucleo)
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // Initialisation UART (ESP32 + Debug PC)
    uart_init_stm32();
    
    // Message de démarrage
    const char *welcome = "\r\n╔════════════════════════════════════════╗\r\n"
                         "║  STM32 UART Receiver - Protocol v1.0  ║\r\n"
                         "║  ESP32-P4 → STM32 → LoRaWAN           ║\r\n"
                         "╚════════════════════════════════════════╝\r\n\r\n";
    HAL_UART_Transmit(&huart2, (uint8_t*)welcome, strlen(welcome), 1000);
    
    uint32_t last_stats_time = HAL_GetTick();
    
    // Boucle principale
    while (1) {
        // Afficher les stats toutes les 30 secondes
        if ((HAL_GetTick() - last_stats_time) > 30000) {
            print_stats();
            last_stats_time = HAL_GetTick();
        }
        
        // Toggle LED pour montrer que le système tourne
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(1000);
    }
}
