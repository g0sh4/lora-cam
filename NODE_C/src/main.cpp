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

// Stany odbiornika LoRa
enum LoRaRxState { STATE_WAITING_FOR_START, STATE_RECEIVING_DATA };
LoRaRxState currentState = STATE_WAITING_FOR_START;

// Zmienne do składania obrazu
std::vector<uint8_t> imageBuffer;
uint32_t expectedImageSize = 0;
uint16_t expectedPacketCount = 0;
uint16_t receivedPacketCount = 0;

// Flaga i funkcja przerwania dla niezawodnego odbioru
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
}

void sendImageToComputer() {
    if (imageBuffer.empty()) { Serial.println("[PC] Błąd: Bufor obrazu pusty!"); return; }
    
    // Ta sekcja wysyła dane do skryptu w Pythonie
    Serial.println("\n[PC] Inicjuję transfer obrazu do komputera...");
    Serial.print("START_IMAGE_TRANSFER\n"); // Marker dla Pythona
    Serial.print(imageBuffer.size());      // Rozmiar dla Pythona
    Serial.print("\n");
    Serial.write(imageBuffer.data(), imageBuffer.size());
    
    Serial.println("\n[PC] Transfer do komputera zakończony.");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting Node C (Image Receiver - In progress)...");

    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
    
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) { Serial.print("[LORA] Błąd krytyczny, kod: "); Serial.println(state); while (1); }
    
    // Pełna, jawna konfiguracja radia
    radio.setFrequency(LORA_FREQUENCY);
    radio.setBandwidth(LORA_BANDWIDTH);
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
    radio.setCRC(true); // lub setCrc(), zależnie od wersji biblioteki
    radio.explicitHeader();

    // Ustawienie funkcji przerwania
    radio.setDio1Action(onPacketReceived);
    resetReceiverState();
    
    // Włączenie ciągłego nasłuchu
    Serial.print("[LORA] Uruchamiam ciągły tryb odbioru...");
    state = radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) { Serial.println(" SUKCES! Nasłuchuję..."); }
    else { Serial.print(" BŁĄD, kod: "); Serial.println(state); while (true); }
}

void loop() {
    if (packetReceived) {
        packetReceived = false;

        int packetSize = radio.getPacketLength();
        // Sprawdzamy, czy pakiet ma co najmniej rozmiar nagłówka
        if (packetSize < sizeof(LoRaHeader)) {
            Serial.println("[LORA] Błąd: Odebrano pakiet za krótki, by zawierać nagłówek.");
            radio.startReceive(); // Ponownie włącz nasłuch
            return;
        }

        uint8_t packetBuffer[packetSize];
        radio.readData(packetBuffer, packetSize);
        
        // Rzutujemy początek bufora na strukturę nagłówka, aby łatwo odczytać dane
        LoRaHeader* header = (LoRaHeader*)packetBuffer;

        // Wyświetlamy szczegółowe logi dla każdego pakietu
        float rssi = radio.getRSSI();
        float snr = radio.getSNR();
        Serial.printf("[LOG] ID=%u, Typ=%u, Seq=%u, RSSI=%.2f, SNR=%.2f, Size=%d\n",
                      header->transaction_id, header->frame_type, header->sequence_num, rssi, snr, packetSize);

        switch (currentState) {
            case STATE_WAITING_FOR_START:
                if (header->frame_type == FRAME_TYPE_START) {
                    // Odczytujemy metadane z pola DANE ramki START
                    uint8_t txType = packetBuffer[sizeof(LoRaHeader)];
                    
                    if (txType == TX_TYPE_FULL_IMAGE) {
                        expectedPacketCount = *(uint16_t*)&packetBuffer[sizeof(LoRaHeader) + 1];
                        expectedImageSize = *(uint32_t*)&packetBuffer[sizeof(LoRaHeader) + 3];
                        
                        imageBuffer.clear();
                        imageBuffer.reserve(expectedImageSize);
                        receivedPacketCount = 0;
                        currentState = STATE_RECEIVING_DATA;
                        Serial.printf("\n[LORA] Odebrano START (Pełny Obraz). Oczekiwany rozmiar: %u, pakiety: %u\n", expectedImageSize, expectedPacketCount);
                    }
                    // W przyszłości tutaj dodasz obsługę `else if (txType == TX_TYPE_DIFF_RLE)`
                }
                break;

            case STATE_RECEIVING_DATA:
                if (header->frame_type == FRAME_TYPE_DATA) {
                    // Sprawdzamy, czy numer sekwencyjny się zgadza
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
        
        radio.startReceive(); // Zawsze ponownie włącz nasłuch po przetworzeniu pakietu
    }
}