#define CAMERA_MODEL_XIAO_ESP32S3  // <-- WYMAGANE przed "camera_pins.h"
#include "camera_pins.h"

#include <esp_camera.h>
#include <SD.h>
#include <SPI.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include "ble_frame_structure.h"

// --- Konfiguracja pinów ---
constexpr uint8_t SD_CS   = 21;
constexpr uint8_t SD_SCK  = 7;
constexpr uint8_t SD_MISO = 8;
constexpr uint8_t SD_MOSI = 9;
SPIClass sdSPI(FSPI);

// NOWOŚĆ: Definicja pinu przycisku
const int buttonPin = 0; // Przycisk użytkownika na XIAO ESP32-S3 to GPIO0

// --- Konfiguracja BLE ---
#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

const char* NODE_B_NAME = "NODE_B";
const size_t PAYLOAD_SIZE = 180;

// --- Zmienne globalne ---
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pCharRx = nullptr;
BLERemoteCharacteristic* pCharTx = nullptr;

volatile uint8_t ackValue = 0x00;
unsigned long lastConnectAttempt = 0;
const unsigned long CONNECT_RETRY_INTERVAL = 3000;

int buttonState = HIGH;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50; 

// --- Funkcje ---
void notifyCB(BLERemoteCharacteristic* rc, uint8_t* data, size_t len, bool isNotify) {
  if (len > 0) { ackValue = data[0]; }
}

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
  if (esp_camera_init(&cfg) != ESP_OK) { Serial.println("[ERROR] Camera init failed"); while (true) delay(1000); }
  else { Serial.println("[OK] Camera initialized successfully"); }
}

static void sdInit() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 25000000)) { Serial.println("[ERROR] Inicjalizacja karty SD nie powiodła się!"); while (true) delay(1000); }
  if (SD.cardType() == CARD_NONE) { Serial.println("[ERROR] Nie wykryto karty SD"); while (true) delay(1000); }
  Serial.println("[OK] Karta SD zainicjalizowana pomyślnie.");
}

void initBLE() {
  BLEDevice::init("");
  pClient = BLEDevice::createClient();
}

void resetBLEClient() {
  if (pClient && pClient->isConnected()) { pClient->disconnect(); delay(200); }
}

bool connectToNodeB() {

  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true); scan->setInterval(100); scan->setWindow(99);
  BLEScanResults results = scan->start(3, false);
  for (int i = 0; i < results.getCount(); ++i) {
    BLEAdvertisedDevice adv = results.getDevice(i);
    if (adv.getName() == NODE_B_NAME) {
      if (!pClient->connect(&adv)) { Serial.println("[BLE] Connection failed"); return false; }
      delay(500);pClient->setMTU(200); delay(200);
      auto svc = pClient->getService(SERVICE_UUID);
      if (!svc) { Serial.println("[BLE] Service not found"); return false; }
      pCharRx = svc->getCharacteristic(CHAR_UUID_RX);
      pCharTx = svc->getCharacteristic(CHAR_UUID_TX);
      if (!pCharRx || !pCharTx) { Serial.println("[BLE] Chars not found"); return false; }
      if (pCharTx->canNotify()) { pCharTx->registerForNotify(notifyCB); Serial.println("[BLE] Wywołano registerForNotify dla TX."); }
      else { Serial.println("[BLE] TX nie obsługuje NOTIFY!"); return false; }
      return true;
    }
  }
  return false;
}
// Plik: NODE_A/src/main.cpp

