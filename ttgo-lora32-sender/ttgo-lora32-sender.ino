/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/ttgo-lora32-sx1276-arduino-ide/
*********/

//Libraries for LoRa
#include <SPI.h>
#include <LoRa.h>
#include <Preferences.h>
#include <math.h>
#include <string.h>

//Libraries for OLED Display
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//define the pins used by the LoRa transceiver module
#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 14
#define DIO0 26

//433E6 for Asia
//866E6 for Europe
//915E6 for North America
#define BAND 866E6

/** Pour 868 MHz (UE), souvent 1 % ou 0,1 % selon sous-bande — ajuster selon canal / règlement. */
#define DUTY_CYCLE_PERCENT 1.0f

/** Doit correspondre au réglage LoRa (défaut de la lib = 8). */
#define LORA_PREAMBLE_LEN 8

/** Fenêtre RX après chaque émission (ms) : pendant ce temps le receiver peut envoyer une commande radio. */
#define CFG_LISTEN_MS 1200

/** Paquet distant: magic, cmd, SF, code BW, P(dBm), xor — identique au receiver. */
#define CFG_MAGIC0 0xC0
#define CFG_MAGIC1 0xA7
#define CFG_CMD_SET_RADIO 0x01
#define CFG_PKT_LEN 7

/** Télémesure: version, millis LE (4o), SF, code BW, P(dBm), xor — reçu tel quel par le receiver. */
#define TELEM_V1 0x01
#define TELEM_V1_LEN 9

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

Preferences prefs;
int currentSF = 7;
long currentBW = 125E3;
int currentPower = 17;

// Etat d'affichage / émission (non-bloquant)
static char lastFrame[16] = "";
static char lastTimestampText[16] = "";
static uint32_t lastTxMillis = 0;
static uint32_t lastAirMs = 0;
static uint32_t lastPeriodMs = 0;
static uint32_t nextTxAtMs = 0;
static uint32_t lastDisplayAtMs = 0;
static uint32_t listenUntilMs = 0;
static uint32_t lastCfgAppliedAtMs = 0;
static bool cfgListenReceivePrimed = false;

static void formatUptime(uint32_t ms, char* out, size_t outSize) {
  const uint32_t totalSeconds = ms / 1000UL;
  const uint32_t hours = totalSeconds / 3600UL;
  const uint32_t minutes = (totalSeconds % 3600UL) / 60UL;
  const uint32_t seconds = totalSeconds % 60UL;
  const uint32_t millisPart = ms % 1000UL;
  snprintf(out, outSize, "%02lu:%02lu:%02lu.%03lu",
           (unsigned long)(hours % 100UL),
           (unsigned long)minutes,
           (unsigned long)seconds,
           (unsigned long)millisPart);
}

/** Temps en air (ms) — CR 4/5 (CR=1), en-tête explicite, CRC (formule LoRaWAN / Semtech). La puissance TX ne change pas le ToA. */
static uint32_t loraTimeOnAirMs(int sf, long bwHz, int plBytes) {
  const int cr = 1;
  const int h = 0;
  const int de = (bwHz == 125000 && sf >= 11) ? 1 : 0;
  const double ts = pow(2.0, sf) / (double)bwHz;
  const double tPreamble = (LORA_PREAMBLE_LEN + 4.25) * ts;
  const double denom = 4.0 * (sf - 2 * de);
  const double payloadBits = 8.0 * plBytes;
  const double q = (payloadBits - 4.0 * sf + 28.0 + 16.0 - 20.0 * h) / denom;
  double nCeil = ceil(q);
  if (nCeil < 0.0) {
    nCeil = 0.0;
  }
  const int symExtra = (int)nCeil * (cr + 4);
  const int payloadSymbNb = 8 + symExtra;
  const double tPayload = payloadSymbNb * ts;
  return (uint32_t)ceil((tPreamble + tPayload) * 1000.0);
}

static uint8_t radioCfgXor6(const uint8_t* b) {
  uint8_t x = 0;
  for (int i = 0; i < 6; i++) {
    x ^= b[i];
  }
  return x;
}

static void saveRadioPrefs() {
  prefs.begin("lora", false);
  prefs.putInt("sf", currentSF);
  prefs.putLong("bw", currentBW);
  prefs.putInt("pw", currentPower);
  prefs.end();
}

