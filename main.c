#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include "protocol_uart_v1.h"

#define REG32(addr) (*(volatile uint32_t *)(addr))

#define RCC_CFGR_ADDR       0x40021008UL
#define RCC_IOPENR_ADDR     0x4002102CUL
#define RCC_APB2ENR_ADDR    0x40021034UL
#define RCC_APB1ENR_ADDR    0x40021038UL

#define GPIOA_MODER_ADDR    0x50000000UL
#define GPIOA_PUPDR_ADDR    0x5000000CUL
#define GPIOA_AFRL_ADDR     0x50000020UL
#define GPIOA_AFRH_ADDR     0x50000024UL

#define USART1_BASE         0x40013800UL
#define USART2_BASE         0x40004400UL

#define USART_CR1(base)     REG32((base) + 0x00UL)
#define USART_BRR(base)     REG32((base) + 0x0CUL)
#define USART_ISR(base)     REG32((base) + 0x1CUL)
#define USART_ICR(base)     REG32((base) + 0x20UL)
#define USART_RDR(base)     REG32((base) + 0x24UL)
#define USART_TDR(base)     REG32((base) + 0x28UL)

#define RCC_IOPENR_GPIOAEN  (1UL << 0)
#define RCC_APB2ENR_USART1EN (1UL << 14)
#define RCC_APB1ENR_USART2EN (1UL << 17)

#define USART_CR1_UE        (1UL << 0)
#define USART_CR1_RE        (1UL << 2)
#define USART_CR1_TE        (1UL << 3)
#define USART_CR1_RXNEIE    (1UL << 5)

#define USART_ISR_RXNE      (1UL << 5)
#define USART_ISR_TXE       (1UL << 7)
#define USART_ISR_ORE       (1UL << 3)

#define USART_ICR_ORECF     (1UL << 3)

#define NVIC_ISER0_ADDR     0xE000E100UL
#define USART1_IRQN         26U

#define BAUDRATE            115200UL

#define RX_BUFFER_SIZE      256U
#define RX_BUFFER_MASK      (RX_BUFFER_SIZE - 1U)

static volatile uint8_t s_rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static volatile uint32_t s_rx_overflow = 0U;
static uint32_t s_last_counter_by_node[256] = {0};

#define LORAWAN_FPORT 15U

typedef struct {
  uint32_t rx_ok;
  uint32_t drop_crc;
  uint32_t drop_len;
  uint32_t drop_replay;
  uint32_t drop_ver;
  uint32_t tx_ok;
  uint32_t tx_fail;
} runtime_stats_t;

static runtime_stats_t s_stats = {0};
static uint32_t s_rx_bytes_total = 0U;
static bool s_rx_seen_once = false;

typedef enum {
  PARSER_WAIT_SOF1 = 0,
  PARSER_WAIT_SOF2,
  PARSER_WAIT_LEN,
  PARSER_READ_PAYLOAD,
  PARSER_READ_CRC
} parser_state_t;

static parser_state_t s_parser_state = PARSER_WAIT_SOF1;
static uint8_t s_parser_len = 0U;
static uint8_t s_payload[UART_V1_PAYLOAD_LEN] = {0};
static uint8_t s_payload_index = 0U;
static uint8_t s_crc_rx[2] = {0};
static uint8_t s_crc_index = 0U;

extern uint32_t SystemCoreClock;

static void uart2_write_char(uint8_t ch)
{
  while ((USART_ISR(USART2_BASE) & USART_ISR_TXE) == 0U) {
  }
  USART_TDR(USART2_BASE) = ch;
}

static void uart2_write_string(const char *msg)
{
  while (*msg != '\0') {
    if (*msg == '\n') {
      uart2_write_char('\r');
    }
    uart2_write_char((uint8_t)*msg);
    msg++;
  }
}

static void log_line(const char *fmt, ...)
{
  char line[160];
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  if (n <= 0) {
    return;
  }
  uart2_write_string(line);
}

static bool lorawan_send(uint8_t fport, const uint8_t *buf, uint8_t len)
{
  (void)fport;
  (void)buf;
  (void)len;
  return true;
}

static int rx_pop(uint8_t *byte)
{
  if (s_rx_head == s_rx_tail) {
    return 0;
  }

  *byte = s_rx_buffer[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1U) & RX_BUFFER_MASK);
  return 1;
}

static void parser_reset(void)
{
  s_parser_state = PARSER_WAIT_SOF1;
  s_parser_len = 0U;
  s_payload_index = 0U;
  s_crc_index = 0U;
}

static void on_payload_valid(const uint8_t *payload_bytes)
{
  vision_uart_payload_v1_t frame;
  deserialize_payload_v1(&frame, payload_bytes);

  if (frame.ver != UART_V1_VERSION) {
    s_stats.drop_ver++;
    log_line("[UART_DROP] reason=ver\n");
    return;
  }

  uint32_t last_counter = s_last_counter_by_node[frame.node_id];
  if (frame.counter <= last_counter) {
    s_stats.drop_replay++;
    log_line("[UART_DROP] reason=replay\n");
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

static void handle_uart_byte(uint8_t byte)
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
        log_line("[UART_DROP] reason=len\n");
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
          log_line("[UART_DROP] reason=crc\n");
        } else {
          on_payload_valid(s_payload);
        }
        parser_reset();
      }
      break;
  }
}

