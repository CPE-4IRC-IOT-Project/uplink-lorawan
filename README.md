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

OTAA credentials are read from `ttn_credentials.h` (local file, ignored by Git).

1. Copy the template:
   - `cp ttn_credentials.example.h ttn_credentials.h`
2. Edit `ttn_credentials.h` with your values:
   - `TTN_DEV_EUI`
   - `TTN_APP_EUI` (JoinEUI on TTN)
   - `TTN_APP_KEY`
3. Build/flash normally.

`mbed_app.json` contains only non-secret defaults (region, baudrate).
