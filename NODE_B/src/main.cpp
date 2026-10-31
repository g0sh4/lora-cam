#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>
#include "frame_structure.h" // <--- UPEWNIJ SIĘ, ŻE MASZ TEN PLIK

// ==========================================
// === KONFIGURACJA PINÓW ===
// ==========================================
#define TRIGGER_PIN  D5   
#define UART_TX_PIN  43   
#define UART_RX_PIN  44   
#define BAUDRATE     115200

// --- LORA SX1262 ---
#define LORA_SCK_PIN 7
#define LORA_MISO_PIN 8
#define LORA_MOSI_PIN 9
#define LORA_NSS_PIN 41
#define LORA_BUSY_PIN 40
#define LORA_DIO1_PIN 39
#define LORA_RST_PIN 42

// --- PARAMETRY LORA ---
#define LORA_FREQUENCY   868.0
#define LORA_BANDWIDTH   125.0
#define CONTROL_SF       12    // Kanał kontrolny
#define LORA_CODING_RATE 5
#define LORA_TX_POWER    18
#define LORA_SYNC_WORD   0x12
#define LORA_PREAMBLE_LENGTH 8
#define LORA_PACKET_SIZE 255

// Maszyna stanów
enum LoRaState {
  IDLE,
  NEGOTIATING,
  TRANSMITTING_BURST,
  WAITING_FOR_ACK,
  RETRANSMITTING,
  FINISHED_SUCCESS,
  FINISHED_ERROR
};

// ==========================================
// === ZMIENNE GLOBALNE ===
// ==========================================
Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

std::vector<uint8_t> imageBuffer; 
uint8_t receivedTxType = 0;       
uint16_t receivedWidth = 320;     
uint16_t receivedHeight = 240;
uint8_t receivedBlockSize = 16;

// Zmienne stanu LoRa
LoRaState loraState = IDLE;
uint8_t currentTransactionId = 0;
uint8_t targetSF = CONTROL_SF; 
unsigned long stateStartTime = 0;
int retryCount = 0;
const int MAX_NEGOTIATION_RETRIES = 5;
volatile bool loraPacketReceived = false;

// ==========================================
// === PRZERWANIA I POMOCNICZE LORA ===
// ==========================================
ICACHE_RAM_ATTR void onLoraIrq() {
  loraPacketReceived = true;
}

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

// Funkcja wypełniająca nagłówek zgodnie z nową strukturą
void fillLoRaHeader(uint8_t* buffer, uint8_t type, uint16_t seq) {
  // Czyścimy pamięć nagłówka (2 bajty)
  memset(buffer, 0, sizeof(LoRaHeader));
  
  LoRaHeader* hdr = (LoRaHeader*)buffer;
  hdr->transaction_id = currentTransactionId & 0x07;
  hdr->frame_type     = type & 0x07;
  hdr->sequence_num   = seq & 0x03FF;
}

void initLoRa() {
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    
    if (radio.begin() == RADIOLIB_ERR_NONE) {
        radio.setDio1Action(onLoraIrq);
        setLoRaConfig(CONTROL_SF);
        Serial.println("[LORA] Init OK.");
    } else {
        Serial.println("[LORA] Init FAIL!");
    }
}