static void loadRadioPrefs() {
  prefs.begin("lora", true);
  currentSF = prefs.getInt("sf", currentSF);
  currentBW = prefs.getLong("bw", currentBW);
  currentPower = prefs.getInt("pw", currentPower);
  prefs.end();
  if (currentSF < 7) {
    currentSF = 7;
  }
  if (currentSF > 12) {
    currentSF = 12;
  }
  if (currentPower < 2) {
    currentPower = 2;
  }
  if (currentPower > 20) {
    currentPower = 20;
  }
}

static bool bwFromCode(uint8_t code, long* bwOut) {
  switch (code) {
    case 0:
      *bwOut = 125000;
      return true;
    case 1:
      *bwOut = 250000;
      return true;
    case 2:
      *bwOut = 500000;
      return true;
    default:
      return false;
  }
}

static bool bwCodeFromBw(long bwHz, uint8_t* codeOut) {
  if (bwHz == 125000) {
    *codeOut = 0;
    return true;
  }
  if (bwHz == 250000) {
    *codeOut = 1;
    return true;
  }
  if (bwHz == 500000) {
    *codeOut = 2;
    return true;
  }
  return false;
}

static uint8_t telemV1Xor8(const uint8_t* b) {
  uint8_t x = 0;
  for (int i = 0; i < 8; i++) {
    x ^= b[i];
  }
  return x;
}

/** Retourne true si une commande valide a été appliquée. */
static bool applyRadioCfgPacket(const uint8_t* b, int len) {
  if (len != CFG_PKT_LEN) {
    return false;
  }
  if (b[0] != CFG_MAGIC0 || b[1] != CFG_MAGIC1 || b[2] != CFG_CMD_SET_RADIO) {
    return false;
  }
  if (radioCfgXor6(b) != b[6]) {
    return false;
  }
  const int sf = (int)b[3];
  long bw = currentBW;
  if (!bwFromCode(b[4], &bw)) {
    return false;
  }
  const int pwr = (int)b[5];
  if (sf < 7 || sf > 12 || pwr < 2 || pwr > 20) {
    return false;
  }
  currentSF = sf;
  currentBW = bw;
  currentPower = pwr;
  LoRa.setSpreadingFactor(currentSF);
  LoRa.setSignalBandwidth(currentBW);
  LoRa.setTxPower(currentPower);
  saveRadioPrefs();
  lastCfgAppliedAtMs = millis();
  nextTxAtMs = millis();
  Serial.print(F("Config recue: SF="));
  Serial.print(currentSF);
  Serial.print(F(" BW="));
  Serial.print((int)(currentBW / 1000));
  Serial.print(F("k P="));
  Serial.println(currentPower);
  return true;
}

void setup() {
  Serial.begin(115200);

  //reset OLED display via software
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(20);
  digitalWrite(OLED_RST, HIGH);

  //initialize OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3c, false, false)) { // Address 0x3C for 128x32
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("LORA SENDER ");
  display.display();
  
  Serial.println("LoRa Sender Test");

  //SPI LoRa pins
  SPI.begin(SCK, MISO, MOSI, SS);
  //setup LoRa transceiver module
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  loadRadioPrefs();
  LoRa.setSpreadingFactor(currentSF);
  LoRa.setSignalBandwidth(currentBW);
  LoRa.setTxPower(currentPower);

  Serial.println("LoRa Initializing OK!");
  display.setCursor(0,10);
  display.print("LoRa Initializing OK!");
  display.display();
  delay(2000);

  // Première émission immédiate
  nextTxAtMs = millis();
}

