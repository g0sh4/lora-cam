#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>
#include <SPI.h>
#include <RadioLib.h>

// <--- KLUCZOWA ZMIANA: Używamy Twojego (zaktualizowanego) pliku nagłówkowego
#include "frame_structure.h" 
#include "ble_frame_structure.h"

// piny
#define LORA_SCK_PIN 7
#define LORA_MISO_PIN 8
#define LORA_MOSI_PIN 9
#define LORA_NSS_PIN 41
#define LORA_BUSY_PIN 40
#define LORA_DIO1_PIN 39
#define LORA_RST_PIN 42

// --- Konfiguracja LoRa ---
#define LORA_SPREADING_FACTOR 7
#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0
#define LORA_CODING_RATE 5
#define LORA_TX_POWER 14
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMLE_LENGTH 8
#define LORA_PACKET_SIZE 255

// --- Symulacja utraty ---
#define PACKET_LOSS_PERCENT 1 // Zwiększono do 5% dla testów

// Inicjalizacja modułu LoRa
Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

// Definicje BLE
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// --- Logika maszyny stanów ---
enum NodeBState {
  B_IDLE,
  B_SENDING,
  B_WAITING_FOR_NACK,
  B_RESENDING
};
NodeBState bState = B_IDLE;
uint8_t currentTransactionId = 0; // Teraz będzie to 0-7 z nagłówka
uint16_t currentSF = LORA_SPREADING_FACTOR;
unsigned long ackTimeout = 0;
const unsigned long NACK_TIMEOUT_MS = 10000; 
volatile bool loraPacketReceived = false;
// -----------------------------

// Stan odbioru BLE
enum RxPhase { WAITING_FOR_HEADER, RECEIVING_DATA, TRANSMISSION_COMPLETE };
struct RxState {
  RxPhase phase = WAITING_FOR_HEADER;
  uint32_t fileSize = 0, bytesRemaining = 0;
  uint8_t hdrBuf[sizeof(BleHeader)];
  size_t hdrGot = 0;
  unsigned long lastActivity = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t block_size = 0;
} rx;

// Globalny bufor obrazu
std::vector<uint8_t> imageBuffer;
uint8_t receivedTxType = 0;
BLECharacteristic *pCharTx = nullptr;
const unsigned long TIMEOUT_MS = 65000;
volatile bool imageReadyToSend = false;


// Przerwanie LoRa
ICACHE_RAM_ATTR void onLoraPacketReceived() {
  loraPacketReceived = true;
}

// Funkcja: Przełącz radio w tryb RX
void setLoRaModeRx(uint8_t sf) {
  radio.setSpreadingFactor(sf);
  Serial.printf("[LORA] Przełączam w tryb RX (SF=%u)...\n", sf);
  radio.startReceive();
}

// Funkcja: Przełącz radio w tryb TX
void setLoRaModeTx(uint8_t sf) {
  radio.setSpreadingFactor(sf);
  Serial.printf("[LORA] Przełączam w tryb TX (SF=%u)...\n", sf);
}

// Funkcje pomocnicze BLE
static void sendAck(uint8_t code) {
  if (pCharTx) { pCharTx->setValue(&code, 1); pCharTx->notify(); }
}

static void resetBleRxState() {
  Serial.println("[RESET] Resetowanie stanu odbiornika BLE.");
  rx.phase = WAITING_FOR_HEADER;
  rx.fileSize = 0;
  rx.bytesRemaining = 0;
  rx.hdrGot = 0;
  rx.lastActivity = millis();
  rx.width = 0;
  rx.height = 0;
  rx.block_size = 0;
}

// Reset stanu LoRa
void resetLoRaState() {
  Serial.println("[LORA] Transakcja zakończona. Resetowanie stanu LoRa.");
  bState = B_IDLE;
  imageBuffer.clear(); 
  receivedTxType = 0;
  currentSF = LORA_SPREADING_FACTOR;
  setLoRaModeRx(currentSF); 
  resetBleRxState();
}