// ==========================================
// === MASZYNA STANÓW LORA ===
// ==========================================
void processLoRaStateMachine() {
  switch (loraState) {
    case NEGOTIATING: {
      setLoRaConfig(CONTROL_SF);
      Serial.printf("[%lu][LORA] Negocjacja (Próba %d/%d) na SF%d...\n", millis(), retryCount + 1, MAX_NEGOTIATION_RETRIES, CONTROL_SF);
      
      // Budowa pakietu START
      // Header (2B) + Type(1B) + W(2B) + H(2B) + BS(1B) + Pkts(2B) + Size(4B) = 14B payload
      uint8_t reqBuffer[sizeof(LoRaHeader) + 14];
      fillLoRaHeader(reqBuffer, FRAME_TYPE_START, 0);
      
      size_t offset = sizeof(LoRaHeader);
      reqBuffer[offset++] = receivedTxType;
      *(uint16_t*)(reqBuffer + offset) = receivedWidth; offset += 2;
      *(uint16_t*)(reqBuffer + offset) = receivedHeight; offset += 2;
      reqBuffer[offset++] = receivedBlockSize;
      
      uint32_t totalData = imageBuffer.size();
      size_t dataChunkSize = LORA_PACKET_SIZE - sizeof(LoRaHeader);
      uint16_t totalPackets = (totalData + dataChunkSize - 1) / dataChunkSize;
      
      *(uint16_t*)(reqBuffer + offset) = totalPackets; offset += 2;
      *(uint32_t*)(reqBuffer + offset) = totalData; offset += 4;
      
      radio.transmit(reqBuffer, offset);
      
      // Reset nasłuchu
      loraPacketReceived = false; 
      radio.startReceive();
      
      unsigned long waitStart = millis();
      bool grantReceived = false;
      
      // Oczekiwanie na ACK (GRANT) - Typ 3
      while (millis() - waitStart < 6000) { 
        if (loraPacketReceived) {
          loraPacketReceived = false;
          int len = radio.getPacketLength();
          uint8_t rxBuf[256];
          int state = radio.readData(rxBuf, len);
          
          if (state == RADIOLIB_ERR_NONE) {
              LoRaHeader* rxHdr = (LoRaHeader*)rxBuf;
              // Debugowanie ramki przychodzącej
              // Serial.printf("[DEBUG] RX Header: ID=%d, T=%d\n", rxHdr->transaction_id, rxHdr->frame_type);

              if (rxHdr->transaction_id == currentTransactionId && rxHdr->frame_type == FRAME_TYPE_ACK) {
                 if (len >= sizeof(LoRaHeader) + 1) {
                    targetSF = rxBuf[sizeof(LoRaHeader)];
                    Serial.printf("[LORA] GRANT otrzymany! Zmiana na SF%d\n", targetSF);
                    grantReceived = true;
                    break;
                 }
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
      Serial.printf("[LORA] Wysyłam BURST na SF%d...\n", targetSF);
      setLoRaConfig(targetSF); 
      delay(50); 
      
      size_t dataChunkSize = LORA_PACKET_SIZE - sizeof(LoRaHeader);
      uint32_t totalData = imageBuffer.size();
      uint16_t totalPackets = (totalData + dataChunkSize - 1) / dataChunkSize;
      uint8_t packetBuf[LORA_PACKET_SIZE];
      
      for (uint16_t i = 0; i < totalPackets; i++) {
        fillLoRaHeader(packetBuf, FRAME_TYPE_DATA, i); // Typ 5
        size_t offset = i * dataChunkSize;
        size_t len = min((size_t)dataChunkSize, (size_t)(totalData - offset));
        memcpy(packetBuf + sizeof(LoRaHeader), &imageBuffer[offset], len);
        
        radio.transmit(packetBuf, sizeof(LoRaHeader) + len);
      }
      
      Serial.println("[LORA] Koniec danych. Wysyłam END.");
      
      fillLoRaHeader(packetBuf, FRAME_TYPE_END, 0); // Typ 7
      radio.transmit(packetBuf, sizeof(LoRaHeader));
      
      loraState = WAITING_FOR_ACK;
      stateStartTime = millis();
      retryCount = 0;
      radio.startReceive(); 
      break;
    }

    case WAITING_FOR_ACK: {
      if (millis() - stateStartTime > 5000) {
        Serial.println("[LORA] Timeout ACK. Ponawiam END.");
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
             fillLoRaHeader(buf, FRAME_TYPE_RE_END, 0); // Typ 4
             radio.transmit(buf, sizeof(LoRaHeader));
             radio.startReceive();
             stateStartTime = millis();
        }
        break;
    }
    
    case FINISHED_SUCCESS:
      Serial.println("[LORA] >>> SUKCES TRANSMISJI! <<<");
      loraState = IDLE;
      break;
      
    case FINISHED_ERROR:
      Serial.println("[LORA] >>> BŁĄD TRANSMISJI! <<<");
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
          Serial.println("[LORA] ACK_FULL! Transmisja zakończona.");
          loraState = FINISHED_SUCCESS;
      }
      else if (hdr->frame_type == FRAME_TYPE_NACK) {
          Serial.println("[LORA] NACK! Retransmisja...");
          // Logika parsowania listy NACK (uproszczona)
          // ... (Tutaj powinna być pętla po liście brakujących pakietów)
          
          // Po wysłaniu brakujących:
          uint8_t pBuf[sizeof(LoRaHeader)];
          fillLoRaHeader(pBuf, FRAME_TYPE_RE_END, 0);
          radio.transmit(pBuf, sizeof(LoRaHeader));
          radio.startReceive();
          loraState = RETRANSMITTING;
          stateStartTime = millis();
      }
  }
}

// ==========================================
// === UART IMAGE RECEIVER ===
// ==========================================
bool receiveImageOverUart() {
    Serial.println("[UART] Oczekiwanie na nagłówek IMG...");
    unsigned long st = millis();
    bool markerFound = false;
    
    // Szukamy 'IMG'
    while(millis() - st < 15000) { 
        if (Serial1.available() >= 3) {
            if (Serial1.read() == 'I' && Serial1.read() == 'M' && Serial1.read() == 'G') {
                markerFound = true; break;
            }
        }
        delay(1);
    }
    
    if (!markerFound) { Serial.println("[UART] Timeout."); return false; }

    while(Serial1.available() < 5) delay(1);
    receivedTxType = Serial1.read();
    uint32_t dataSize = 0;
    Serial1.readBytes((char*)&dataSize, 4);
    
    Serial.printf("[UART] Typ: %d, Rozmiar: %u B\n", receivedTxType, dataSize);
    if (dataSize == 0) return false;

    imageBuffer.clear();
    imageBuffer.reserve(dataSize);
    
    uint32_t received = 0;
    st = millis();
    while (received < dataSize && millis() - st < 20000) {
        while (Serial1.available()) {
            imageBuffer.push_back(Serial1.read());
            received++;
            if (received == dataSize) break;
        }
    }

    if (received == dataSize) {
        Serial.println("[UART] Odbiór OK.");
        return true;
    } else {
        Serial.println("[UART] Błąd odbioru.");
        return false;
    }
}

// ==========================================
// === SETUP & LOOP ===
// ==========================================
void setup() {
    Serial.begin(115200);
    while(!Serial) delay(10);
    delay(1000);
    Serial.println("\n=== NODE B: MASTER GATEWAY (FIXED PROTOCOL) ===");

    pinMode(TRIGGER_PIN, OUTPUT);
    digitalWrite(TRIGGER_PIN, HIGH); 
    Serial1.begin(BAUDRATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    initLoRa();
}

void loop() {
    Serial.println("\n-------------------------------------------");
    
    // 1. BUDZENIE A
    Serial.println("[MASTER] 1. Budzę NODE_A...");
    digitalWrite(TRIGGER_PIN, LOW); delay(100); digitalWrite(TRIGGER_PIN, HIGH);

    // 2. CZEKANIE
    Serial.println("[MASTER] 2. Czekam 4s...");
    delay(4000); 
    while(Serial1.available()) Serial1.read();

    // 3. KOMENDA 'G'
    Serial.println("[MASTER] 3. Wysyłam 'G'");
    Serial1.write('G');

    // 4. ODBIÓR DANYCH
    bool hasData = receiveImageOverUart();

    // 5. UŚPIENIE A (Od razu)
    Serial.println("[MASTER] 5. Wysyłam 'S' do Slave (niech idzie spać)");
    Serial1.write('S');

    // 6. LORA TRANSMISSION
    if (hasData) {
        Serial.println("[MASTER] 6. Rozpoczynam procedurę LoRa...");
        
        currentTransactionId = (currentTransactionId + 1) % 8;
        loraState = NEGOTIATING;
        retryCount = 0;
        
        while (loraState != IDLE) {
            processLoRaStateMachine();
            if (loraPacketReceived) {
                loraPacketReceived = false;
                handleIncomingLoRa();
            }
        }
    } else {
        Serial.println("[MASTER] Brak danych LoRa.");
    }

    Serial.println("[MASTER] Cykl zakończony. Czekam 30s...");
    delay(30000);
}