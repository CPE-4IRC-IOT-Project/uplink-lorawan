#include "RTE_Components.h"
#include CMSIS_device_header

int main(void) {
  SystemCoreClockUpdate();

  while (1) {
    __NOP();
  }
}
