#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector> // Do buforowania obrazu

// Definicje UUID dla serwisu i charakterystyk BLE
#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// Fazy odbioru danych
enum RxPhase {
  WAITING_FOR_HEADER,
  RECEIVING_DATA,
  TRANSMISSION_COMPLETE
};

// Struktura przechowująca stan odbiornika
struct RxState {
  RxPhase  phase           = WAITING_FOR_HEADER;
  uint32_t fileSize        = 0;
  uint32_t bytesRemaining  = 0;
  uint16_t totalPkts       = 0;
  uint16_t receivedPkts    = 0;
  uint8_t  hdrBuf[10];
  size_t   hdrGot          = 0;
  unsigned long lastActivity = 0;
} rx;

// Globalny bufor na odebrane bajty obrazu
std::vector<uint8_t> imageBuffer;

// Wskaźnik na charakterystykę TX do wysyłania powiadomień
BLECharacteristic* pCharTx = nullptr;
const uint32_t MAX_FILE_SIZE = 5000000; // Limit rozmiaru pliku: 5MB
const unsigned long TIMEOUT_MS = 15000; // Timeout bezczynności: 15 sekund

// Funkcja do wysyłania potwierdzeń (ACK) do klienta
static void sendAck(uint8_t code) {
  if (pCharTx) {
    pCharTx->setValue(&code, 1);
    pCharTx->notify();
  }
}

// Funkcja resetująca maszynę stanów odbiornika
static void resetRxState() {
  Serial.println("[RESET] Resetowanie stanu odbiornika");
  rx.phase = WAITING_FOR_HEADER;
  rx.fileSize = 0;
  rx.bytesRemaining = 0;
  rx.totalPkts = 0;
  rx.receivedPkts = 0;
  rx.hdrGot = 0;
  rx.lastActivity = millis();
  memset(rx.hdrBuf, 0, sizeof(rx.hdrBuf));
  imageBuffer.clear(); // Czyść również bufor obrazu
}

// Funkcja przesyłająca zbuforowany obraz do komputera przez port szeregowy
void sendImageToComputer() {
  if (imageBuffer.empty()) {
    Serial.println("[PC] Błąd: Bufor obrazu jest pusty!");
    return;
  }
  
  Serial.println("\n[PC] Inicjuję transfer obrazu do komputera...");
  
  // Prosty protokół komunikacyjny z PC:
  // 1. Znacznik startu
  // 2. Rozmiar obrazu w nowej linii
  // 3. Surowe dane obrazu
  
  Serial.print("START_IMAGE_TRANSFER\n");
  Serial.print(imageBuffer.size());
  Serial.print("\n");
  Serial.write(imageBuffer.data(), imageBuffer.size());
  
  Serial.println("\n[PC] Transfer do komputera zakończony.");
}


// Funkcja walidująca poprawność nagłówka
static bool validateHeader(const uint8_t* hdr) {
  if (hdr[0] != 0xAB || hdr[1] != 0xCD) {
    Serial.printf("[VALIDATE] Błędny znacznik początku: 0x%02X 0x%02X\n", hdr[0], hdr[1]);
    return false;
  }
  if (hdr[8] != 0xEF || hdr[9] != 0x12) {
    Serial.printf("[VALIDATE] Błędny znacznik końca: 0x%02X 0x%02X\n", hdr[8], hdr[9]);
    return false;
  }
  uint32_t size = (uint32_t(hdr[2]) << 24) | (uint32_t(hdr[3]) << 16) |
                  (uint32_t(hdr[4]) << 8)  | uint32_t(hdr[5]);
  if (size == 0 || size > MAX_FILE_SIZE) {
    Serial.printf("[VALIDATE] Nieprawidłowy rozmiar pliku: %lu\n", size);
    return false;
  }
  return true;
}

// Klasa obsługująca zdarzenia serwera BLE (połączenie/rozłączenie)
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    Serial.println("[BLE] Połączono z klientem (NODE_A)");
    resetRxState(); // Resetuj stan przy każdym nowym połączeniu
  }
  void onDisconnect(BLEServer* pServer) override {
    Serial.println("[BLE] Rozłączono klienta.");
    BLEDevice::startAdvertising(); // Włącz ponowne rozgłaszanie
    Serial.println("[BLE] Oczekiwanie na nowe połączenie...");
  }
};

