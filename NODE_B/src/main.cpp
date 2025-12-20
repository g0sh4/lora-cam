#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <vector>
#include <SPI.h>
#include <RadioLib.h>

#include "frame_structure.h"
#include "ble_frame_structure.h"

// --- KONFIGURACJA PINÓW ---
#define LORA_SCK_PIN 7
#define LORA_MISO_PIN 8
#define LORA_MOSI_PIN 9
#define LORA_NSS_PIN 41
#define LORA_BUSY_PIN 40
#define LORA_DIO1_PIN 39
#define LORA_RST_PIN 42

// --- PARAMETRY LORA ---
#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0
#define LORA_CODING_RATE 5
#define LORA_TX_POWER 18
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8
#define LORA_PACKET_SIZE 255

// KANAŁ KONTROLNY SF12
#define CONTROL_SF 12 

// --- BLE ---
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

#define ACK_IMG_BUFFERED  0xAA 
#define ACK_LORA_SUCCESS  0xBB
#define ACK_LORA_FAIL     0xFF

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

BLECharacteristic *pCharTx = nullptr;
bool deviceConnected = false;

std::vector<uint8_t> imageBuffer;
struct BleRxState {
  bool receiving = false;
  uint32_t totalSize = 0;
  uint32_t receivedSize = 0;
  uint8_t txType = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint8_t blockSize = 0;
} bleRx;

enum LoRaState {
  IDLE,
  NEGOTIATING,
  TRANSMITTING_BURST,
  WAITING_FOR_ACK,
  RETRANSMITTING,
  FINISHED_SUCCESS,
  FINISHED_ERROR
};
LoRaState loraState = IDLE;

uint8_t currentTransactionId = 0;
uint8_t targetSF = CONTROL_SF; 
unsigned long stateStartTime = 0;
int retryCount = 0;
const int MAX_NEGOTIATION_RETRIES = 5;

volatile bool loraPacketReceived = false;

// Prototypy
void setLoRaConfig(uint8_t sf);
void sendBleStatus(uint8_t status);
void processLoRaStateMachine();
void fillLoRaHeader(uint8_t* buffer, uint8_t type, uint16_t seq);

ICACHE_RAM_ATTR void onLoraIrq() {
  loraPacketReceived = true;
}

// --- BLE ---
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("[BLE] Polaczono."); }
    void onDisconnect(BLEServer* pServer) { deviceConnected = false; BLEDevice::startAdvertising(); Serial.println("[BLE] Rozlaczono."); }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pChar) {
      std::string rxData = pChar->getValue();
      size_t len = rxData.length();
      const uint8_t* data = (const uint8_t*)rxData.data();
      if (len == 0) return;

      if (!bleRx.receiving && len == sizeof(BleHeader)) {
        BleHeader* header = (BleHeader*)data;
        if (header->magic_start[0] == 0xAB && header->magic_start[1] == 0xCD) {
          bleRx.receiving = true;
          bleRx.totalSize = header->data_size;
          bleRx.receivedSize = 0;
          bleRx.txType = header->tx_type;
          bleRx.width = header->width;
          bleRx.height = header->height;
          bleRx.blockSize = header->block_size;
          imageBuffer.clear();
          imageBuffer.reserve(bleRx.totalSize);
          sendBleStatus(0x01);
          Serial.printf("[BLE] Start odbioru. Rozmiar: %u B\n", bleRx.totalSize);
        }
      }
      else if (bleRx.receiving) {
        if (bleRx.receivedSize + len <= bleRx.totalSize) {
          imageBuffer.insert(imageBuffer.end(), data, data + len);
          bleRx.receivedSize += len;
        }
        if (bleRx.receivedSize >= bleRx.totalSize) {
          Serial.println("[BLE] Odbior zakonczony. Przechodze do LoRa.");
          bleRx.receiving = false;
          sendBleStatus(ACK_IMG_BUFFERED);
          loraState = NEGOTIATING;
          retryCount = 0;
          // ZMIANA: ID transakcji tylko 3 bity (0-7)
          currentTransactionId = random(0, 8); 
        }
      }
    }
};

void sendBleStatus(uint8_t status) {
  if (deviceConnected && pCharTx) {
    pCharTx->setValue(&status, 1);
    pCharTx->notify();
  }
}

// --- LORA ---
void setLoRaConfig(uint8_t sf) {
  radio.standby();
  radio.setFrequency(LORA_FREQUENCY);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(LORA_BANDWIDTH);
  radio.setCodingRate(LORA_CODING_RATE);
  radio.setSyncWord(LORA_SYNC_WORD);
  radio.setOutputPower(LORA_TX_POWER);
  radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
  radio.setCRC(true);
  radio.explicitHeader();
}

void fillLoRaHeader(uint8_t* buffer, uint8_t type, uint16_t seq) {
  // Rzutowanie na strukturę bitową - kompilator sam upakuje bity
  LoRaHeader* hdr = (LoRaHeader*)buffer;
  hdr->transaction_id = currentTransactionId & 0x07; // 3 bity
  hdr->frame_type = type & 0x07;                     // 3 bity
  hdr->sequence_num = seq & 0x03FF;                  // 10 bitów
}