void loop() {
  const uint32_t now = millis();

  if (listenUntilMs != 0 && (int32_t)(now - listenUntilMs) >= 0) {
    listenUntilMs = 0;
    cfgListenReceivePrimed = false;
  }

  const bool listening = (listenUntilMs != 0);

  if (listening) {
    if (!cfgListenReceivePrimed) {
      LoRa.receive();
      cfgListenReceivePrimed = true;
    }
    const int ps = LoRa.parsePacket();
    if (ps >= CFG_PKT_LEN) {
      uint8_t cfg[CFG_PKT_LEN];
      int n = 0;
      while (LoRa.available() && n < CFG_PKT_LEN) {
        cfg[n++] = (uint8_t)LoRa.read();
      }
      if (applyRadioCfgPacket(cfg, n)) {
        listenUntilMs = 0;
        cfgListenReceivePrimed = false;
      }
    }
  } else {
    // Emettre seulement quand autorisé par l'intervalle calculé
    if ((int32_t)(now - nextTxAtMs) >= 0) {
      lastTxMillis = now;
      formatUptime(lastTxMillis, lastTimestampText, sizeof(lastTimestampText));
      snprintf(lastFrame, sizeof(lastFrame), "%lu", (unsigned long)lastTxMillis);

      uint8_t payload[TELEM_V1_LEN];
      int plBytes = TELEM_V1_LEN;
      uint8_t bwCode = 0;
      if (!bwCodeFromBw(currentBW, &bwCode)) {
        plBytes = 4;
        payload[0] = (uint8_t)(lastTxMillis & 0xFFUL);
        payload[1] = (uint8_t)((lastTxMillis >> 8) & 0xFFUL);
        payload[2] = (uint8_t)((lastTxMillis >> 16) & 0xFFUL);
        payload[3] = (uint8_t)((lastTxMillis >> 24) & 0xFFUL);
        Serial.println(F("WARN: BW hors 125/250/500k -> paquet 4o (sans setup)"));
      } else {
        payload[0] = TELEM_V1;
        payload[1] = (uint8_t)(lastTxMillis & 0xFFUL);
        payload[2] = (uint8_t)((lastTxMillis >> 8) & 0xFFUL);
        payload[3] = (uint8_t)((lastTxMillis >> 16) & 0xFFUL);
        payload[4] = (uint8_t)((lastTxMillis >> 24) & 0xFFUL);
        payload[5] = (uint8_t)currentSF;
        payload[6] = bwCode;
        payload[7] = (uint8_t)currentPower;
        payload[8] = telemV1Xor8(payload);
      }

      lastAirMs = loraTimeOnAirMs(currentSF, currentBW, plBytes);
      const float duty = DUTY_CYCLE_PERCENT / 100.0f;
      lastPeriodMs = (uint32_t)ceilf((float)lastAirMs / duty);
      nextTxAtMs = now + lastPeriodMs;

      Serial.print(F("Envoi timestamp ms="));
      Serial.print(lastTxMillis);
      Serial.print(F(" ("));
      Serial.print(lastTimestampText);
      Serial.print(F(")"));
      Serial.print(F(" ToA="));
      Serial.print(lastAirMs);
      Serial.print(F("ms periode>="));
      Serial.print(lastPeriodMs);
      Serial.println(F("ms"));

      LoRa.beginPacket();
      LoRa.write(payload, (size_t)plBytes);
      LoRa.endPacket();

      listenUntilMs = now + CFG_LISTEN_MS;
      cfgListenReceivePrimed = false;
    }
  }

  // Rafraîchir l'écran en continu (sans bloquer)
  if ((uint32_t)(now - lastDisplayAtMs) >= 200) {
    lastDisplayAtMs = now;
    const uint32_t remainingMs =
        ((int32_t)(nextTxAtMs - now) > 0) ? (uint32_t)(nextTxAtMs - now) : 0;

    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("LORA SENDER ");
    display.print(DUTY_CYCLE_PERCENT, 1);
    display.print("%");

    display.setCursor(0, 10);
    display.print("SF");
    display.print(currentSF);
    display.print(" BW");
    display.print((int)(currentBW / 1000));
    display.print("k P");
    display.print(currentPower);

    display.setCursor(0, 20);
    // IMPORTANT: beaucoup de TTGO sont en 128x32 malgré la config 128x64.
    // On garde le timestamp "en clair" sur une ligne toujours visible.
    display.print("ts=");
    display.print(lastTimestampText[0] ? lastTimestampText : "--:--:--.---");

    if (SCREEN_HEIGHT >= 64) {
      display.setCursor(0, 30);
      display.print("ToA ");
      display.print(lastAirMs);
      display.print("ms");

      display.setCursor(0, 40);
      display.print("per ");
      display.print(lastPeriodMs);
      display.print("ms");

      display.setCursor(0, 50);
      display.print("next ");
      display.print(remainingMs);
      display.print("ms");
    } else {
      // 128x32: ne pas ajouter de ligne supplémentaire.
      // (Les lignes utiles restent: titre, paramètres, timestamp en clair.)
    }

    if (listenUntilMs != 0 && (int32_t)(now - listenUntilMs) < 0 && SCREEN_HEIGHT >= 64) {
      display.setCursor(62, 50);
      display.print("RX");
    }

    if (lastCfgAppliedAtMs != 0 && (uint32_t)(now - lastCfgAppliedAtMs) < 4000) {
      display.setCursor(90, 0);
      display.print("CFG");
    }

    display.display();
  }
}
