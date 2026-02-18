# uplink-lorawan

`main.cpp` now parses UART framed packets from ESP32:

- Wire format: `0x55 0xAA | len(16) | payload(16) | crc16`.
- Anti-replay: monotonic `counter` per `node_id`.
- LoRa uplink mapping: forwards only the 16-byte payload on FPort `15`.
