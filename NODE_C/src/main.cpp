#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>
#include "frame_structure.h"

// piny - NIE ZMIENIAĆ BO INACZEJ NIE DZIAŁA
#define LORA_SCK_PIN   7
#define LORA_MISO_PIN  8
#define LORA_MOSI_PIN  9
#define LORA_NSS_PIN   41
#define LORA_BUSY_PIN  40
#define LORA_DIO1_PIN  39
#define LORA_RST_PIN   42

#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0

#define LORA_SPREADING_FACTOR 7

#define LORA_CODING_RATE 5
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8

// Definicje protokołu LoRa
#define PACKET_TYPE_START 0xA1
#define PACKET_TYPE_DATA  0xB1
#define PACKET_TYPE_END   0xC1
#define LORA_PACKET_SIZE 250

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

enum LoRaRxState { STATE_WAITING_FOR_START, STATE_RECEIVING_DATA };
LoRaRxState currentState = STATE_WAITING_FOR_START;

std::vector<uint8_t> imageBuffer;
uint32_t expectedImageSize = 0;
uint16_t expectedPacketCount = 0;
uint16_t receivedPacketCount = 0;
uint16_t imageWidth = 0;      // Nowe
uint16_t imageHeight = 0;     // Nowe
uint8_t blockSize = 0;        // Nowe

volatile bool packetReceived = false;
ICACHE_RAM_ATTR void onPacketReceived() {
    packetReceived = true;
}

void resetReceiverState() {
    Serial.println("\n[STATE] Resetowanie stanu odbiornika LoRa. Oczekiwanie na nową transmisję...");
    currentState = STATE_WAITING_FOR_START;
    imageBuffer.clear();
    expectedImageSize = 0;
    expectedPacketCount = 0;
    receivedPacketCount = 0;
    imageWidth = 0;
    imageHeight = 0;
    blockSize = 0;
}

void sendImageToComputer() {
if (imageBuffer.empty()) { Serial.println("[PC] Błąd: Bufor obrazu pusty!"); return; }
    
    // Ta sekcja wysyła dane do skryptu w Pythonie (rozszerzona o width, height, block_size)
    Serial.println("\n[PC] Inicjuję transfer obrazu do komputera...");
    Serial.print("START_IMAGE_TRANSFER\n"); // Marker dla Pythona
    Serial.print(imageBuffer.size());      // Rozmiar
    Serial.print(",");                     // Separator
    Serial.print(imageWidth);              // Nowe: width
    Serial.print(",");
    Serial.print(imageHeight);             // Nowe: height
    Serial.print(",");
    Serial.print(blockSize);               // Nowe: block_size
    Serial.print("\n");
    Serial.write(imageBuffer.data(), imageBuffer.size());
    
    Serial.println("\n[PC] Transfer do komputera zakończony.");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting Node C (Image Receiver - In progress v3)...");

    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
    
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) { Serial.print("[LORA] Błąd krytyczny, kod: "); Serial.println(state); while (1); }
    
    radio.setFrequency(LORA_FREQUENCY);
    radio.setBandwidth(LORA_BANDWIDTH);
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
    radio.setCRC(true); 
    radio.explicitHeader();

    radio.setDio1Action(onPacketReceived);
    resetReceiverState();
    
    Serial.print("[LORA] Uruchamiam ciągły tryb odbioru...");
    state = radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) { Serial.println(" SUKCES! Nasłuchuję..."); }
    else { Serial.print(" BŁĄD, kod: "); Serial.println(state); while (true); }
}