// Funkcja wysyłania LoRa
void sendImageViaLoRa(uint8_t txType) {
  if (imageBuffer.empty()) {
    Serial.println("[LORA] Bufor obrazu pusty, anulowanie transmisji.");
    return;
  }
  
  bState = B_SENDING;
  currentSF = LORA_SPREADING_FACTOR; 
  setLoRaModeTx(currentSF);

  const int guardTimeMs = 100;
  uint32_t dataSize = imageBuffer.size();
  const size_t LORA_DATA_CHUNK_SIZE = LORA_PACKET_SIZE - sizeof(LoRaHeader);
  uint16_t totalDataPackets = (dataSize + LORA_DATA_CHUNK_SIZE - 1) / LORA_DATA_CHUNK_SIZE;

  Serial.printf("[LORA] Rozpoczynam transmisję (SF=%u). Rozmiar: %u B, Pakiety: %u\n", currentSF, dataSize, totalDataPackets);
  currentTransactionId = random(8); // <--- ZMIANA: ID transakcji musi być 0-7
  Serial.printf("[LORA] ID tej transakcji: %u\n", currentTransactionId);

  // --- 1. WYSYŁANIE RAMKI START ---
  uint8_t startPacket[sizeof(LoRaHeader) + 1 + 2 + 2 + 1 + 2 + 4]; 
  LoRaHeader *startHeader = (LoRaHeader *)startPacket;
  startHeader->transaction_id = currentTransactionId;
  startHeader->frame_type = FRAME_TYPE_START; // Używa wartości '1' z .h
  startHeader->sequence_num = 0;
  size_t offset = sizeof(LoRaHeader);
  startPacket[offset++] = (txType == BLE_TX_TYPE_FULL_IMAGE) ? TX_TYPE_FULL_IMAGE : TX_TYPE_DIFF_RLE;
  *(uint16_t *)(startPacket + offset) = rx.width; offset += 2;
  *(uint16_t *)(startPacket + offset) = rx.height; offset += 2;
  startPacket[offset++] = rx.block_size;
  *(uint16_t *)(startPacket + offset) = totalDataPackets; offset += 2;
  *(uint32_t *)(startPacket + offset) = dataSize;
  
  Serial.printf("[LORA] Wysyłam pakiet START...\n");
  radio.transmit(startPacket, sizeof(startPacket));
  delay(radio.getTimeOnAir(sizeof(startPacket)) / 1000 + guardTimeMs);

  // --- 2. WYSYŁANIE RAMEK Z DANYMI ---
  static uint8_t dataPacket[LORA_PACKET_SIZE];
  LoRaHeader *dataHeader = (LoRaHeader *)dataPacket;
  for (uint16_t i = 0; i < totalDataPackets; ++i) {
    dataHeader->transaction_id = currentTransactionId;
    dataHeader->frame_type = FRAME_TYPE_DATA; // Używa wartości '5' z .h
    dataHeader->sequence_num = i;
    size_t offset = i * LORA_DATA_CHUNK_SIZE;
    size_t chunkSize = (offset + LORA_DATA_CHUNK_SIZE > dataSize) ? (dataSize - offset) : LORA_DATA_CHUNK_SIZE;

    #if PACKET_LOSS_PERCENT > 0
    if (random(100) < PACKET_LOSS_PERCENT) {
      Serial.printf("   > [SYMULACJA] Opuszczam pakiet danych #%u/%u!\n", i, totalDataPackets - 1);
      continue;
    }
    #endif

    memcpy(&dataPacket[sizeof(LoRaHeader)], &imageBuffer[offset], chunkSize);
    size_t packetLength = sizeof(LoRaHeader) + chunkSize;
    Serial.printf("   > Wysyłam pakiet danych #%u/%u...\n", i, totalDataPackets - 1);
    radio.transmit(dataPacket, packetLength);
    delay(50);
  }

  // --- 3. WYSYŁANIE RAMKI KONIEC ---
  uint8_t endPacket[sizeof(LoRaHeader)];
  LoRaHeader *endHeader = (LoRaHeader *)endPacket;
  endHeader->transaction_id = currentTransactionId;
  endHeader->frame_type = FRAME_TYPE_END; // Używa wartości '7' z .h
  endHeader->sequence_num = 0;
  Serial.printf("[LORA] Wysyłam pakiet END. Przechodzę w tryb nasłuchu NACK...\n");
  radio.transmit(endPacket, sizeof(endPacket));
  // delay(200);
  // --- 4. PRZEJŚCIE W TRYB OCZEKIWANIA NA NACK ---
  setLoRaModeRx(currentSF); 
  ackTimeout = millis() + NACK_TIMEOUT_MS;
  bState = B_WAITING_FOR_NACK;
}

