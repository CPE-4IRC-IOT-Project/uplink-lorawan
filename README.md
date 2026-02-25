# uplink-lorawan

Bridge UART -> LoRaWAN (TTN) for `DISCO_L072CZ_LRWAN1`.

## Behavior

- UART input wire format: `0x55 0xAA | len(16) | payload(16) | crc16`.
- Payload validation:
  - fixed length `16`
  - CRC16-CCITT check
  - anti-replay via monotonic `counter` per `node_id`
- LoRaWAN uplink:
  - sends the validated 16-byte payload as-is
  - FPort `15`
  - OTAA join to TTN

## Secure TTN credentials (not committed)

Real identifiers/keys must stay local in `mbed_app_local.json`, which is ignored by Git.

1. Copy the template:
   - `cp mbed_app_local.example.json mbed_app_local.json`
2. Edit `mbed_app_local.json` with your TTN values:
   - `lora.device-eui`
   - `lora.application-eui` (JoinEUI on TTN)
   - `lora.application-key`
3. Build/flash normally.

`mbed_app.json` contains only non-secret defaults (region, baudrate, FPort, OTAA enabled).
