#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>

// <--- KLUCZOWA ZMIANA: Używamy Twojego (zaktualizowanego) pliku nagłówkowego
#include "frame_structure.h"

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
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8
#define LORA_PACKET_SIZE 255
#define LORA_DATA_CHUNK_SIZE (LORA_PACKET_SIZE - sizeof(LoRaHeader))

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

// --- Logika maszyny stanów ---
enum NodeCState {
  C_IDLE,
  C_RECEIVING,
  C_SENDING_NACK,
  C_WAITING_FOR_RESEND
};
NodeCState cState = C_IDLE;
uint8_t currentTransactionId = 0; // Będzie 0-7
uint8_t currentSF = LORA_SPREADING_FACTOR;
unsigned long resendTimeout = 0;
const unsigned long RESEND_TIMEOUT_MS = 10000;
// -----------------------------

std::vector<uint8_t> imageBuffer;
std::vector<bool> packetsReceived; // Bufor śledzenia pakietów
uint32_t expectedImageSize = 0;
uint16_t expectedPacketCount = 0;
uint16_t receivedPacketCount = 0;
uint16_t imageWidth = 0;
uint16_t imageHeight = 0;
uint8_t blockSize = 0;

volatile bool loraPacketReceived = false;
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

// Reset stanu
void resetReceiverState() {
    Serial.println("\n[STATE] Resetowanie stanu odbiornika LoRa. Oczekiwanie na nową transmisję...");
    cState = C_IDLE;
    imageBuffer.clear();
    packetsReceived.clear();
    expectedImageSize = 0;
    expectedPacketCount = 0;
    receivedPacketCount = 0;
    imageWidth = 0;
    imageHeight = 0;
    blockSize = 0;
    currentTransactionId = 0;
    currentSF = LORA_SPREADING_FACTOR; 
    setLoRaModeRx(currentSF);
}

// Wysyłanie do PC
void sendImageToComputer() {
    if (imageBuffer.empty() || expectedImageSize == 0) {
        Serial.println("[PC] Błąd: Bufor pusty lub niekompletny, anulowanie wysyłania do PC.");
        return;
    }
    Serial.println("\n[PC] Inicjuję transfer obrazu do komputera...");
    Serial.print("START_IMAGE_TRANSFER\n");
    Serial.print(expectedImageSize);
    Serial.print(",");
    Serial.print(imageWidth);
    Serial.print(",");
    Serial.print(imageHeight);
    Serial.print(",");
    Serial.print(blockSize);
    Serial.print("\n");
    Serial.write(imageBuffer.data(), imageBuffer.size());
    Serial.println("\n[PC] Transfer do komputera zakończony.");
}