// --- STATE MACHINE ---
void processLoRaStateMachine() {
  switch (loraState) {
    case NEGOTIATING: {
      setLoRaConfig(CONTROL_SF);
      Serial.printf("[%lu][LORA] Negocjacja (Próba %d/%d) na SF%d...\n", millis(), retryCount + 1, MAX_NEGOTIATION_RETRIES, CONTROL_SF);
      
      // Bufor na nagłówek (2 bajty) + Metadane (14 bajtów)
      uint8_t reqBuffer[sizeof(LoRaHeader) + 14];
      fillLoRaHeader(reqBuffer, FRAME_TYPE_START, 0);
      
      size_t offset = sizeof(LoRaHeader);
      reqBuffer[offset++] = bleRx.txType;
      *(uint16_t*)(reqBuffer + offset) = bleRx.width; offset += 2;
      *(uint16_t*)(reqBuffer + offset) = bleRx.height; offset += 2;
      reqBuffer[offset++] = bleRx.blockSize;
      
      uint32_t totalData = imageBuffer.size();
      size_t dataChunkSize = LORA_PACKET_SIZE - sizeof(LoRaHeader);
      uint16_t totalPackets = (totalData + dataChunkSize - 1) / dataChunkSize;
      
      *(uint16_t*)(reqBuffer + offset) = totalPackets; offset += 2;
      *(uint32_t*)(reqBuffer + offset) = totalData; offset += 4;
      
      radio.transmit(reqBuffer, offset);
      
      // ZAPOBIEGANIE DUCHOM:
      loraPacketReceived = false; 
      
      radio.startReceive();
      
      unsigned long waitStart = millis();
      bool grantReceived = false;
      
      while (millis() - waitStart < 6000) { 
        if (loraPacketReceived) {
          loraPacketReceived = false;
          int len = radio.getPacketLength();
          uint8_t rxBuf[256];
          int state = radio.readData(rxBuf, len); // Czytamy z CRC check
          
          if (state != RADIOLIB_ERR_NONE) {
              Serial.printf("[%lu][RX] Błąd CRC: %d\n", millis(), state);
              radio.startReceive(); 
              continue;
          }

          LoRaHeader* rxHdr = (LoRaHeader*)rxBuf;
          
          // Debugowanie nagłówka bitowego
          Serial.printf("[%lu][RX] Ramka: Typ=%d, ID=%d, Seq=%d\n", 
                        millis(), rxHdr->frame_type, rxHdr->transaction_id, rxHdr->sequence_num);

          if (rxHdr->transaction_id == currentTransactionId && rxHdr->frame_type == FRAME_TYPE_ACK) {
             if (len >= sizeof(LoRaHeader) + 1) {
                // Payload jest tuż za nagłówkiem (czyli od 3. bajtu)
                targetSF = rxBuf[sizeof(LoRaHeader)];
                if (targetSF < 7 || targetSF > 12) targetSF = CONTROL_SF;
                
                Serial.printf("[%lu][LORA] GRANT! Zmiana na SF%d\n", millis(), targetSF);
                grantReceived = true;
                break;
             }
          }
          radio.startReceive(); 
        }
        yield();
      }
      
      if (grantReceived) {
        loraState = TRANSMITTING_BURST;
      } else {
        retryCount++;
        Serial.printf("[%lu][LORA] Timeout.\n", millis());
        if (retryCount >= MAX_NEGOTIATION_RETRIES) {
          Serial.println("[LORA] Błąd krytyczny: Brak połączenia.");
          loraState = FINISHED_ERROR;
        } else {
          delay(random(200, 1000));
        }
      }
      break;
    }

    case TRANSMITTING_BURST: {
      Serial.printf("[%lu][LORA] Przelaczam na SF%d i wysylam BURST...\n", millis(), targetSF);
      setLoRaConfig(targetSF); 
      delay(100); 
      
      size_t dataChunkSize = LORA_PACKET_SIZE - sizeof(LoRaHeader);
      uint32_t totalData = imageBuffer.size();
      uint16_t totalPackets = (totalData + dataChunkSize - 1) / dataChunkSize;
      uint8_t packetBuf[LORA_PACKET_SIZE];
      
      for (uint16_t i = 0; i < totalPackets; i++) {
        fillLoRaHeader(packetBuf, FRAME_TYPE_DATA, i);
        
        size_t offset = i * dataChunkSize;
        size_t len = min((size_t)dataChunkSize, (size_t)(totalData - offset));
        memcpy(packetBuf + sizeof(LoRaHeader), &imageBuffer[offset], len);
        
        radio.transmit(packetBuf, sizeof(LoRaHeader) + len);
        if (i % 10 == 0) Serial.print(".");
      }
      Serial.println("\n[LORA] Koniec danych. Wysylam END.");
      
      fillLoRaHeader(packetBuf, FRAME_TYPE_END, 0);
      radio.transmit(packetBuf, sizeof(LoRaHeader));
      
      loraState = WAITING_FOR_ACK;
      stateStartTime = millis();
      retryCount = 0;
      radio.startReceive(); 
      break;
    }

    case WAITING_FOR_ACK: {
      if (millis() - stateStartTime > 5000) {
        Serial.println("Timeout ACK. Ponawiam END.");
        retryCount++;
        if (retryCount > 5) loraState = FINISHED_ERROR;
        else {
           uint8_t buf[sizeof(LoRaHeader)];
           fillLoRaHeader(buf, FRAME_TYPE_END, 0);
           radio.transmit(buf, sizeof(LoRaHeader));
           radio.startReceive();
           stateStartTime = millis();
        }
      }
      break;
    }
    case RETRANSMITTING: {
        if (millis() - stateStartTime > 5000) {
             Serial.println("[LORA] Timeout Retransmisji. Ponawiam RE_END...");
             uint8_t buf[sizeof(LoRaHeader)];
             fillLoRaHeader(buf, FRAME_TYPE_RE_END, 0);
             radio.transmit(buf, sizeof(LoRaHeader));
             radio.startReceive();
             stateStartTime = millis();
        }
        break;
    }
    case FINISHED_SUCCESS:
      sendBleStatus(ACK_LORA_SUCCESS);
      loraState = IDLE;
      break;
    case FINISHED_ERROR:
      sendBleStatus(ACK_LORA_FAIL);
      loraState = IDLE;
      break;
    case IDLE: default: break;
  }
}