// Wysyłanie brakujących pakietów
void sendMissingPackets(uint8_t* nackPacket, int packetSize) {
  bState = B_RESENDING;
  
  size_t nackHeaderSize = sizeof(LoRaHeader);
  uint8_t newSF = nackPacket[nackHeaderSize + 0];
  uint16_t numMissing = *(uint16_t*)(&nackPacket[nackHeaderSize + 1]);
  uint16_t* missingList = (uint16_t*)(&nackPacket[nackHeaderSize + 3]);
  
  Serial.printf("[LORA] Otrzymano NACK. Żądanie zmiany na SF=%u.\n", newSF);
  Serial.printf("[LORA] Brakuje %u pakietów. Rozpoczynam retransmisję...\n", numMissing);
  
  currentSF = newSF; 
  setLoRaModeTx(currentSF);
  delay(100);
  static uint8_t dataPacket[LORA_PACKET_SIZE];
  LoRaHeader *dataHeader = (LoRaHeader *)dataPacket;
  const size_t LORA_DATA_CHUNK_SIZE = LORA_PACKET_SIZE - sizeof(LoRaHeader);
  uint32_t dataSize = imageBuffer.size();

  for (uint16_t i = 0; i < numMissing; i++) {
    uint16_t packetNum = missingList[i];
    
    dataHeader->transaction_id = currentTransactionId;
    dataHeader->frame_type = FRAME_TYPE_DATA; // Używa wartości '5' z .h
    dataHeader->sequence_num = packetNum;
    
    size_t offset = (size_t)packetNum * LORA_DATA_CHUNK_SIZE;
    size_t chunkSize = (offset + LORA_DATA_CHUNK_SIZE > dataSize) ? (dataSize - offset) : LORA_DATA_CHUNK_SIZE;

    if (offset + chunkSize > dataSize) {
        Serial.printf("   > [BŁĄD] Żądany pakiet #%u jest poza zakresem!\n", packetNum);
        continue;
    }

    memcpy(&dataPacket[sizeof(LoRaHeader)], &imageBuffer[offset], chunkSize);
    size_t packetLength = sizeof(LoRaHeader) + chunkSize;
    
    Serial.printf("   > Wysyłam ponownie pakiet #%u...\n", packetNum);
    radio.transmit(dataPacket, packetLength);
    delay(50);
  }

  // --- WYSYŁANIE RAMKI RE-END ---
  uint8_t endPacket[sizeof(LoRaHeader)];
  LoRaHeader *endHeader = (LoRaHeader *)endPacket;
  endHeader->transaction_id = currentTransactionId;
  endHeader->frame_type = FRAME_TYPE_RE_END; // Używa wartości '4' z .h
  endHeader->sequence_num = 0;
  
  Serial.printf("[LORA] Retransmisja zakończona. Wysyłam RE-END.\n");
  radio.transmit(endPacket, sizeof(endPacket));
  
  setLoRaModeRx(currentSF);
  ackTimeout = millis() + NACK_TIMEOUT_MS;
  bState = B_WAITING_FOR_NACK;
}

// Obsługa odebranych pakietów ACK/NACK
void handleLoraPacket() {
  int packetSize = radio.getPacketLength();
  uint8_t packetBuffer[packetSize];
  radio.readData(packetBuffer, packetSize);
  LoRaHeader* header = (LoRaHeader*)packetBuffer;
  
  float rssi = radio.getRSSI();
  float snr = radio.getSNR();
  Serial.printf("[LORA RX] Odebrano pakiet, ID=%u, Typ=%u, RSSI=%.2f\n", header->transaction_id, header->frame_type, rssi);

  if (header->transaction_id != currentTransactionId) {
    Serial.println("   > Złe ID transakcji. Ignoruję.");
    setLoRaModeRx(currentSF);
    return;
  }

  if (bState == B_WAITING_FOR_NACK) {
    if (header->frame_type == FRAME_TYPE_NACK) { // Używa wartości '2' z .h
      sendMissingPackets(packetBuffer, packetSize);
    } else if (header->frame_type == FRAME_TYPE_ACK) { // Używa wartości '3' z .h
      Serial.println("[LORA] Otrzymano ACK. Transmisja pomyślna!");
      resetLoRaState();
    }
  } else {
    Serial.println("   > Odebrano pakiet LoRa w złym stanie. Ignoruję.");
    setLoRaModeRx(currentSF);
  }
}