static void gpio_init(void)
{
  REG32(RCC_IOPENR_ADDR) |= RCC_IOPENR_GPIOAEN;

  /* PA2 (USART2_TX), PA9 (USART1_TX), PA10 (USART1_RX) in AF mode. */
  uint32_t moder = REG32(GPIOA_MODER_ADDR);
  moder &= ~((3UL << (2U * 2U)) | (3UL << (2U * 9U)) | (3UL << (2U * 10U)));
  moder |= (2UL << (2U * 2U)) | (2UL << (2U * 9U)) | (2UL << (2U * 10U));
  REG32(GPIOA_MODER_ADDR) = moder;

  /* Pull-up on RX to keep the line stable when disconnected. */
  uint32_t pupdr = REG32(GPIOA_PUPDR_ADDR);
  pupdr &= ~(3UL << (2U * 10U));
  pupdr |= (1UL << (2U * 10U));
  REG32(GPIOA_PUPDR_ADDR) = pupdr;

  /* AF4 = USART1/USART2 on these pins. */
  uint32_t afrl = REG32(GPIOA_AFRL_ADDR);
  afrl &= ~(0xFUL << (4U * 2U));
  afrl |= (4UL << (4U * 2U));
  REG32(GPIOA_AFRL_ADDR) = afrl;

  uint32_t afrh = REG32(GPIOA_AFRH_ADDR);
  afrh &= ~((0xFUL << (4U * (9U - 8U))) | (0xFUL << (4U * (10U - 8U))));
  afrh |= (4UL << (4U * (9U - 8U))) | (4UL << (4U * (10U - 8U)));
  REG32(GPIOA_AFRH_ADDR) = afrh;
}

static uint32_t get_apb1_clock_hz(void)
{
  uint32_t hclk = SystemCoreClock;
  if (hclk < 1000000UL || hclk > 64000000UL) {
    hclk = 32000000UL;
  }
  uint32_t cfgr = REG32(RCC_CFGR_ADDR);

  uint32_t hpre = (cfgr >> 4) & 0x0FU;
  uint32_t hclk_div = 1U;
  if (hpre >= 0x08U) {
    static const uint16_t ahb_div_table[8] = {2U, 4U, 8U, 16U, 64U, 128U, 256U, 512U};
    hclk_div = ahb_div_table[hpre - 0x08U];
  }
  uint32_t ahb_clk = hclk / hclk_div;

  uint32_t ppre1 = (cfgr >> 8) & 0x07U;
  uint32_t apb1_div = 1U;
  if (ppre1 >= 0x04U) {
    static const uint8_t apb_div_table[4] = {2U, 4U, 8U, 16U};
    apb1_div = apb_div_table[ppre1 - 0x04U];
  }

  return ahb_clk / apb1_div;
}

static void uart_init(void)
{
  uint32_t pclk1_hz = get_apb1_clock_hz();
  if (pclk1_hz < 1000000UL || pclk1_hz > 64000000UL) {
    pclk1_hz = 32000000UL;
  }
  const uint32_t brr = (pclk1_hz + (BAUDRATE / 2UL)) / BAUDRATE;

  REG32(RCC_APB2ENR_ADDR) |= RCC_APB2ENR_USART1EN;
  REG32(RCC_APB1ENR_ADDR) |= RCC_APB1ENR_USART2EN;

  USART_CR1(USART2_BASE) = 0U;
  USART_BRR(USART2_BASE) = brr;
  USART_CR1(USART2_BASE) = USART_CR1_UE | USART_CR1_TE;

  USART_CR1(USART1_BASE) = 0U;
  USART_BRR(USART1_BASE) = brr;
  USART_CR1(USART1_BASE) = USART_CR1_UE | USART_CR1_RE;
}

void USART1_IRQHandler(void)
{
  uint32_t isr = USART_ISR(USART1_BASE);

  if ((isr & USART_ISR_ORE) != 0U) {
    USART_ICR(USART1_BASE) = USART_ICR_ORECF;
  }

  if ((isr & USART_ISR_RXNE) != 0U) {
    uint8_t rx = (uint8_t)USART_RDR(USART1_BASE);
    uint16_t next = (uint16_t)((s_rx_head + 1U) & RX_BUFFER_MASK);

    if (next != s_rx_tail) {
      s_rx_buffer[s_rx_head] = rx;
      s_rx_head = next;
    } else {
      s_rx_overflow++;
    }
  }
}

int main(void)
{
  gpio_init();
  uart_init();

  log_line("[BOOT] pclk1=%lu baud=%lu\n", (unsigned long)get_apb1_clock_hz(), (unsigned long)BAUDRATE);
  log_line("[BOOT] parser ready fport=%u\n", (unsigned)LORAWAN_FPORT);

  uint32_t last_overflow = 0U;
  for (;;) {
    uint32_t isr = USART_ISR(USART1_BASE);
    if ((isr & USART_ISR_ORE) != 0U) {
      USART_ICR(USART1_BASE) = USART_ICR_ORECF;
      log_line("[UART_DROP] reason=ore\n");
    }
    if ((isr & USART_ISR_RXNE) != 0U) {
      uint8_t byte = (uint8_t)USART_RDR(USART1_BASE);
      handle_uart_byte(byte);
    }

    uint8_t byte = 0U;
    if (rx_pop(&byte)) {
      handle_uart_byte(byte);
    }

    if (s_rx_overflow != last_overflow) {
      uart2_write_string("\n[WARN] RX buffer overflow\n");
      last_overflow = s_rx_overflow;
    }
  }
}
