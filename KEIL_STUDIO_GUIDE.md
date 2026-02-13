# 🚀 Guide Keil Studio Cloud - STM32 UART Receiver

## 📋 Configuration du projet

### 1. Créer le projet dans Keil Studio Cloud

1. Aller sur https://studio.keil.arm.com/
2. **File → New → Mbed Project**
3. Sélectionner **Mbed OS 6** (dernière version stable)
4. Choisir votre carte : **NUCLEO-L476RG** (ou votre modèle)
5. Nommer le projet : `stm32-uart-receiver`

### 2. Importer les fichiers

Dans le workspace Keil Studio :

```
stm32-uart-receiver/
├── main.cpp           ← Copier le contenu de main_mbed.cpp
├── protocol_uart.h    ← Copier ce fichier
└── mbed_app.json      ← Copier ce fichier
```

**Supprimer** le `main.cpp` généré automatiquement et **créer** les 3 fichiers ci-dessus.

### 3. Contenu des fichiers

#### ✅ `main.cpp`
Copier le contenu de `main_mbed.cpp`

#### ✅ `protocol_uart.h`
Copier le fichier protocol_uart.h (déjà créé)

#### ✅ `mbed_app.json`
```json
{
    "target_overrides": {
        "*": {
            "target.printf_lib": "std",
            "platform.stdio-baud-rate": 115200
        }
    }
}
```

---

## 🔌 Branchements STM32 Nucleo

```
ESP32-P4              STM32 Nucleo L476RG
━━━━━━━━              ━━━━━━━━━━━━━━━━━━━
GPIO37 (TX) ────────→ PA10 (D2 - Arduino pin)
GND         ────────  GND
```

**Important** : 
- ESP32 TX → STM32 RX (PA10)
- GND commun obligatoire
- Pas de résistance nécessaire

---

## 🔨 Compilation et Flash

### Dans Keil Studio Cloud

1. Cliquer sur **Build** (⚙️ en haut)
2. Attendre la compilation (~30s)
3. Le fichier `.bin` sera téléchargé automatiquement

### Flasher la carte

**Option A : Drag & Drop**
1. Brancher le STM32 Nucleo en USB
2. Un lecteur `NODE_L476RG` apparaît
3. Glisser-déposer le fichier `.bin` dedans
4. Attendre que la LED ST-LINK clignote
5. La carte redémarre automatiquement

**Option B : Via Keil Studio**
1. Brancher le STM32
2. Cliquer sur **Run** (▶️)
3. Sélectionner votre carte
4. Le flash est automatique

---

## 📊 Monitorer la sortie

### Terminal série

**Option 1 : Terminal Keil Studio**
1. Cliquer sur **Serial Monitor** (📟)
2. Sélectionner le port ST-LINK
3. Baudrate : **115200**
4. Vous devriez voir :

```
╔════════════════════════════════════════╗
║  STM32 UART Receiver - Protocol v1.0  ║
║  ESP32-P4 → STM32 → LoRaWAN (Mbed)   ║
╚════════════════════════════════════════╝

Listening on PA10 (RX from ESP32)...
```

**Option 2 : PuTTY / Tera Term**
- Port : COM port du ST-LINK
- Baudrate : 115200
- 8N1 (8 bits, No parity, 1 stop)

---

## ✅ Test complet

1. **Flasher le STM32** avec le code Mbed
2. **Ouvrir le terminal série** (115200 bauds)
3. **Flasher l'ESP32-P4** avec le code émetteur
4. **Connecter** ESP32 TX → STM32 RX + GND
5. **Observer** les messages :

```
[RX] Node:1 Count:5 Conf:95% Flags:0x01
[RX] Node:1 Count:6 Conf:92% Flags:0x01
[RX] Node:1 Count:7 Conf:88% Flags:0x01

=== Stats ===
Total: 3 | Valid: 3 | CRC err: 0
```

---

## 🐛 Dépannage Keil Studio

### Erreur de compilation
- Vérifier que Mbed OS 6 est sélectionné
- Vérifier que `protocol_uart.h` est bien dans le projet
- Nettoyer le build : **Clean Build**

### Pas de sortie série
- Vérifier que le ST-LINK est bien connecté
- Vérifier le baudrate : 115200
- Vérifier que la LED verte du Nucleo clignote (programme tourne)

### LED ne clignote pas
- Vérifier que l'ESP32 envoie bien des données
- Utiliser le terminal pour voir les octets bruts
- Vérifier les connexions TX→RX et GND

---

## 📝 Différences Mbed vs HAL

| Aspect | Mbed OS | STM32 HAL |
|--------|---------|-----------|
| **Environnement** | Keil Studio Cloud ✅ | STM32CubeIDE |
| **Langage** | C++ | C |
| **UART** | `UnbufferedSerial` | `HAL_UART_*` |
| **Délai** | `ThisThread::sleep_for()` | `HAL_Delay()` |
| **Portabilité** | ++ | + |

---

## 🎯 Avantages Keil Studio Cloud

- ✅ **En ligne** : Pas d'installation logiciel
- ✅ **Multi-plateforme** : Windows, Mac, Linux
- ✅ **Mbed OS** : API simple et portable
- ✅ **Debugging** : Intégré dans le navigateur
- ✅ **Git intégré** : Versionning facile

---

## 🚀 Prochaines étapes

1. ✅ Copier les 3 fichiers dans Keil Studio
2. ✅ Compiler (Build)
3. ✅ Flasher la carte
4. ✅ Tester la communication ESP32 → STM32
5. 🔜 Intégrer LoRaWAN (ajouter lib LoRaWAN Mbed)

**Votre projet est prêt pour Keil Studio Cloud ! 🎉**