void captureAndProcessImage() {
    Serial.println("\n[ACTION] Uruchamiam proces foto-transmisji...");
    
    const char* filePath = "/image.jpg";
    File f = SD.open(filePath, FILE_READ);
    if (!f) {
        Serial.printf("[ERROR] Nie można otworzyć pliku %s!\n", filePath);
        return;
    }

    uint32_t fileSize = f.size();
    if (fileSize == 0) {
        Serial.printf("[ERROR] Plik %s jest pusty!\n", filePath);
        f.close();
        return;
    }
    
    // ================== POCZĄTEK POPRAWKI ==================
    // Tworzymy nagłówek BLE DOKŁADNIE tak, jak w Twoim pliku .h
    BleHeader header;
    header.magic_start[0] = 0xAB;
    header.magic_start[1] = 0xCD;
    header.tx_type = BLE_TX_TYPE_FULL_IMAGE;
    header.data_size = fileSize;
    header.magic_end[0] = 0xEF;
    header.magic_end[1] = 0x12;
    // (Brak pola total_packets)
    // =================== KONIEC POPRAWKI ===================

    // Wysyłamy nagłówek
    pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
    Serial.println("[BLE] Wysłano nagłówek (bez oczekiwania na ACK).");
    
    // Dajemy Węzłowi B chwilę na przetworzenie nagłówka
    delay(50); 
    
    Serial.println("[BLE] Rozpoczynam wysyłanie danych obrazu...");
    uint8_t buf[PAYLOAD_SIZE];
    uint16_t totalPkts = (fileSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;

    for (uint16_t i = 0; i < totalPkts; ++i) {
        size_t len = f.read(buf, PAYLOAD_SIZE);
        if (len > 0) {
            pCharRx->writeValue(buf, len, false);
        }
        delay(5);
    }
    f.close();
    
    Serial.println("[BLE] Wszystkie pakiety wysłane (bez oczekiwania na finalny ACK).");
}
void sendImage(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { 
    Serial.printf("[sendImage] BŁĄD: Nie można otworzyć pliku %s!\n", path); 
    return; 
  }
  
  uint32_t fileSize = f.size();
  
  if (fileSize == 0) {
    Serial.printf("[sendImage] BŁĄD: Plik %s jest pusty lub nie istnieje! Anulowanie wysyłania.\n", path);
    f.close();
    return;
  }
  
  uint16_t totalPkts = (fileSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
  Serial.println("\n====================================");
  Serial.println("[sendImage] Rozpoczynam wysyłanie obrazu z karty SD");
  Serial.printf("[sendImage] Plik: %s, Rozmiar: %lu B, Pakiety: %u\n", path, fileSize, totalPkts);
  Serial.println("====================================");
  
  // ... reszta funkcji bez zmian ...
  uint8_t hdr[10] = { 0xAB, 0xCD, (uint8_t)(fileSize >> 24), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 8), (uint8_t)fileSize, (uint8_t)(totalPkts >> 8), (uint8_t)totalPkts, 0xEF, 0x12 };
  ackValue = 0x00;
  pCharRx->writeValue(hdr, sizeof(hdr), false);
  Serial.println("[sendImage] Wysłano nagłówek. Czekam na ACK...");
  unsigned long startWait = millis();
  while (ackValue != 0x01 && millis() - startWait < 2000) { delay(10); }
  if (ackValue != 0x01) { Serial.println("[sendImage] Timeout lub zły ACK dla nagłówka! Przerwanie."); f.close(); resetBLEClient(); return; }
  Serial.println("[sendImage] Otrzymano ACK dla nagłówka. Wysyłam dane...");
  uint8_t buf[PAYLOAD_SIZE];
  for (uint16_t i = 0; i < totalPkts; ++i) {
    size_t len = f.read(buf, PAYLOAD_SIZE);
    if (len > 0) { pCharRx->writeValue(buf, len, false); }
    delay(5);
  }
  f.close();
  Serial.println("[sendImage] Wszystkie pakiety wysłane. Czekam na finalny ACK...");
  startWait = millis();
  while (ackValue != 0xAA && millis() - startWait < 8000) { delay(10); }
  if (ackValue == 0xAA) { Serial.println("[sendImage] Otrzymano finalny ACK (0xAA). Transmisja udana."); }
  else { Serial.printf("[sendImage] Timeout na finalny ACK! Otrzymano: 0x%02X\n", ackValue); resetBLEClient(); }
  ackValue = 0x00;
}

// NOWOŚĆ: Funkcja do robienia zdjęcia, zapisu i wysyłania
// void captureAndSendImage() {
//   Serial.println("\n[ACTION] Wykryto wciśnięcie przycisku. Uruchamiam proces foto-transmisji...");
  
//   // 1. Zrób zdjęcie
//   camera_fb_t* fb = esp_camera_fb_get();
//   if (!fb) {
//     Serial.println("[ERROR] Nie udało się zrobić zdjęcia!");
//     return;
//   }
//   Serial.printf("[CAMERA] Zrobiono zdjęcie. Rozmiar: %zu bajtów\n", fb->len);

//   // 2. Zapisz zdjęcie na karcie SD
//   const char* photoPath = "/image.jpg";
//   // SD.remove(photoPath); // Usuń stary plik, jeśli istnieje
//   File photoFile = SD.open(photoPath, FILE_WRITE);
//   if (!photoFile) {
//     Serial.println("[ERROR] Nie udało się otworzyć pliku na karcie SD do zapisu!");
//     esp_camera_fb_return(fb); // WAŻNE: zawsze zwalniaj bufor!
//     return;
//   }
//   // photoFile.write(fb->buf, fb->len);
//   photoFile.close();
//   // Serial.printf("[SD] Zdjęcie zapisano jako %s\n", photoPath);

//   // 3. ZWOLNIJ BUFOR RAMKI - to jest absolutnie krytyczne!
//   esp_camera_fb_return(fb);

//   // 4. Wyślij zapisane zdjęcie przez BLE
//   sendImage(photoPath);
// }

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("Starting Node A (Button Trigger Version v2)...");

  // NOWOŚĆ: Konfiguracja pinu przycisku
  pinMode(buttonPin, INPUT_PULLUP);
  Serial.println("[OK] Przycisk na pinie GPIO0 skonfigurowany.");

  initCamera();
  sdInit();
  initBLE();
}

void loop() {
  // --- Zarządzanie połączeniem BLE (bez zmian) ---
  if (!pClient || !pClient->isConnected()) {
    unsigned long now = millis();
    if (now - lastConnectAttempt > CONNECT_RETRY_INTERVAL) {
      lastConnectAttempt = now;
      Serial.println("[BLE] Utracono połączenie. Próba ponownego połączenia...");
      resetBLEClient();
      initBLE();
      if (connectToNodeB()) { 
        Serial.println("[BLE] Połączono z NODE_B!"); 
        delay(500); 
        }
      else { Serial.println("[BLE] Nie udało się połączyć."); }
    }
    delay(100); 
    return; // Jeśli nie ma połączenia, nie sprawdzaj przycisku
  }
  
  // ZMIANA: Całkowicie nowa logika pętli oparta na przycisku
  
  // 1. Odczytaj stan przycisku
  int reading = digitalRead(buttonPin);

  // 2. Sprawdź, czy stan się zmienił (debouncing)
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  // 3. Po upływie czasu debouncingu, sprawdź stabilny stan
  if ((millis() - lastDebounceTime) > debounceDelay) {
    // Jeśli stan się ustabilizował i jest inny niż poprzednio
    if (reading != buttonState) {
      buttonState = reading;
      
      // Jeśli przycisk został wciśnięty (stan zmienił się na LOW)
      if (buttonState == LOW) {
        // sendImage("/image.jpg");
        captureAndProcessImage();
      }
    }
  }

  // 4. Zapisz obecny stan do następnej iteracji
  lastButtonState = reading;
}