#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>
#include "frame_structure.h"

// --- PINY ---
#define LORA_SCK_PIN 7
#define LORA_MISO_PIN 8
#define LORA_MOSI_PIN 9
#define LORA_NSS_PIN 41
#define LORA_BUSY_PIN 40
#define LORA_DIO1_PIN 39
#define LORA_RST_PIN 42

// --- START NA SF12 ---
#define CONTROL_SF 12
#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0
#define LORA_CODING_RATE 5
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

enum NodeCState { C_IDLE, C_RECEIVING, C_SENDING_RESPONSE };
NodeCState cState = C_IDLE;

uint8_t currentTransactionId = 255;
uint8_t currentSF = CONTROL_SF; 

uint32_t expectedImageSize = 0;
uint16_t expectedPacketCount = 0;
uint16_t receivedPacketCount = 0;
uint16_t imageWidth = 0; uint16_t imageHeight = 0; uint8_t blockSize = 0;

std::vector<uint8_t> imageBuffer;
std::vector<bool> packetsReceived;

// Prototypy
void setLoRaModeRx(uint8_t sf);
void setLoRaModeTx(uint8_t sf);
void handleReceivedPacket(int packetSize);
void sendGrant(uint8_t targetSF);
void sendNackOrAck();
void sendImageToComputer();
void resetReceiverState();

void setup() {
    Serial.begin(115200); delay(2000);
    Serial.println("Starting Node C (Receiver Bitfields + 500ms Delay)...");
    
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);

    int s = radio.begin();
    if (s != RADIOLIB_ERR_NONE) { Serial.printf("Lora Init Error: %d\n", s); while(1); }

    radio.setFrequency(LORA_FREQUENCY);
    radio.setBandwidth(LORA_BANDWIDTH);
    radio.setSpreadingFactor(CONTROL_SF); // SF12
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setOutputPower(14);
    radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
    radio.setCRC(true);
    radio.explicitHeader();
    
    Serial.printf("[LORA] Gotowy. Nasłuch na SF%d (Control)\n", CONTROL_SF);
    radio.startReceive();
}

void loop() {
    // Polling DIO1
    if (digitalRead(LORA_DIO1_PIN) == HIGH) {
        int packetSize = radio.getPacketLength();
        if (packetSize > 0) {
            digitalWrite(LED_BUILTIN, LOW); // Mignięcie
            handleReceivedPacket(packetSize);
            digitalWrite(LED_BUILTIN, HIGH);
        }
    }
}

void handleReceivedPacket(int packetSize) {
    if (packetSize < sizeof(LoRaHeader)) { setLoRaModeRx(currentSF); return; }
    
    uint8_t packetBuffer[256];
    int state = radio.readData(packetBuffer, packetSize);
    if (state != RADIOLIB_ERR_NONE) { 
        Serial.printf("[RX] Blad CRC: %d\n", state);
        setLoRaModeRx(currentSF); 
        return; 
    }

    // Rzutowanie na strukturę bitową
    LoRaHeader* header = (LoRaHeader*)packetBuffer;
    float snr = radio.getSNR();
    float rssi = radio.getRSSI();

    // Logowanie ramek sterujących
    if (header->frame_type != FRAME_TYPE_DATA) {
        Serial.printf("[%lu][RX] ID: %u | Typ: %u | SF: %d | RSSI: %.1f | SNR: %.2f\n", 
                      millis(), header->transaction_id, header->frame_type, currentSF, rssi, snr);
    }

    // --- STAN: IDLE (Nasłuch na SF12) ---
    if (cState == C_IDLE) {
        if (header->frame_type == FRAME_TYPE_START) {
            size_t offset = sizeof(LoRaHeader); 
            // Tuż po 2 bajtach nagłówka mamy metadane
            
            // offset to 2. Metadane: [txType(1)][width(2)][height(2)][block(1)][pkts(2)][size(4)]
            offset++; // skip txType
            imageWidth = *(uint16_t*)(packetBuffer + offset); offset += 2;
            imageHeight = *(uint16_t*)(packetBuffer + offset); offset += 2; offset++; // skip block
            expectedPacketCount = *(uint16_t*)(packetBuffer + offset); offset += 2;
            expectedImageSize = *(uint32_t*)(packetBuffer + offset);

            Serial.printf("[AITP] Nowa sesja. Rozmiar: %u B. Pakiety: %u\n", expectedImageSize, expectedPacketCount);

            // --- ALGORYTM DOBORU SF ---
            uint8_t selectedSF = 12; // Domyślnie bezpieczne

            if (snr > -6.0) selectedSF = 7; 
            else if (snr > -9.0) selectedSF = 8;
            else if (snr > -11.5) selectedSF = 9;
            else if (snr > -14.0) selectedSF = 10;
            else selectedSF = 12;
            
            Serial.printf("[DECISION] Wybrano: SF%d\n", selectedSF);
            
            currentTransactionId = header->transaction_id;
            imageBuffer.clear();
            imageBuffer.resize(expectedImageSize);
            packetsReceived.assign(expectedPacketCount, false);
            receivedPacketCount = 0;

            sendGrant(selectedSF);
            
            cState = C_RECEIVING;
            currentSF = selectedSF;
            setLoRaModeRx(currentSF);
            Serial.printf("[LORA] Przelaczono nasluch na SF%d\n", currentSF);
        }
    } 
    // --- STAN: RECEIVING ---
    else if (cState == C_RECEIVING) {
        if (header->transaction_id != currentTransactionId) { setLoRaModeRx(currentSF); return; }

        if (header->frame_type == FRAME_TYPE_DATA) {
            uint16_t seq = header->sequence_num;
            if (seq < expectedPacketCount && !packetsReceived[seq]) {
                size_t dSize = packetSize - sizeof(LoRaHeader);
                size_t off = (size_t)seq * (255 - sizeof(LoRaHeader));
                if (off + dSize <= imageBuffer.size()) {
                    memcpy(&imageBuffer[off], &packetBuffer[sizeof(LoRaHeader)], dSize);
                    packetsReceived[seq] = true;
                    receivedPacketCount++;
                    if(receivedPacketCount % 5 == 0) Serial.print(".");
                }
            }
            setLoRaModeRx(currentSF);
        }
        else if (header->frame_type == FRAME_TYPE_END || header->frame_type == FRAME_TYPE_RE_END) {
            Serial.println("\n[RX] Otrzymano END. Weryfikacja brakow...");
            sendNackOrAck();
        }
    }
}

