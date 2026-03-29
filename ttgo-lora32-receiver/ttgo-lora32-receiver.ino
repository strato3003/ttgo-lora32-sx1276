/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/ttgo-lora32-sx1276-arduino-ide/
*********/

//Libraries for LoRa
#include <SPI.h>
#include <LoRa.h>
#include <stdlib.h>
#include <stdio.h>
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

/** Commande radio vers l'émetteur (même format que le sender attend). */
#define CFG_MAGIC0 0xC0
#define CFG_MAGIC1 0xA7
#define CFG_CMD_SET_RADIO 0x01
#define CFG_PKT_LEN 7

/** Télémesure v1 (identique sender). */
#define TELEM_V1 0x01
#define TELEM_V1_LEN 9

//OLED pins
#define OLED_SDA 4
#define OLED_SCL 15 
#define OLED_RST 16
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

String LoRaData;
static char tsText[16] = "";
static bool lastDecodedMillis = false;
static uint32_t lastMs = 0;
static bool lastPacketWasRadioCfg = false;
static bool lastPacketWasTelemV1 = false;
static int lastRxSf = 0;
static int lastRxBwKhz = 0;
static int lastRxPwr = 0;

static char serialLine[48];
static uint8_t serialLineLen = 0;

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

/** 4 octets LE (uint32_t millis) ou ASCII décimal (émetteur ancien / copie de sketch). */
static bool tryDecodeMillisMs(const uint8_t* buf, int len, uint32_t* outMs) {
  // binaire: accepter exactement 4 octets, ou >=4 si le reste est padding/whitespace
  if (len >= 4) {
    bool restOk = true;
    for (int i = 4; i < len; i++) {
      const uint8_t c = buf[i];
      if (!(c == 0x00 || c == ' ' || c == '\r' || c == '\n' || c == '\t')) {
        restOk = false;
        break;
      }
    }
    if (restOk) {
      *outMs = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
      return true;
    }
  }
  int s = 0;
  while (s < len && (buf[s] == ' ' || buf[s] == '\r' || buf[s] == '\n' || buf[s] == '\t')) {
    s++;
  }
  int e = len - 1;
  while (e >= s && (buf[e] == ' ' || buf[e] == '\r' || buf[e] == '\n' || buf[e] == '\t' || buf[e] == '\0')) {
    e--;
  }
  const int tlen = e - s + 1;
  if (tlen <= 0 || tlen > 10) {
    return false;
  }
  for (int i = 0; i < tlen; i++) {
    const uint8_t c = buf[s + i];
    if (c < '0' || c > '9') {
      return false;
    }
  }
  char tmp[12];
  memcpy(tmp, buf + s, (size_t)tlen);
  tmp[tlen] = '\0';
  *outMs = (uint32_t)strtoul(tmp, nullptr, 10);
  return true;
}

static uint8_t telemV1Xor8(const uint8_t* b) {
  uint8_t x = 0;
  for (int i = 0; i < 8; i++) {
    x ^= b[i];
  }
  return x;
}

static bool bwKhzFromCode(uint8_t code, int* bwKhzOut) {
  switch (code) {
    case 0:
      *bwKhzOut = 125;
      return true;
    case 1:
      *bwKhzOut = 250;
      return true;
    case 2:
      *bwKhzOut = 500;
      return true;
    default:
      return false;
  }
}

