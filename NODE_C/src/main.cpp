#include <Arduino.h>
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

// Konfiguracja LoRa
#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODING_RATE 5
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

volatile bool packetReceived = false;

ICACHE_RAM_ATTR void onPacketReceived() {
    packetReceived = true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Start Node C - Odbiornik z Ciągłym Nasłuchem (v2 POPRAWIONY)");

    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);
    
    int state = radio.begin();
    if (state != RADIOLIB_ERR_NONE) { while (1); }
    
    radio.setFrequency(LORA_FREQUENCY); 
    radio.setBandwidth(LORA_BANDWIDTH);
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR); 
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
    radio.setCRC(true); // lub setCrc(), zależnie od Twojej wersji biblioteki
    
    radio.setDio1Action(onPacketReceived);

    Serial.print("Włączam ciągły nasłuch... ");
    state = radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("OK! Radio nasłuchuje.");
    } else {
        while (true);
    }
}

void loop() {
    if (packetReceived) {
        packetReceived = false;

        // --- POPRAWIONA LOGIKA ODCZYTU ---
        // 1. Pobierz długość odebranego pakietu
        int packetSize = radio.getPacketLength();
        
        // 2. Przygotuj bufor o idealnym rozmiarze (+1 na znak końca stringa)
        char rx_buffer[packetSize + 1]; 
        
        // 3. Odczytaj DOKŁADNIE tyle bajtów, ile liczył pakiet
        radio.readData((uint8_t*)rx_buffer, packetSize);
        
        // 4. Ręcznie zakończ string, aby bezpiecznie go wydrukować
        rx_buffer[packetSize] = '\0'; 
        // --- KONIEC POPRAWIONEJ LOGIKI ---

        Serial.println("\n--- Odebrano pakiet! ---");
        Serial.print("  > Tresc: '");
        Serial.print(rx_buffer);
        Serial.println("'");
        Serial.printf("  > RSSI: %.2f dBm\n", radio.getRSSI());
        Serial.printf("  > SNR:  %.2f dB\n", radio.getSNR());

        // Po odczycie, ponownie włącz nasłuchiwanie
        radio.startReceive();
    }
}