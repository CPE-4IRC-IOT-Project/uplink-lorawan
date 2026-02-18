#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(addr))

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

#define PERIPH_CLK_HZ       2097000UL
#define BAUDRATE            115200UL

#define RX_BUFFER_SIZE      256U
#define RX_BUFFER_MASK      (RX_BUFFER_SIZE - 1U)

static volatile uint8_t s_rx_buffer[RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head = 0U;
static volatile uint16_t s_rx_tail = 0U;
static volatile uint32_t s_rx_overflow = 0U;

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

static int rx_pop(uint8_t *byte)
{
  if (s_rx_head == s_rx_tail) {
    return 0;
  }

  *byte = s_rx_buffer[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1U) & RX_BUFFER_MASK);
  return 1;
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

static void uart_init(void)
{
  const uint32_t brr = (PERIPH_CLK_HZ + (BAUDRATE / 2UL)) / BAUDRATE;

  REG32(RCC_APB2ENR_ADDR) |= RCC_APB2ENR_USART1EN;
  REG32(RCC_APB1ENR_ADDR) |= RCC_APB1ENR_USART2EN;

  USART_CR1(USART2_BASE) = 0U;
  USART_BRR(USART2_BASE) = brr;
  USART_CR1(USART2_BASE) = USART_CR1_UE | USART_CR1_TE;

  USART_CR1(USART1_BASE) = 0U;
  USART_BRR(USART1_BASE) = brr;
  USART_CR1(USART1_BASE) = USART_CR1_UE | USART_CR1_RE | USART_CR1_RXNEIE;

  REG32(NVIC_ISER0_ADDR) = (1UL << USART1_IRQN);
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

  uart2_write_string("\nSTM32 ready: RX events on D2/P10 (PA10, USART1).\n");
  uart2_write_string("Forwarding ESP32 data to terminal on USART2.\n");

  uint32_t last_overflow = 0U;
  for (;;) {
    uint8_t byte = 0U;
    if (rx_pop(&byte)) {
      uart2_write_char(byte);
    }

    if (s_rx_overflow != last_overflow) {
      uart2_write_string("\n[WARN] RX buffer overflow\n");
      last_overflow = s_rx_overflow;
    }
  }
}