/** Timestamp + SF/BW/P utilisés par l'émetteur pour ce paquet (pour essais portée). */
static bool parseTelemV1(const uint8_t* b, int len, uint32_t* ms, int* sf, int* bwKhz, int* pwr) {
  if (len != TELEM_V1_LEN || b[0] != TELEM_V1) {
    return false;
  }
  if (telemV1Xor8(b) != b[8]) {
    return false;
  }
  *ms = (uint32_t)b[1] | ((uint32_t)b[2] << 8) | ((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 24);
  *sf = (int)b[5];
  if (*sf < 7 || *sf > 12) {
    return false;
  }
  if (!bwKhzFromCode(b[6], bwKhz)) {
    return false;
  }
  *pwr = (int)b[7];
  if (*pwr < 2 || *pwr > 20) {
    return false;
  }
  return true;
}

static uint8_t radioCfgXor6(const uint8_t* b) {
  uint8_t x = 0;
  for (int i = 0; i < 6; i++) {
    x ^= b[i];
  }
  return x;
}

static uint8_t bwCodeFromKhz(int bwKhz) {
  if (bwKhz == 125) {
    return 0;
  }
  if (bwKhz == 250) {
    return 1;
  }
  if (bwKhz == 500) {
    return 2;
  }
  return 255;
}

static bool buildRadioCfgPacket(uint8_t* out, int sf, int bwKhz, int pwrDbm) {
  const uint8_t bc = bwCodeFromKhz(bwKhz);
  if (bc == 255 || sf < 7 || sf > 12 || pwrDbm < 2 || pwrDbm > 20) {
    return false;
  }
  out[0] = CFG_MAGIC0;
  out[1] = CFG_MAGIC1;
  out[2] = CFG_CMD_SET_RADIO;
  out[3] = (uint8_t)sf;
  out[4] = bc;
  out[5] = (uint8_t)pwrDbm;
  out[6] = radioCfgXor6(out);
  return true;
}

static bool isRadioCfgPacket(const uint8_t* b, int len) {
  if (len != CFG_PKT_LEN) {
    return false;
  }
  if (b[0] != CFG_MAGIC0 || b[1] != CFG_MAGIC1 || b[2] != CFG_CMD_SET_RADIO) {
    return false;
  }
  return radioCfgXor6(b) == b[6];
}

/** Applique SF/BW/P sur la radio locale pour recevoir comme l'émetteur après sa prise en compte du CFG. */
static void applyLocalReceiverRadio(int sf, int bwKhz, int pwrDbm) {
  if (bwCodeFromKhz(bwKhz) == 255 || sf < 7 || sf > 12 || pwrDbm < 2 || pwrDbm > 20) {
    return;
  }
  const long bwHz = (long)bwKhz * 1000L;
  LoRa.setSpreadingFactor(sf);
  LoRa.setSignalBandwidth(bwHz);
  LoRa.setTxPower(pwrDbm);
  LoRa.receive();
  Serial.print(F("Receiver local: SF="));
  Serial.print(sf);
  Serial.print(F(" BW="));
  Serial.print(bwKhz);
  Serial.print(F("k P="));
  Serial.println(pwrDbm);
}

static void sendRadioConfigLoRa(int sf, int bwKhz, int pwrDbm) {
  uint8_t pkt[CFG_PKT_LEN];
  if (!buildRadioCfgPacket(pkt, sf, bwKhz, pwrDbm)) {
    Serial.println(F("CFG invalide: SF 7..12, BW 125|250|500 kHz, P 2..20"));
    return;
  }
  /* Emission avec la config actuelle du receiver : l'émetteur écoute encore sur l'ancienne config. */
  LoRa.beginPacket();
  LoRa.write(pkt, sizeof(pkt));
  LoRa.endPacket();
  Serial.print(F("CFG LoRa envoye: SF="));
  Serial.print(sf);
  Serial.print(F(" BW="));
  Serial.print(bwKhz);
  Serial.print(F(" P="));
  Serial.println(pwrDbm);
  /* Puis alignement du receiver sur la cible pour les trames suivantes (télémesure v1). */
  applyLocalReceiverRadio(sf, bwKhz, pwrDbm);
}

static void handleSerialLine(const char* line) {
  int sf = 0;
  int bw = 0;
  int pwr = 0;
  if (sscanf(line, "CFG %d %d %d", &sf, &bw, &pwr) == 3 ||
      sscanf(line, "cfg %d %d %d", &sf, &bw, &pwr) == 3) {
    sendRadioConfigLoRa(sf, bw, pwr);
    return;
  }
  Serial.println(F("Tape: CFG <SF> <BW_kHz> <P_dBm>  ex: CFG 9 125 14"));
}

void setup() { 
  //initialize Serial Monitor
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
  display.print("LORA RECEIVER ");
  display.display();

  Serial.println("LoRa Receiver Test");
  
  //SPI LoRa pins
  SPI.begin(SCK, MISO, MOSI, SS);
  //setup LoRa transceiver module
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  LoRa.receive();
  Serial.println("LoRa Initializing OK!");
  display.setCursor(0,10);
  display.println("LoRa Initializing OK!");
  display.display();  

  Serial.println(F("CFG <SF> <BW_kHz> <P_dBm>  ex: CFG 9 125 14  (emetteur + ce receiver)"));
}

void loop() {

  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialLineLen > 0) {
        serialLine[serialLineLen] = '\0';
        handleSerialLine(serialLine);
        serialLineLen = 0;
      }
    } else if (serialLineLen < sizeof(serialLine) - 1) {
      serialLine[serialLineLen++] = c;
    }
  }

  //try to parse packet
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    //received a packet
    Serial.print("Received packet ");

    uint8_t payload[32];
    int plen = 0;
    while (LoRa.available() && plen < (int)sizeof(payload)) {
      payload[plen++] = (uint8_t)LoRa.read();
    }

    lastPacketWasRadioCfg = isRadioCfgPacket(payload, plen);
    lastPacketWasTelemV1 = false;
    if (lastPacketWasRadioCfg) {
      lastDecodedMillis = false;
      tsText[0] = '\0';
      lastMs = 0;
      LoRaData = "CFG air";
      Serial.print(LoRaData);
    } else if (parseTelemV1(payload, plen, &lastMs, &lastRxSf, &lastRxBwKhz, &lastRxPwr)) {
      lastDecodedMillis = true;
      lastPacketWasTelemV1 = true;
      formatUptime(lastMs, tsText, sizeof(tsText));
      LoRaData = String(lastMs) + " (" + String(tsText) + ")";
      Serial.print(LoRaData);
      Serial.print(F(" air:SF"));
      Serial.print(lastRxSf);
      Serial.print(F(" BW"));
      Serial.print(lastRxBwKhz);
      Serial.print(F(" P"));
      Serial.print(lastRxPwr);
    } else {
      lastDecodedMillis = tryDecodeMillisMs(payload, plen, &lastMs);
      if (lastDecodedMillis) {
        formatUptime(lastMs, tsText, sizeof(tsText));
        LoRaData = String(lastMs) + " (" + String(tsText) + ")";
      } else {
        tsText[0] = '\0';
        LoRaData = "";
        for (int i = 0; i < plen; i++) {
          LoRaData += (char)payload[i];
        }
      }
      Serial.print(LoRaData);
    }

    //print RSSI of packet
    int rssi = LoRa.packetRssi();
    Serial.print(" with RSSI ");    
    Serial.println(rssi);

   // Dsiplay information
   display.clearDisplay();
   display.setCursor(0,0);
   display.print("LORA RECEIVER");
   // Affichage: timestamp "en clair" si millis décodé (binaire ou ASCII chiffres)
   if (SCREEN_HEIGHT >= 64) {
     display.setCursor(0, 12);
     display.print("ts=");
     display.print(lastPacketWasRadioCfg ? "-" : (lastDecodedMillis ? tsText : "-"));

     display.setCursor(0, 22);
     if (lastPacketWasRadioCfg) {
       display.print("pkt CFG");
     } else if (lastPacketWasTelemV1) {
       display.print("S");
       display.print(lastRxSf);
       display.print(" ");
       display.print(lastRxBwKhz);
       display.print("k P");
       display.print(lastRxPwr);
     } else if (lastDecodedMillis) {
       display.print("ms=");
       display.print((unsigned long)lastMs);
     } else {
       display.print("msg=");
       display.print(LoRaData);
     }

     display.setCursor(0, 32);
     if (lastPacketWasTelemV1) {
       display.print("ms=");
       display.print((unsigned long)lastMs);
       display.print(" R");
       display.print(rssi);
     } else {
       display.print("RSSI:");
       display.print(rssi);
     }
   } else {
     // 128x32: ts / setup ou msg / RSSI
     display.setCursor(0, 10);
     display.print("ts=");
     display.print(lastPacketWasRadioCfg ? "-" : (lastDecodedMillis ? tsText : "-"));

     display.setCursor(0, 20);
     if (lastPacketWasRadioCfg) {
       display.print("CFG");
     } else if (lastPacketWasTelemV1) {
       display.print("S");
       display.print(lastRxSf);
       display.print(" ");
       display.print(lastRxBwKhz);
       display.print("k P");
       display.print(lastRxPwr);
     } else if (lastDecodedMillis) {
       display.print("ms=");
       display.print((unsigned long)lastMs);
     } else {
       display.print("msg=");
       display.print(LoRaData);
     }

     display.setCursor(0, 30);
     display.print("R");
     display.print(rssi);
   }
   display.display();   
  }
}