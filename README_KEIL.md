# STM32 UART Receiver for Keil Studio Cloud

ESP32-P4 to STM32 UART protocol receiver (Mbed OS 6)

## Quick Setup

1. **Create new Mbed project** in Keil Studio Cloud
2. **Copy these files** to your project:
   - `main.cpp` (from main_mbed.cpp)
   - `protocol_uart.h`
   - `mbed_app.json`
3. **Build** and **Flash** to STM32 Nucleo

## Hardware Connections

```
ESP32-P4 GPIO37 (TX) → STM32 PA10 (D2)
ESP32-P4 GND         → STM32 GND
```

## Serial Monitor

- Baudrate: **115200**
- Port: ST-LINK Virtual COM Port

## Documentation

See `KEIL_STUDIO_GUIDE.md` for complete instructions.

## Supported Boards

- NUCLEO-L476RG (tested)
- Any Mbed-compatible STM32 with UART

## Files

- `main.cpp` - Mbed C++ code with UART receiver
- `protocol_uart.h` - Protocol definitions (shared with ESP32)
- `mbed_app.json` - Mbed configuration
- `KEIL_STUDIO_GUIDE.md` - Complete setup guide