// Wysyłanie NACK
// Wysyłanie NACK
void sendNackToNodeB() {
  cState = C_SENDING_NACK;
  std::vector<uint16_t> missingPackets;
  
  for (uint16_t i = 0; i < expectedPacketCount; i++) {
    if (!packetsReceived[i]) {
      missingPackets.push_back(i);
    }
  }

  if (missingPackets.empty()) {
     Serial.println("[LORA] Błąd NACK: Brak brakujących pakietów.");
     resetReceiverState();
     return;
  }

  // <--- OTO POPRAWIONA LOGIKA ---
  // 1. Zapisz SF, na którym FAKTYCZNIE słuchaliśmy (i na którym słucha teraz NODE_B)
  uint8_t sf_do_wyslania_nack = currentSF; 

  // 2. Zdecyduj, na jakim SF będziemy słuchać NASTĘPNYM razem
  if (currentSF < 12) {
    currentSF++; // Zwiększ SF, na którym będziemy SŁUCHAĆ
  }
  // Jeśli już jest 12, to currentSF pozostanie 12 (i na takim będziemy słuchać)
  // --- KONIEC POPRAWIONEJ LOGIKI ---

  Serial.printf("[LORA] Brakuje %u pakietów. Wysyłam NACK z żądaniem SF=%u\n", missingPackets.size(), currentSF);

  // Budowanie pakietu NACK
  size_t nackDataSize = 1 + 2 + (missingPackets.size() * sizeof(uint16_t));
  if (nackDataSize + sizeof(LoRaHeader) > LORA_PACKET_SIZE) {
     Serial.println("[LORA] Błąd NACK: Zbyt wiele brakujących pakietów!");
     resetReceiverState();
     return;
  }
  
  uint8_t nackPacket[sizeof(LoRaHeader) + nackDataSize];
  LoRaHeader* header = (LoRaHeader*)nackPacket;
  header->transaction_id = currentTransactionId;
  header->frame_type = FRAME_TYPE_NACK; // Używa wartości '2' z .h
  header->sequence_num = 0;

  size_t offset = sizeof(LoRaHeader);
  
  // 3. Wpisz do pakietu ten SF, na którym będziemy słuchać
  nackPacket[offset] = currentSF; // Mówimy NODE_B, aby używał tego SF
  offset += 1;
  
  *(uint16_t*)(&nackPacket[offset]) = missingPackets.size();
  offset += 2;
  memcpy(&nackPacket[offset], missingPackets.data(), missingPackets.size() * sizeof(uint16_t));
  
  delay(300); // Daj NODE_B czas na włączenie RX

  // 4. Wysyłamy NACK na tym SF, na którym NODE_B nasłuchuje
  //    (czyli ten, który zapisaliśmy w kroku 1)
  setLoRaModeTx(sf_do_wyslania_nack); // <--- KLUCZOWA POPRAWKA
  
  radio.transmit(nackPacket, sizeof(nackPacket));
  delay(200); // Unikaj self-reception
  
  // 5. Przełączamy się na nasłuch na NOWYM SF (tym z kroku 2 i 3)
  setLoRaModeRx(currentSF); 
  cState = C_WAITING_FOR_RESEND;
  resendTimeout = millis() + RESEND_TIMEOUT_MS;
}

// Wysyłanie ACK
void sendAckToNodeB() {
  Serial.printf("[LORA] Wysyłam ACK (SF=%u)\n", currentSF);
  setLoRaModeTx(currentSF);
  
  uint8_t ackPacket[sizeof(LoRaHeader)];
  LoRaHeader* header = (LoRaHeader*)ackPacket;
  header->transaction_id = currentTransactionId;
  header->frame_type = FRAME_TYPE_ACK; // Używa wartości '3' z .h
  header->sequence_num = 0;
  
  radio.transmit(ackPacket, sizeof(ackPacket));
}


void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting Node C (Image Receiver - v5 ARQ / 3-bit Header)..."); // Zmiana nazwy

    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);

    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) { Serial.print("[LORA] Błąd krytyczny, kod: "); Serial.println(state); while (1); }

    // Ustaw domyślne parametry
    radio.setFrequency(LORA_FREQUENCY);
    radio.setBandwidth(LORA_BANDWIDTH);
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
    radio.setCRC(true);
    radio.explicitHeader();

    radio.setDio1Action(onLoraPacketReceived);
    Serial.println("[LORA] Inicjalizacja pomyślna.");
    
    resetReceiverState();
}

