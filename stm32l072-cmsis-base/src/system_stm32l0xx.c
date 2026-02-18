#include <stdint.h>

uint32_t SystemCoreClock = 2097000U;

void SystemInit(void) {
}

void SystemCoreClockUpdate(void) {
  SystemCoreClock = 2097000U;
}
