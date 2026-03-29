# ttgo-lora32-sx1276

**Version : 1.0.2**

Sketch Arduino pour la carte **TTGO LoRa32 (SX1276)** : envoi et réception LoRa avec afficheur OLED SSD1306. Base tirée du tutoriel [Random Nerd Tutorials — TTGO LoRa32 SX1276](https://randomnerdtutorials.com/ttgo-lora32-sx1276-arduino-ide/).

## Matériel et environnement

- Carte compatible **ESP32** + **SX1276** (brochage TTGO LoRa32 classique).
- **Arduino IDE** (ou PlatformIO) avec support ESP32.
- Bibliothèques : **LoRa** (Sandeep Mistry), **Adafruit SSD1306**, **Adafruit GFX**, **Wire**, **SPI**.

## Émetteur (`ttgo-lora32-sender.ino`)

### Payload

- Le paquet transporte un **horodatage** basé sur `millis()` :
  - **Transmission compressée** : `uint32_t` sur **4 octets** (binaire).
  - **Affichage en clair** : format **`HH:MM:SS.mmm`** (uptime).

### Intervalle d’émission et duty cycle (réglementation type UE 868 MHz)

- Le **temps en air (ToA)** est calculé à partir du **facteur d’étalement (SF)**, de la **bande passante (BW)**, de la longueur du payload, du **codage 4/5** (défaut de la librairie), en-tête explicite et CRC (formule alignée sur LoRaWAN / Semtech). L’optimisation bas débit (DE) est prise en compte pour **BW = 125 kHz** et **SF ≥ 11**.
- La **puissance TX** (`setTxPower`) ne modifie **pas** le ToA ; elle concerne surtout les limites **ERP** selon la réglementation.
- La **période minimale** entre deux **dernières planifications d’envoi** (début logique de cycle) est :  
  `ToA / (DUTY_CYCLE_PERCENT / 100)`.  
  L’émetteur utilise une **boucle non bloquante** : le prochain envoi est planifié à `millis() + période` ; entre-temps, l’**OLED est rafraîchi en continu** (compte à rebours, derniers paramètres).
- Réglage principal dans le fichier :

| Symbole | Rôle |
|--------|------|
| `DUTY_CYCLE_PERCENT` | Pourcentage de duty cycle autorisé (ex. `1.0f` pour 1 %, `0.1f` pour 0,1 % selon sous-bande). |
| `LORA_PREAMBLE_LEN` | Longueur du préambule ; doit rester **identique** à celle de la librairie (défaut 8 ; si vous utilisez `LoRa.setPreambleLength`, mettre la même valeur ici pour un ToA correct). |

Les paramètres radio utilisés pour le calcul et l’émission sont `currentSF`, `currentBW` et `currentPower` (réappliqués après `LoRa.begin()`).

### Affichage OLED (émetteur)

- **Duty cycle** réglé (`DUTY_CYCLE_PERCENT`), **SF**, **BW**, **puissance**.
- **Trame / timestamp** : dernière payload envoyée (timestamp affiché en clair).
- **ToA** (ms), **période** minimale (ms), **délai restant** avant le prochain envoi (ms).

## Récepteur (`ttgo-lora32-receiver.ino`)

- Reçoit soit :
  - **binaire (4 octets)** : `uint32_t millis` (format actuel) ; affichage `millis (HH:MM:SS.mmm)`
  - **ASCII** : fallback si une ancienne version émet encore en texte
- Pour rester cohérent avec l’émetteur, garder les mêmes **fréquence**, **SF**, **BW** et **CR** (défaut 4/5).

## Historique des versions

### 1.0.2

- Payload : envoi du timestamp **compressé** (4 octets) et affichage **en clair** (`HH:MM:SS.mmm`) sur l’OLED ; récepteur compatible (décodage binaire + fallback ASCII).

### 1.0.1

- Émetteur : affichage OLED **mis à jour en continu** (duty %, SF/BW/PWR, dernière trame, ToA, période, temps restant) ; **pas de `delay()`** bloquant sur l’intervalle légal.

### 1.0.0

- Émetteur : payload timestamp (`millis()`), espacement des envois basé sur **ToA** et **`DUTY_CYCLE_PERCENT`** ; affichage ToA et délai sur l’OLED.
- Initialisation : SF / BW / puissance appliqués après `LoRa.begin()` pour qu’ils ne soient pas perdus.