// Callbacks BLE
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) { Serial.println("[BLE] Połączono z klientem (NODE_A)"); resetBleRxState(); }
  void onDisconnect(BLEServer *pServer) { Serial.println("[BLE] Rozłączono klienta."); BLEDevice::startAdvertising(); Serial.println("[BLE] Oczekiwanie na nowe połączenie..."); }
};

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *chr) {
    std::string data = chr->getValue();
    const uint8_t *p = (const uint8_t *)data.data();
    size_t len = data.length();
    rx.lastActivity = millis();

    if (rx.phase == TRANSMISSION_COMPLETE) { resetBleRxState(); }

    if (rx.phase == WAITING_FOR_HEADER) {
      size_t need = sizeof(BleHeader) - rx.hdrGot;
      size_t take = (len < need) ? len : need;
      memcpy(rx.hdrBuf + rx.hdrGot, p, take);
      rx.hdrGot += take;

      if (rx.hdrGot == sizeof(BleHeader)) {
        BleHeader *header = (BleHeader *)rx.hdrBuf;
        if (header->magic_start[0] == 0xAB && header->magic_start[1] == 0xCD && header->magic_end[0] == 0xEF && header->magic_end[1] == 0x12) {
          rx.fileSize = header->data_size;
          rx.bytesRemaining = header->data_size;
          receivedTxType = header->tx_type;
          rx.width = header->width;
          rx.height = header->height;
          rx.block_size = header->block_size;
          rx.phase = RECEIVING_DATA;
          
          imageBuffer.clear();
          imageBuffer.reserve(rx.fileSize);
          
          Serial.printf("[BLE RECV] Prawidłowy nagłówek. Typ: 0x%X, Rozmiar: %lu\n", receivedTxType, rx.fileSize);
          sendAck(0x01);
        } else {
          Serial.println("[BLE ERROR] Nieprawidłowy nagłówek BLE.");
          resetBleRxState();
        }
      }
    } else if (rx.phase == RECEIVING_DATA) {
      if (len > rx.bytesRemaining) len = rx.bytesRemaining;
      imageBuffer.insert(imageBuffer.end(), p, p + len);
      rx.bytesRemaining -= len;
      if (rx.bytesRemaining == 0) {
        Serial.println("\n[RECV] *** DANE ODEBRANE KOMPLETNIE PRZEZ BLE! ***\n");
        rx.phase = TRANSMISSION_COMPLETE;
        sendAck(0xAA);
        imageReadyToSend = true;
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting Node B (v5 ARQ / 3-bit Header)..."); // Zmiana nazwy

  BLEDevice::init("NODE_B");
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *svc = server->createService(SERVICE_UUID);
  BLECharacteristic *pRx = svc->createCharacteristic(CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
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
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[LORA] Błąd krytyczny, kod: "); Serial.println(state); while (1);
  }

  // Ustaw domyślne parametry
  radio.setFrequency(LORA_FREQUENCY);
  radio.setBandwidth(LORA_BANDWIDTH);
  radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
  radio.setCodingRate(LORA_CODING_RATE);
  radio.setSyncWord(LORA_SYNC_WORD);
  radio.setOutputPower(LORA_TX_POWER);
  radio.setPreambleLength(LORA_PREAMLE_LENGTH);
  radio.setCRC(true);
  radio.explicitHeader();
  
  radio.setDio1Action(onLoraPacketReceived);
  
  Serial.println("[LORA] Inicjalizacja pomyślna.");

  resetBleRxState();
  resetLoRaState();
}

// Główna pętla: Maszyna Stanów
void loop() {
  // 1. Obsługa zdarzeń BLE
  if (rx.phase != WAITING_FOR_HEADER && millis() - rx.lastActivity > TIMEOUT_MS) {
    Serial.println("[TIMEOUT] Przekroczono limit czasu na aktywność BLE. Resetowanie stanu.");
    resetBleRxState();
  }

  // 2. Obsługa odebranych pakietów LoRa
  if (loraPacketReceived) {
    loraPacketReceived = false; 
    if (bState == B_WAITING_FOR_NACK) {
      handleLoraPacket();
    } else {
       Serial.println("[LORA] Odebrano pakiet, ale nie w stanie oczekiwania. Ignoruję.");
       setLoRaModeRx(currentSF);
    }
  }

  // 3. Główna maszyna stanów
  switch (bState) {
    case B_IDLE:
      if (imageReadyToSend) {
        imageReadyToSend = false;
        sendImageViaLoRa(receivedTxType);
      }
      break;

    case B_SENDING:
      break;

    case B_WAITING_FOR_NACK:
      if (millis() > ackTimeout) {
        Serial.println("[LORA] Timeout NACK/ACK. Zakładam błąd i resetuję.");
        resetLoRaState();
      }
      break;
    
    case B_RESENDING:
      break;
  }

  delay(10);
}