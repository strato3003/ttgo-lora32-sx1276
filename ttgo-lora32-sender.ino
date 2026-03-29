/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/ttgo-lora32-sx1276-arduino-ide/
*********/

//Libraries for LoRa
#include <SPI.h>
#include <LoRa.h>
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

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

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

  // Emettre seulement quand autorisé par l'intervalle calculé
  if ((int32_t)(now - nextTxAtMs) >= 0) {
    lastTxMillis = now;
    formatUptime(lastTxMillis, lastTimestampText, sizeof(lastTimestampText));
    snprintf(lastFrame, sizeof(lastFrame), "%lu", (unsigned long)lastTxMillis);

    // Payload compressé: 4 octets (uint32_t millis)
    const int plBytes = 4;
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
    LoRa.write((const uint8_t*)&lastTxMillis, sizeof(lastTxMillis));
    LoRa.endPacket();
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
    display.print("ts:");
    display.print(lastTimestampText[0] ? lastTimestampText : "-");

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

    display.display();
  }
}
