# ttgo-lora32-sx1276

**Version : 1.0.0**

Sketch Arduino pour la carte **TTGO LoRa32 (SX1276)** : envoi et réception LoRa avec afficheur OLED SSD1306. Base tirée du tutoriel [Random Nerd Tutorials — TTGO LoRa32 SX1276](https://randomnerdtutorials.com/ttgo-lora32-sx1276-arduino-ide/).

## Matériel et environnement

- Carte compatible **ESP32** + **SX1276** (brochage TTGO LoRa32 classique).
- **Arduino IDE** (ou PlatformIO) avec support ESP32.
- Bibliothèques : **LoRa** (Sandeep Mistry), **Adafruit SSD1306**, **Adafruit GFX**, **Wire**, **SPI**.

## Émetteur (`ttgo-lora32-sender.ino`)

### Payload

- Le paquet transporte un **horodatage** : valeur de `millis()` en **chaîne ASCII décimale** (compatible avec `LoRa.readString()` côté récepteur).

### Intervalle d’émission et duty cycle (réglementation type UE 868 MHz)

- Le **temps en air (ToA)** est calculé à partir du **facteur d’étalement (SF)**, de la **bande passante (BW)**, de la longueur du payload, du **codage 4/5** (défaut de la librairie), en-tête explicite et CRC (formule alignée sur LoRaWAN / Semtech). L’optimisation bas débit (DE) est prise en compte pour **BW = 125 kHz** et **SF ≥ 11**.
- La **puissance TX** (`setTxPower`) ne modifie **pas** le ToA ; elle concerne surtout les limites **ERP** selon la réglementation.
- La **période minimale** entre deux **débuts** d’émission est :  
  `ToA / (DUTY_CYCLE_PERCENT / 100)`.  
  Après `endPacket()`, le délai appliqué est `période − ToA` pour respecter ce rythme.
- Réglage principal dans le fichier :

| Symbole | Rôle |
|--------|------|
| `DUTY_CYCLE_PERCENT` | Pourcentage de duty cycle autorisé (ex. `1.0f` pour 1 %, `0.1f` pour 0,1 % selon sous-bande). |
| `LORA_PREAMBLE_LEN` | Longueur du préambule ; doit rester **identique** à celle de la librairie (défaut 8 ; si vous utilisez `LoRa.setPreambleLength`, mettre la même valeur ici pour un ToA correct). |

Les paramètres radio utilisés pour le calcul et l’émission sont `currentSF`, `currentBW` et `currentPower` (réappliqués après `LoRa.begin()`).

## Récepteur (`ttgo-lora32-receiver.ino`)

- Reçoit une chaîne ASCII (timestamp émis) et l’affiche sur le moniteur série et l’OLED.
- Pour rester cohérent avec l’émetteur, garder les mêmes **fréquence**, **SF**, **BW** et **CR** (défaut 4/5).

## Historique des versions

### 1.0.0

- Émetteur : payload timestamp (`millis()`), espacement des envois basé sur **ToA** et **`DUTY_CYCLE_PERCENT`** ; affichage ToA et délai sur l’OLED.
- Initialisation : SF / BW / puissance appliqués après `LoRa.begin()` pour qu’ils ne soient pas perdus.
