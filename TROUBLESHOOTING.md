# 🐛 Guide de dépannage - Trames invalides

## Symptômes observés

```
[ERR] Invalid frame
Total: 4 | Valid: 0 | CRC err: 4
```

Les données sont reçues mais **invalides** (caractères corrompus ���).

---

## 🔍 Diagnostic avec le code debug

### 1. Recompilez avec le code debug amélioré

Le nouveau `main.cpp` affiche maintenant :
- Octets bruts en HEX
- Trames complètes
- CRC reçu vs calculé
- Détection du SOF (0xAA)

### 2. Analysez la sortie

**Sortie attendue (trames valides) :**
```
[SOF] 
[FRAME] AA 0F 01 01 69 8E E6 4C 10 05 5F 01 00 01 06 A2
[✓ VALID] Node:1 Count:5 Conf:95% Flags:0x01 Counter:1
```

**Sortie actuelle (problème) :**
```
[FRAME] E8 3F 92 11 ...
[✗ INVALID] SOF:E8 LEN:63 CRC_rcv:XXXX CRC_calc:YYYY Counter:ZZ
```

---

## ✅ Checklist de résolution

### 🔴 Problème 1 : Pas de SOF détecté

**Symptôme** : Vous ne voyez jamais `[SOF]`

**Cause** : ESP32 n'envoie pas ou baudrate incorrect

**Solutions** :
1. **Vérifier ESP32 fonctionne** :
   ```bash
   cd p4-edge-vision
   idf.py monitor
   ```
   → Vous devriez voir les logs ESP32

2. **Vérifier le baudrate** :
   - ESP32 : 115200 (dans main.c)
   - STM32 : 115200 (dans main.cpp ligne 13)
   - Les deux DOIVENT être identiques

3. **Tester un message simple** :
   Dans ESP32 `main.c`, ajoutez temporairement :
   ```c
   // Au lieu de send_event_frame()
   const char *test = "HELLO\r\n";
   uart_write_bytes(UART_PORT, test, strlen(test));
   ```
   → Vous devriez voir "HELLO" sur le STM32

---

### 🔴 Problème 2 : SOF trouvé mais trame invalide

**Symptôme** : Vous voyez `[SOF]` mais ensuite `[✗ INVALID]`

**Cause** : Erreur de transmission (baudrate, câble, bruit)

**Solutions** :

#### A. Vérifier le câblage
```
ESP32-P4              STM32 Nucleo
───────────           ──────────────
GPIO37 (TX) ────────→ PA10 (RX) ← Vérifier !
GND         ────────  GND        ← OBLIGATOIRE
```

**⚠️ Erreurs courantes** :
- ❌ ESP32 RX → STM32 RX (MAUVAIS)
- ❌ ESP32 TX → STM32 TX (MAUVAIS)
- ✅ ESP32 TX → STM32 RX (CORRECT)

#### B. Tester avec un câble plus court
- Longueur max recommandée : **30 cm** à 115200 bauds
- Au-delà : risque de dégradation du signal

#### C. Réduire le baudrate temporairement

**Sur ESP32** (`p4-edge-vision/main/main.c`) :
```c
#define BAUD_RATE 9600  // Au lieu de 115200
```

**Sur STM32** (`uplink-lorawan/main.cpp`) :
```cpp
UnbufferedSerial esp(PA_9, PA_10, 9600);  // Au lieu de 115200
UnbufferedSerial pc(USBTX, USBRX, 9600);  // Ou garder 115200 pour debug
```

Recompiler les deux et tester.

---

### 🔴 Problème 3 : CRC invalide systématique

**Symptôme** : `CRC_rcv` ≠ `CRC_calc` toujours

**Cause** : Données corrompues en transmission OU endianness

**Solutions** :

1. **Vérifier l'endianness** :
   Dans `protocol_uart.h`, la structure utilise `__attribute__((packed))`.
   Vérifiez que les deux côtés (ESP32 et STM32) ont le même ordre d'octets.