// Główna funkcja obsługi pakietów
void handleReceivedLoraPacket() {
    int packetSize = radio.getPacketLength();
    if (packetSize < sizeof(LoRaHeader)) {
      Serial.println("[LORA] Błąd: Pakiet za krótki.");
      setLoRaModeRx(currentSF); return;
    }
    
    uint8_t packetBuffer[packetSize];
    radio.readData(packetBuffer, packetSize);
    LoRaHeader* header = (LoRaHeader*)packetBuffer;
    
    float rssi = radio.getRSSI();
    float snr = radio.getSNR();
    Serial.printf("[LOG] ID=%u, Typ=%u, Seq=%u, RSSI=%.2f, Size=%d\n",
                  header->transaction_id, header->frame_type, header->sequence_num, rssi, packetSize);

    // Sprawdzanie ID transakcji
    if (header->frame_type != FRAME_TYPE_START && 
        header->transaction_id != currentTransactionId && 
        currentTransactionId != 0) {
      Serial.println("   > Złe ID transakcji. Ignoruję.");
      setLoRaModeRx(currentSF); return;
    }

    switch (cState) {
        case C_IDLE:
            if (header->frame_type == FRAME_TYPE_START) { // Używa wartości '1'
                currentTransactionId = header->transaction_id;
                currentSF = LORA_SPREADING_FACTOR;
                
                size_t offset = sizeof(LoRaHeader);
                uint8_t txType = packetBuffer[offset++];
                imageWidth = *(uint16_t*)(packetBuffer + offset); offset += 2;
                imageHeight = *(uint16_t*)(packetBuffer + offset); offset += 2;
                blockSize = packetBuffer[offset++];
                expectedPacketCount = *(uint16_t*)(packetBuffer + offset); offset += 2;
                expectedImageSize = *(uint32_t*)(packetBuffer + offset);

                imageBuffer.clear();
                imageBuffer.resize(expectedImageSize);
                packetsReceived.assign(expectedPacketCount, false);
                receivedPacketCount = 0;
                cState = C_RECEIVING;
                
                Serial.printf("\n[LORA LOG] Odebrano START. ID: %u, Rozmiar: %u, Pakiety: %u\n", currentTransactionId, expectedImageSize, expectedPacketCount);
            }
            break;

        case C_RECEIVING:
        case C_WAITING_FOR_RESEND: 
            if (header->frame_type == FRAME_TYPE_DATA) { // Używa wartości '5'
                if (header->sequence_num >= expectedPacketCount) {
                    Serial.printf("[LORA] Błąd! Pakiet #%u poza zakresem. Ignoruję.\n", header->sequence_num);
                } else if (!packetsReceived[header->sequence_num]) {
                    size_t offset = (size_t)header->sequence_num * LORA_DATA_CHUNK_SIZE;
                    size_t dataSize = packetSize - sizeof(LoRaHeader);

                    if (offset + dataSize <= imageBuffer.size()) {
                        memcpy(&imageBuffer[offset], &packetBuffer[sizeof(LoRaHeader)], dataSize);
                        packetsReceived[header->sequence_num] = true;
                        receivedPacketCount++;
                        Serial.printf("   > Odebrano pakiet #%u. Łącznie: %u/%u\n", header->sequence_num, receivedPacketCount, expectedPacketCount);
                    } else {
                        Serial.printf("[LORA] Błąd! Pakiet #%u nie mieści się w buforze. Ignoruję.\n", header->sequence_num);
                    }
                } else {
                     Serial.printf("   > Duplikat pakietu #%u. Ignoruję.\n", header->sequence_num);
                }
            
            } else if (header->frame_type == FRAME_TYPE_END || header->frame_type == FRAME_TYPE_RE_END) { // Używa '7' lub '4'
                Serial.printf("[LORA] Odebrano pakiet %s.\n", (header->frame_type == FRAME_TYPE_END) ? "END" : "RE-END");
                
                if (receivedPacketCount == expectedPacketCount) {
                    Serial.println("[LORA] Obraz kompletny! Wysyłam ACK.");
                    sendAckToNodeB();
                    sendImageToComputer(); 
                    resetReceiverState(); 
                } else {
                    Serial.println("[LORA] Obraz wciąż niekompletny. Wysyłam NACK...");
                    sendNackToNodeB(); 
                }
            }
            break;

        case C_SENDING_NACK:
          break; 
    }
    
    if (cState == C_IDLE || cState == C_RECEIVING || cState == C_WAITING_FOR_RESEND) {
      setLoRaModeRx(currentSF);
    }
}


void loop() {
    if (loraPacketReceived) {
        loraPacketReceived = false;
        handleReceivedLoraPacket();
    }

    // Obsługa timeoutów
    if (cState == C_WAITING_FOR_RESEND) {
        if (millis() > resendTimeout) {
            Serial.println("[LORA] Timeout! Nie otrzymano retransmisji. Resetuję stan.");
            resetReceiverState();
        }
    }
}