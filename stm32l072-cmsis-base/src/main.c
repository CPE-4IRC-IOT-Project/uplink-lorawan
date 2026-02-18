#include <stdio.h>

int main(void) {
  printf("Hello\r\n");
  while (1) {
    __asm volatile("nop");
  }
}