2. **Ajouter un délai sur ESP32** :
   Parfois le STM32 n'est pas prêt. Dans ESP32 `main.c` :
   ```c
   // Après uart_write_bytes
   vTaskDelay(pdMS_TO_TICKS(10));  // Attendre 10ms
   ```

3. **Vérifier le voltage** :
   - ESP32-P4 : 3.3V logic
   - STM32 L476 : 3.3V logic
   - ✅ Compatible, pas besoin de level shifter

---

### 🔴 Problème 4 : Bytes reçus mais aléatoires

**Symptôme** : Octets bruts affichés mais n'ont aucun sens

**Cause** : Baudrate désynchronisé

**Test rapide** :
Envoyez un caractère connu depuis l'ESP32 :
```c
uart_write_bytes(UART_PORT, "A", 1);  // 0x41 en ASCII
```

Sur le STM32, vous devriez voir :
```
41
```

Si vous voyez autre chose (ex: `C3`, `82`...), le baudrate est **FAUX**.

**Solution** :
- Vérifier les deux côtés utilisent **exactement** 115200
- Recompiler les deux programmes
- Reflasher les deux cartes

---

## 🧪 Test progressif

### Étape 1 : Test minimal ESP32

Remplacez temporairement le code ESP32 par :
```c
void app_main(void) {
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);
    uart_set_baudrate(UART_PORT, 115200);
    
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    while (1) {
        const char *test = "TEST\r\n";
        uart_write_bytes(UART_PORT, test, strlen(test));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**Résultat attendu sur STM32** :
```
54 45 53 54 0D 0A  (= "TEST\r\n")
54 45 53 54 0D 0A
...
```

Si ça marche → Le problème est dans la génération des trames.  
Si ça ne marche pas → Problème hardware/baudrate.

---

### Étape 2 : Test trame manuelle

Envoyez une trame valide manuellement depuis ESP32 :
```c
uint8_t test_frame[16] = {
    0xAA,  // SOF
    0x0F,  // LEN
    0x01,  // TYPE
    0x01,  // NODE_ID
    0x00, 0x00, 0x00, 0x01,  // TIMESTAMP = 1
    0x10,  // EVENT
    0x05,  // COUNT = 5
    0x5F,  // CONF = 95
    0x01,  // FLAGS
    0x00, 0x01,  // COUNTER = 1
    0x00, 0x00   // CRC (à calculer)
};

// Calculer le CRC
// (utilisez la fonction crc16_ccitt sur les 14 premiers octets)

uart_write_bytes(UART_PORT, test_frame, 16);
```

**Résultat attendu sur STM32** :
```
[SOF] 
[FRAME] AA 0F 01 01 00 00 00 01 10 05 5F 01 00 01 XX XX
[✓ VALID] Node:1 Count:5 Conf:95% Flags:0x01 Counter:1
```

---

## 📊 Résumé checklist

- [ ] **ESP32 flashé et tourne** (monitor idf.py montre des logs)
- [ ] **STM32 flashé et tourne** (LED clignote, "Listening..." affiché)
- [ ] **Baudrate identique** (115200 sur les deux)
- [ ] **Câblage correct** (TX→RX, pas TX→TX)
- [ ] **GND commun** (obligatoire)
- [ ] **Câble court** (< 30 cm recommandé)
- [ ] **Test "HELLO"** fonctionne
- [ ] **Octets bruts corrects** (pas de caractères bizarres)

---

## 🆘 Si rien ne fonctionne

1. **Tester avec un oscilloscope/analyseur logique**
   - Signal propre sur GPIO37 (ESP32 TX)
   - Signal reçu sur PA10 (STM32 RX)

2. **Utiliser un autre pin UART sur STM32**
   - Essayer USART3 par exemple
   - Modifier `main.cpp` en conséquence

3. **Revenir au code Mbed simple**
   - Juste afficher les octets reçus sans protocole
   - Vérifier la communication de base

---

**Après ces tests, vous devriez identifier la source du problème ! 🎯**
