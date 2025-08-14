#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>
#include <SPI.h>
#include <RadioLib.h>

// Konfiguracja pinów
#define LORA_SCK_PIN   7
#define LORA_MISO_PIN  8
#define LORA_MOSI_PIN  9
#define LORA_NSS_PIN   41
#define LORA_BUSY_PIN  40
#define LORA_DIO1_PIN  39
#define LORA_RST_PIN   42

// Sprawdzona konfiguracja LoRa
#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODING_RATE 5
#define LORA_TX_POWER 14
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8

// Definicje protokołu LoRa
#define PACKET_TYPE_START 0xA1
#define PACKET_TYPE_DATA  0xB1
#define PACKET_TYPE_END   0xC1
#define LORA_PACKET_SIZE 250
#define LORA_DATA_CHUNK_SIZE (LORA_PACKET_SIZE - 3) 

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

// Definicje BLE
#define SERVICE_UUID      "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

enum RxPhase { WAITING_FOR_HEADER, RECEIVING_DATA, TRANSMISSION_COMPLETE };
struct RxState {
  RxPhase  phase = WAITING_FOR_HEADER;
  uint32_t fileSize = 0, bytesRemaining = 0;
  uint16_t totalPkts = 0, receivedPkts = 0;
  uint8_t  hdrBuf[10];
  size_t   hdrGot = 0;
  unsigned long lastActivity = 0;
} rx;

std::vector<uint8_t> imageBuffer;
BLECharacteristic* pCharTx = nullptr;
const uint32_t MAX_FILE_SIZE = 5000000;
const unsigned long TIMEOUT_MS = 65000; 

static void sendAck(uint8_t code) {
  if (pCharTx) { pCharTx->setValue(&code, 1); pCharTx->notify(); }
}

static void resetRxState() {
  Serial.println("[RESET] Resetowanie stanu odbiornika BLE");
  rx.phase = WAITING_FOR_HEADER;
  rx.fileSize = 0; rx.bytesRemaining = 0; rx.totalPkts = 0;
  rx.receivedPkts = 0; rx.hdrGot = 0; rx.lastActivity = millis();
  imageBuffer.clear();
}

void sendImageViaLoRa() {
    if (imageBuffer.empty()) { Serial.println("[LORA] Bufor obrazu pusty, anulowanie transmisji."); return; }
    
    // Uwaga: Upewnij się, że zasilanie Węzła B jest stabilne (np. przez dodanie kondensatora 470uF-1000uF na linii zasilania).
    // Transmisja LoRa powoduje chwilowe, duże skoki poboru prądu.
    
    uint32_t imageSize = imageBuffer.size();
    uint16_t totalPackets = (imageSize + LORA_DATA_CHUNK_SIZE - 1) / LORA_DATA_CHUNK_SIZE;
    Serial.printf("[LORA] Rozpoczynam transmisję obrazu do NODE_C. Rozmiar: %u bajtów, Pakiety: %u\n", imageSize, totalPackets);
    
    uint8_t startPacket[7];
    startPacket[0] = PACKET_TYPE_START;
    startPacket[1] = (imageSize >> 24) & 0xFF; startPacket[2] = (imageSize >> 16) & 0xFF;
    startPacket[3] = (imageSize >> 8) & 0xFF; startPacket[4] = imageSize & 0xFF;
    startPacket[5] = (totalPackets >> 8) & 0xFF; startPacket[6] = totalPackets & 0xFF;
    radio.transmit(startPacket, sizeof(startPacket));
    Serial.println("[LORA] Wysłano pakiet START.");
    delay(50); // Zmniejszone opóźnienie

    for (uint16_t i = 0; i < totalPackets; ++i) {
        uint8_t dataPacket[LORA_PACKET_SIZE];
        dataPacket[0] = PACKET_TYPE_DATA; dataPacket[1] = (i >> 8) & 0xFF; dataPacket[2] = i & 0xFF;
        size_t offset = i * LORA_DATA_CHUNK_SIZE;
        size_t chunkSize = (offset + LORA_DATA_CHUNK_SIZE > imageSize) ? (imageSize - offset) : LORA_DATA_CHUNK_SIZE;
        memcpy(&dataPacket[3], &imageBuffer[offset], chunkSize);
        Serial.printf("[LORA] Wysyłam pakiet danych #%u/%u\n", i + 1, totalPackets);
        radio.transmit(dataPacket, chunkSize + 3);
        delay(50); // Zmniejszone opóźnienie
    }

    uint8_t endPacket[] = {PACKET_TYPE_END};
    radio.transmit(endPacket, sizeof(endPacket));
    Serial.println("[LORA] Wysłano pakiet END. Transmisja zakończona.");
}

