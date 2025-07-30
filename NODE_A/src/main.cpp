#define CAMERA_MODEL_XIAO_ESP32S3  // <-- WYMAGANE przed "camera_pins.h"
#include "camera_pins.h"

#include <esp_camera.h>
#include <SD.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>

constexpr uint8_t SD_CS   = 21;
constexpr uint8_t SD_SCK  = 7;
constexpr uint8_t SD_MISO = 8;
constexpr uint8_t SD_MOSI = 9;
SPIClass sdSPI(FSPI);

#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

const char* NODE_B_NAME = "NODE_B";
const size_t PAYLOAD_SIZE = 180; // Zmniejsz, jeśli MTU jest mniejsze

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pCharRx = nullptr;
BLERemoteCharacteristic* pCharTx = nullptr;

// Używamy volatile, bo flaga jest modyfikowana w przerwaniu (callback)
volatile uint8_t ackValue = 0x00;

unsigned long lastConnectAttempt = 0;
const unsigned long CONNECT_RETRY_INTERVAL = 3000;

// --- ACK callback ---
void notifyCB(BLERemoteCharacteristic* rc, uint8_t* data, size_t len, bool isNotify) {
  if (len > 0) {
    ackValue = data[0]; // Zapisz otrzymaną wartość ACK
    // Serial.printf("[notifyCB] Otrzymano ACK: 0x%02X\n", ackValue); // Opcjonalny debug
  }
}

// --- inicjalizacja kamery ---
void initCamera() {
  camera_config_t cfg = {
    .pin_pwdn = PWDN_GPIO_NUM, .pin_reset = RESET_GPIO_NUM, .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM, .pin_sccb_scl = SIOC_GPIO_NUM, .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM, .pin_d5 = Y7_GPIO_NUM, .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM, .pin_d2 = Y4_GPIO_NUM, .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM, .pin_vsync = VSYNC_GPIO_NUM, .pin_href  = HREF_GPIO_NUM,
    .pin_pclk  = PCLK_GPIO_NUM, .xclk_freq_hz = 20000000, .pixel_format = PIXFORMAT_JPEG,
    .frame_size  = FRAMESIZE_HQVGA, .jpeg_quality = 20, .fb_count = 1
  };
  if (esp_camera_init(&cfg) != ESP_OK) {
    Serial.println("[ERROR] Camera init failed");
    while (true) delay(1000);
  } else {
    Serial.println("[OK] Camera initialized successfully");
  }
}

static void sdInit() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 25000000)) {
    Serial.println("[ERROR] Inicjalizacja karty SD nie powiodła się!");
    while (true) delay(1000);
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("[ERROR] Nie wykryto karty SD");
    while (true) delay(1000);
  }
  Serial.println("[OK] Karta SD zainicjalizowana pomyślnie.");
}

void initBLE() {
  BLEDevice::init("");
  pClient = BLEDevice::createClient();
}

void resetBLEClient() {
  if (pClient && pClient->isConnected()) {
    pClient->disconnect();
    delay(200);
  }
  // Nie usuwaj pClient, aby uniknąć problemów z ponownym użyciem
}

bool connectToNodeB() {
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(99);

  BLEScanResults results = scan->start(3, false);

  for (int i = 0; i < results.getCount(); ++i) {
    BLEAdvertisedDevice adv = results.getDevice(i);
    if (adv.getName() == NODE_B_NAME) {
      if (!pClient->connect(&adv)) {
        Serial.println("[BLE] Connection failed");
        return false;
      }

      pClient->setMTU(200); // Ustaw MTU
      delay(200);

      auto svc = pClient->getService(SERVICE_UUID);
      if (!svc) { Serial.println("[BLE] Service not found"); return false; }
      
      pCharRx = svc->getCharacteristic(CHAR_UUID_RX);
      pCharTx = svc->getCharacteristic(CHAR_UUID_TX);
      if (!pCharRx || !pCharTx) { Serial.println("[BLE] Chars not found"); return false; }

      if (pCharTx->canNotify()) {
        pCharTx->registerForNotify(notifyCB);
        Serial.println("[BLE] Wywołano registerForNotify dla TX.");
      } else {
        Serial.println("[BLE] TX nie obsługuje NOTIFY!");
        return false;
      }
      return true;
    }
  }
  return false;
}