void handleIncomingLoRa() {
  int len = radio.getPacketLength();
  uint8_t rxBuf[256];
  radio.readData(rxBuf, len);
  LoRaHeader* hdr = (LoRaHeader*)rxBuf;
  
  if (hdr->transaction_id != currentTransactionId) return;
  
  if (loraState == WAITING_FOR_ACK || loraState == RETRANSMITTING) {
      if (hdr->frame_type == FRAME_TYPE_ACK) {
          Serial.printf("[%lu][LORA] ACK_FULL! Sukces.\n", millis());
          loraState = FINISHED_SUCCESS;
      }
      else if (hdr->frame_type == FRAME_TYPE_NACK) {
          if (len >= sizeof(LoRaHeader) + 3) {
              uint8_t reqSF = rxBuf[sizeof(LoRaHeader)];
              uint16_t count = *(uint16_t*)(rxBuf + sizeof(LoRaHeader) + 1);
              uint16_t* list = (uint16_t*)(rxBuf + sizeof(LoRaHeader) + 3);
              
              Serial.printf("[%lu][LORA] NACK. Brakuje: %d. ReqSF: %d\n", millis(), count, reqSF);
              
              if (reqSF != targetSF) { 
                  Serial.printf("[LORA] Zmiana SF na %d\n", reqSF);
                  targetSF = reqSF; setLoRaConfig(targetSF); delay(50); 
              }
              
              uint8_t pBuf[LORA_PACKET_SIZE];
              size_t dSize = LORA_PACKET_SIZE - sizeof(LoRaHeader);
              for(int i=0; i<count; i++) {
                  uint16_t seq = list[i];
                  size_t off = seq * dSize;
                  if (off < imageBuffer.size()) {
                      fillLoRaHeader(pBuf, FRAME_TYPE_DATA, seq);
                      size_t l = min((size_t)dSize, (size_t)(imageBuffer.size() - off));
                      memcpy(pBuf + sizeof(LoRaHeader), &imageBuffer[off], l);
                      radio.transmit(pBuf, sizeof(LoRaHeader) + l);
                  }
              }
              Serial.println("[LORA] Doslano. Wysylam RE_END.");
              fillLoRaHeader(pBuf, FRAME_TYPE_RE_END, 0);
              radio.transmit(pBuf, sizeof(LoRaHeader));
              radio.startReceive();
              loraState = RETRANSMITTING;
              stateStartTime = millis();
          }
      }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting Node B (Bitfields + GhostFix)...");

  BLEDevice::init("NODE_B");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  BLECharacteristic *pCharRx = pService->createCharacteristic(CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pCharRx->setCallbacks(new RxCallbacks());
  pCharTx = pService->createCharacteristic(CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pCharTx->addDescriptor(new BLE2902());
  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("[BLE] Gotowy.");

  pinMode(LORA_RST_PIN, OUTPUT);
  digitalWrite(LORA_RST_PIN, LOW); delay(10);
  digitalWrite(LORA_RST_PIN, HIGH); delay(10);
  SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
  
  radio.begin();
  radio.setDio1Action(onLoraIrq);
  setLoRaConfig(CONTROL_SF); 
  Serial.printf("[LORA] Gotowy. Kanal Kontrolny: SF%d\n", CONTROL_SF);
}

void loop() {
  processLoRaStateMachine();
  if (loraPacketReceived) {
    loraPacketReceived = false;
    handleIncomingLoRa();
  }
  delay(5);
}