static bool validateHeader(const uint8_t* hdr) {
  if (hdr[0] != 0xAB || hdr[1] != 0xCD) return false;
  if (hdr[8] != 0xEF || hdr[9] != 0x12) return false;
  uint32_t size = (uint32_t(hdr[2]) << 24) | (uint32_t(hdr[3]) << 16) | (uint32_t(hdr[4]) << 8) | uint32_t(hdr[5]);
  return !(size == 0 || size > MAX_FILE_SIZE);
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { Serial.println("[BLE] Połączono z klientem (NODE_A)"); resetRxState(); }
  void onDisconnect(BLEServer* pServer) { Serial.println("[BLE] Rozłączono klienta."); BLEDevice::startAdvertising(); Serial.println("[BLE] Oczekiwanie na nowe połączenie..."); }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* chr) {
    std::string data = chr->getValue();
    const uint8_t* p = (const uint8_t*)data.data();
    size_t len = data.length();
    rx.lastActivity = millis();

    if (rx.phase == TRANSMISSION_COMPLETE) { Serial.println("[STATE] Zakończono poprzedni transfer. Reset stanu."); resetRxState(); }
    
    if (rx.phase == WAITING_FOR_HEADER) {
      size_t need = sizeof(rx.hdrBuf) - rx.hdrGot;
      size_t take = (len < need) ? len : need;
      memcpy(rx.hdrBuf + rx.hdrGot, p, take);
      rx.hdrGot += take;
      if (rx.hdrGot == sizeof(rx.hdrBuf)) {
        if (validateHeader(rx.hdrBuf)) {
          rx.fileSize = (uint32_t(rx.hdrBuf[2]) << 24) | (uint32_t(rx.hdrBuf[3]) << 16) | (uint32_t(rx.hdrBuf[4]) << 8) | uint32_t(rx.hdrBuf[5]);
          rx.totalPkts = (uint16_t(rx.hdrBuf[6]) << 8) | uint16_t(rx.hdrBuf[7]);
          rx.bytesRemaining = rx.fileSize;
          rx.phase = RECEIVING_DATA;
          imageBuffer.clear(); imageBuffer.reserve(rx.fileSize);
          Serial.println("[BUFFER] Bufor obrazu przygotowany.");
          Serial.printf("[RECV] Prawidłowy nagłówek BLE: rozmiar=%lu, pakiety=%u\n", rx.fileSize, rx.totalPkts);
          sendAck(0x01);
        } else { Serial.println("[ERROR] Nieprawidłowy nagłówek BLE - odrzucam i resetuję."); resetRxState(); }
      }
    } else if (rx.phase == RECEIVING_DATA) {
      if (len > rx.bytesRemaining) len = rx.bytesRemaining;
      imageBuffer.insert(imageBuffer.end(), p, p + len);
      rx.bytesRemaining -= len;
      rx.receivedPkts++;
      if (rx.bytesRemaining == 0) {
        Serial.println("\n[RECV] *** OBRAZ ODEBRANY KOMPLETNIE PRZEZ BLE! ***\n");
        rx.phase = TRANSMISSION_COMPLETE;
        sendAck(0xAA);
        sendImageViaLoRa();
      }
    }
  }
};

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("Starting Node B (Final Version)...");
  
  BLEDevice::init("NODE_B");
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* svc = server->createService(SERVICE_UUID);
  BLECharacteristic* pRx = svc->createCharacteristic(CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRx->setCallbacks(new RxCallbacks());
  pCharTx = svc->createCharacteristic(CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pCharTx->addDescriptor(new BLE2902());
  svc->start();
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Oczekiwanie na połączenie...");
  
  pinMode(LORA_RST_PIN, OUTPUT);
  digitalWrite(LORA_RST_PIN, LOW); delay(10);
  digitalWrite(LORA_RST_PIN, HIGH); delay(10);
  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) { Serial.print("[LORA] Błąd krytyczny, kod: "); Serial.println(state); while (1); }

  // PEŁNA, JAWNA KONFIGURACJA RADIA
  radio.setFrequency(LORA_FREQUENCY); 
  radio.setBandwidth(LORA_BANDWIDTH);
  radio.setSpreadingFactor(LORA_SPREADING_FACTOR); 
  radio.setCodingRate(LORA_CODING_RATE);
  radio.setSyncWord(LORA_SYNC_WORD);
  radio.setOutputPower(LORA_TX_POWER);
  radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
  radio.setCRC(true); 
  radio.explicitHeader();
  Serial.println("[LORA] Inicjalizacja pomyślna.");
  
  resetRxState();
}

void loop() {
  if (rx.phase != WAITING_FOR_HEADER && millis() - rx.lastActivity > TIMEOUT_MS) {
    Serial.println("[TIMEOUT] Przekroczono limit czasu na aktywność BLE. Resetowanie stanu.");
    resetRxState();
  }
  delay(1000); // Zmniejsza obciążenie procesora, pętla nie musi być tak szybka
}