void sendImage(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.println("[sendImage] Failed to open file");
    return;
  }

  uint32_t fileSize = f.size();
  uint16_t totalPkts = (fileSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;

  Serial.println("\n====================================");
  Serial.println("[sendImage] Rozpoczynam wysyłanie obrazu");
  Serial.printf("[sendImage] Rozmiar: %lu B, Pakiety: %u\n", fileSize, totalPkts);
  Serial.println("====================================");

  uint8_t hdr[10] = {
    0xAB, 0xCD, // Magiczny znacznik początku
    uint8_t(fileSize >> 24), uint8_t(fileSize >> 16),
    uint8_t(fileSize >> 8),  uint8_t(fileSize),
    uint8_t(totalPkts >> 8), uint8_t(totalPkts),
    0xEF, 0x12  // Magiczny znacznik końca
  };

  // 1. Wysłanie nagłówka i oczekiwanie na ACK
  ackValue = 0x00; // Zresetuj flagę ACK przed wysłaniem
  pCharRx->writeValue(hdr, sizeof(hdr), false); // Wyślij bez odpowiedzi
  Serial.println("[sendImage] Wysłano nagłówek. Czekam na ACK...");

  unsigned long startWait = millis();
  while (ackValue != 0x01 && millis() - startWait < 2000) { // Czekaj na ACK nagłówka (0x01)
    delay(10);
  }

  if (ackValue != 0x01) {
    Serial.println("[sendImage] Timeout lub zły ACK dla nagłówka! Przerwanie.");
    f.close();
    resetBLEClient(); // Zresetuj połączenie
    return;
  }
  Serial.println("[sendImage] Otrzymano ACK dla nagłówka. Wysyłam dane...");

  // 2. Wysyłanie pakietów danych
  uint8_t buf[PAYLOAD_SIZE];
  for (uint16_t i = 0; i < totalPkts; ++i) {
    size_t len = f.read(buf, PAYLOAD_SIZE);
    if (len > 0) {
      pCharRx->writeValue(buf, len, false);
    }
    if (i % 10 == 0 || i == totalPkts - 1) {
      Serial.printf("[sendImage] Wysłano pakiet %u/%u\n", i + 1, totalPkts);
    }
    delay(5); // Małe opóźnienie dla stabilności
  }
  f.close();
  Serial.println("[sendImage] Wszystkie pakiety wysłane. Czekam na finalny ACK...");

  // 3. Oczekiwanie na finalny ACK (0xAA)
  startWait = millis();
  while (ackValue != 0xAA && millis() - startWait < 8000) {
    delay(10);
  }

  if (ackValue == 0xAA) {
    Serial.println("[sendImage] Otrzymano finalny ACK (0xAA). Transmisja udana.");
  } else {
    Serial.printf("[sendImage] Timeout na finalny ACK! Otrzymano: 0x%02X\n", ackValue);
    resetBLEClient(); // Zresetuj połączenie
  }
  ackValue = 0x00; // Przygotuj na następną transmisję
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Czas na ustabilizowanie się portu szeregowego
  Serial.println("Starting...");
  initCamera();
  sdInit();
  initBLE();
}

void loop() {
  if (!pClient || !pClient->isConnected()) {
    unsigned long now = millis();
    if (now - lastConnectAttempt > CONNECT_RETRY_INTERVAL) {
      lastConnectAttempt = now;
      Serial.println("[BLE] Utracono połączenie. Próba ponownego połączenia...");
      resetBLEClient();
      initBLE(); // Utwórz nową instancję klienta
      if (connectToNodeB()) {
        Serial.println("[BLE] Połączono z NODE_B!");
      } else {
        Serial.println("[BLE] Nie udało się połączyć.");
      }
    }
    delay(100);
    return;
  }

  // Jeśli jesteśmy połączeni, wyślij obraz
  Serial.println("[SEND] Rozpoczynam transmisję obrazu");
  sendImage("/image.jpg");
  
  // Dłuższa przerwa, aby dać obu stronom czas na "odetchnięcie"
  Serial.println("[LOOP] Czekam 20 sekund przed następną transmisją...");
  delay(60000);
}