void loop() {
    if (packetReceived) {
        packetReceived = false;

        int packetSize = radio.getPacketLength();
        if (packetSize < sizeof(LoRaHeader)) {
            Serial.println("[LORA] Błąd: Odebrano pakiet za krótki, by zawierać nagłówek.");
            radio.startReceive(); // Ponownie włącz nasłuch
            return;
        }

        uint8_t packetBuffer[packetSize];
        radio.readData(packetBuffer, packetSize);
        
        LoRaHeader* header = (LoRaHeader*)packetBuffer;

        float rssi = radio.getRSSI();
        float snr = radio.getSNR();
        Serial.printf("[LOG] ID=%u, Typ=%u, Seq=%u, RSSI=%.2f, SNR=%.2f, Size=%d\n",
                      header->transaction_id, header->frame_type, header->sequence_num, rssi, snr, packetSize);

       switch (currentState) {
            case STATE_WAITING_FOR_START:
                if (header->frame_type == FRAME_TYPE_START) {
                    // Odczytujemy metadane z pola DANE ramki START (rozszerzone)
                    size_t offset = sizeof(LoRaHeader);
                    uint8_t txType = packetBuffer[offset++];
                    if (txType == TX_TYPE_FULL_IMAGE) {
                        imageWidth = *(uint16_t*)(packetBuffer + offset); offset += 2;   // Nowe: width
                        imageHeight = *(uint16_t*)(packetBuffer + offset); offset += 2;  // Nowe: height
                        blockSize = packetBuffer[offset++];                              // Nowe: block_size
                        expectedPacketCount = *(uint16_t*)(packetBuffer + offset); offset += 2;
                        expectedImageSize = *(uint32_t*)(packetBuffer + offset);
                        
                        imageBuffer.clear();
                        imageBuffer.reserve(expectedImageSize);
                        receivedPacketCount = 0;
                        currentState = STATE_RECEIVING_DATA;
                        // Serial.printf("\n[LORA] Odebrano START (Pełny Obraz). Oczekiwany rozmiar: %u, pakiety: %u, Width: %u, Height: %u, Block: %u\n", expectedImageSize, expectedPacketCount, imageWidth, imageHeight, blockSize);
                        Serial.printf("\n[LORA LOG] Odebrano START (Typ: %s). Rozmiar: %u, pakiety: %u, Width: %u, Height: %u, Block: %u\n",
  (txType == TX_TYPE_FULL_IMAGE) ? "FULL" : "DIFF", expectedImageSize, expectedPacketCount, imageWidth, imageHeight, blockSize);
                    }
                }
                break;

            case STATE_RECEIVING_DATA:
                if (header->frame_type == FRAME_TYPE_DATA) {
                    if (header->sequence_num == receivedPacketCount) {
                         size_t dataSize = packetSize - sizeof(LoRaHeader);
                         imageBuffer.insert(imageBuffer.end(), &packetBuffer[sizeof(LoRaHeader)], &packetBuffer[packetSize]);
                         receivedPacketCount++;

                         if (receivedPacketCount % 20 == 0 || receivedPacketCount == expectedPacketCount) {
                            Serial.printf("  > Odebrano pakiet #%u/%u. Postęp: %d%%\n", receivedPacketCount, expectedPacketCount, (int)(100.0 * imageBuffer.size() / expectedImageSize));
                        }
                    } else {
                        Serial.printf("[LORA] Błąd sekwencji! Oczekiwano pakietu #%u, a otrzymano #%u. Pomijam.\n", receivedPacketCount, header->sequence_num);
                    }

                } else if (header->frame_type == FRAME_TYPE_END) {
                    Serial.println("[LORA] Odebrano pakiet END.");
                    
                    if (imageBuffer.size() == expectedImageSize) {
                        Serial.println("[LORA] SUKCES! Obraz odebrany w całości.");
                        Serial.println("STATUS: COMPLETE"); 
                    } else {
                        Serial.printf("[LORA] BŁĄD! Obraz niekompletny. Oczekiwano: %u, otrzymano: %lu\n", expectedImageSize, imageBuffer.size());
                        Serial.println("STATUS: INCOMPLETE"); 
                    }
                    sendImageToComputer();
                    resetReceiverState();
                }
                break;
        }
        
        radio.startReceive();
    }
}