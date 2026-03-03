# uplink-lorawan

Bridge STM32 (`DISCO_L072CZ_LRWAN1`) entre UART et TTN (LoRaWAN OTAA).

## Fonctionnement rapide

1. Lit les trames UART v1 venant de l'ESP32-P4:
   `0x55 0xAA | len=16 | payload(16) | crc16`.
2. Vérifie la longueur et le CRC (`CRC16-CCITT`).
3. Envoie le `payload` brut de 16 octets en LoRaWAN sur le port applicatif `FPort 15`.
4. Gestion via pile Mbed LoRaWAN (ADR activé, OTAA).

Le code principal est dans `main.cpp`.

## Configuration TTN (locale, non commit)

Créer `ttn_credentials.h` depuis le template:

```bash
cp ttn_credentials.example.h ttn_credentials.h
```

Puis renseigner:
- `TTN_DEV_EUI`
- `TTN_APP_EUI` (JoinEUI)
- `TTN_APP_KEY`

## Build (Mbed)

Exemple avec `mbed-tools`:

```bash
mbed-tools compile -m DISCO_L072CZ_LRWAN1 -t GCC_ARM
```

Les paramètres radio/région/app-port sont dans `mbed_app.json`.