// Klasa obsługująca zdarzenia charakterystyki RX (zapis danych)
class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* chr) override {
    std::string data = chr->getValue();
    const uint8_t* p = (const uint8_t*)data.data();
    size_t len = data.length();
    
    rx.lastActivity = millis();

    // Jeśli poprzednia transmisja została zakończona, a otrzymujemy nowe dane,
    // zresetuj stan, aby przygotować się na nowy transfer.
    if (rx.phase == TRANSMISSION_COMPLETE) {
      Serial.println("[STATE] Zakończono poprzedni transfer. Reset stanu w odpowiedzi na nowe dane.");
      resetRxState();
    }
    
    if (rx.phase == WAITING_FOR_HEADER) {
      size_t need = sizeof(rx.hdrBuf) - rx.hdrGot;
      size_t take = (len < need) ? len : need;
      memcpy(rx.hdrBuf + rx.hdrGot, p, take);
      rx.hdrGot += take;
      
      if (rx.hdrGot == sizeof(rx.hdrBuf)) {
        if (validateHeader(rx.hdrBuf)) {
          rx.fileSize = (uint32_t(rx.hdrBuf[2]) << 24) | (uint32_t(rx.hdrBuf[3]) << 16) |
                        (uint32_t(rx.hdrBuf[4]) << 8)  | uint32_t(rx.hdrBuf[5]);
          rx.totalPkts = (uint16_t(rx.hdrBuf[6]) << 8) | uint16_t(rx.hdrBuf[7]);
          rx.bytesRemaining = rx.fileSize;
          rx.phase = RECEIVING_DATA;
          
          // Przygotuj bufor na dane obrazu
          imageBuffer.clear();
          imageBuffer.reserve(rx.fileSize);
          Serial.println("[BUFFER] Bufor obrazu wyczyszczony i przygotowany.");
          
          Serial.printf("[RECV] Prawidłowy nagłówek: rozmiar=%lu, pakiety=%u\n", rx.fileSize, rx.totalPkts);
          sendAck(0x01); // Wyślij potwierdzenie odebrania nagłówka
        } else {
          Serial.println("[ERROR] Nieprawidłowy nagłówek - odrzucam i resetuję.");
          resetRxState();
        }
      }
    } else if (rx.phase == RECEIVING_DATA) {
      if (len > rx.bytesRemaining) {
        Serial.printf("[ERROR] Otrzymano za dużo danych! Odrzucam %u bajtów.\n", (unsigned)(len - rx.bytesRemaining));
        len = rx.bytesRemaining;
      }
      
      // Zapisz odebrane bajty do bufora obrazu
      imageBuffer.insert(imageBuffer.end(), p, p + len);

      rx.bytesRemaining -= len;
      rx.receivedPkts++;

      if (rx.receivedPkts % 10 == 0 || rx.bytesRemaining == 0) {
        Serial.printf("[RECV] Pakiet %u, pozostało %lu bajtów\n", rx.receivedPkts, rx.bytesRemaining);
      }

      if (rx.bytesRemaining == 0) {
        Serial.println("[RECV] *** OBRAZ ODEBRANY KOMPLETNIE! ***");
        rx.phase = TRANSMISSION_COMPLETE;
        sendAck(0xAA); // Wyślij finalne potwierdzenie
        
        // Prześlij gotowy obraz do komputera
        sendImageToComputer();
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  Serial.println("Starting BLE Node B...");

  BLEDevice::init("NODE_B");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* svc = server->createService(SERVICE_UUID);

  BLECharacteristic* pRx = svc->createCharacteristic(
    CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pRx->setCallbacks(new RxCallbacks());

  pCharTx = svc->createCharacteristic(CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pCharTx->addDescriptor(new BLE2902());

  svc->start();
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Oczekiwanie na połączenie BLE...");
  resetRxState();
}

void loop() {
  // Sprawdzaj, czy transmisja nie "utknęła" w martwym punkcie
  if (rx.phase != WAITING_FOR_HEADER && millis() - rx.lastActivity > TIMEOUT_MS) {
    Serial.println("[TIMEOUT] Przekroczono limit czasu na aktywność. Resetowanie stanu.");
    resetRxState();
  }
  delay(1000); // Sprawdzaj co sekundę
}