void sendGrant(uint8_t targetSF) {
    setLoRaModeTx(CONTROL_SF); // SF12
    delay(500); // 500ms opóźnienia dla NODE_B!
    
    uint8_t buf[sizeof(LoRaHeader) + 1];
    LoRaHeader* h = (LoRaHeader*)buf;
    h->transaction_id = currentTransactionId & 0x07;
    h->frame_type = FRAME_TYPE_ACK & 0x07;
    h->sequence_num = 0;
    
    buf[sizeof(LoRaHeader)] = targetSF; // Payload: Wybrane SF
    
    radio.transmit(buf, sizeof(buf));
    Serial.printf("[%lu][TX] GRANT wyslany na SF12. Payload: SF%d\n", millis(), targetSF);
}

void sendNackOrAck() {
    std::vector<uint16_t> missing;
    for (uint16_t i = 0; i < expectedPacketCount; i++) if (!packetsReceived[i]) missing.push_back(i);
    delay(20);

    setLoRaModeTx(currentSF); 
    
    if (missing.empty()) {
        Serial.println("[TX] ACK FULL. Sukces.");
        uint8_t ack[sizeof(LoRaHeader)];
        LoRaHeader* h = (LoRaHeader*)ack;
        h->transaction_id = currentTransactionId & 0x07;
        h->frame_type = FRAME_TYPE_ACK & 0x07;
        h->sequence_num = 0;
        
        radio.transmit(ack, sizeof(ack));
        
        sendImageToComputer();
        resetReceiverState();
    } else {
        Serial.printf("[TX] NACK: Brakuje %u pakietow.\n", missing.size());
        
        size_t count = min((size_t)missing.size(), (size_t)((255 - sizeof(LoRaHeader)-3)/2));
        size_t pSize = sizeof(LoRaHeader) + 3 + (count * 2);
        uint8_t nack[pSize];
        LoRaHeader* h = (LoRaHeader*)nack;
        h->transaction_id = currentTransactionId & 0x07;
        h->frame_type = FRAME_TYPE_NACK & 0x07;
        h->sequence_num = 0;
        
        size_t off = sizeof(LoRaHeader);
        nack[off++] = currentSF; // Opcjonalnie: prośba o zmianę SF
        *(uint16_t*)(nack + off) = (uint16_t)count; off += 2;
        memcpy(nack + off, missing.data(), count * 2);
        
        radio.transmit(nack, pSize);
        setLoRaModeRx(currentSF);
    }
}

void sendImageToComputer() {
    if (imageBuffer.empty()) return;
    Serial.print("START_IMAGE_TRANSFER\n");
    Serial.printf("%u,%u,%u,%u\n", expectedImageSize, imageWidth, imageHeight, blockSize);
    Serial.write(imageBuffer.data(), imageBuffer.size());
    Serial.print("\n");
}

void setLoRaModeRx(uint8_t sf) { radio.setSpreadingFactor(sf); radio.startReceive(); }
void setLoRaModeTx(uint8_t sf) { radio.setSpreadingFactor(sf); }
void resetReceiverState() {
    cState = C_IDLE;
    imageBuffer.clear();
    packetsReceived.clear();
    currentSF = CONTROL_SF; 
    setLoRaModeRx(currentSF);
    Serial.println("[AITP] Reset stanu. Nasluch na SF12 (Control).");
}