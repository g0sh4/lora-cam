#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>
#include "frame_structure.h" // Upewnij się, że masz ten plik w folderze projektu

// --- KONFIGURACJA PINÓW (SX1262) ---
#define LORA_SCK_PIN 7
#define LORA_MISO_PIN 8
#define LORA_MOSI_PIN 9
#define LORA_NSS_PIN 41
#define LORA_BUSY_PIN 40
#define LORA_DIO1_PIN 39
#define LORA_RST_PIN 42

// --- KONFIGURACJA LORA (START NA SF12) ---
#define CONTROL_SF 12
#define LORA_FREQUENCY 868.0
#define LORA_BANDWIDTH 125.0
#define LORA_CODING_RATE 5
#define LORA_SYNC_WORD 0x12
#define LORA_PREAMBLE_LENGTH 8

Module mod(LORA_NSS_PIN, LORA_DIO1_PIN, LORA_RST_PIN, LORA_BUSY_PIN);
SX1262 radio(&mod);

// --- STANY ---
enum NodeCState { C_IDLE, C_RECEIVING };
NodeCState cState = C_IDLE;

// --- ZMIENNE SESJI ---
uint8_t currentTransactionId = 255;
uint8_t currentSF = CONTROL_SF; 

uint32_t expectedImageSize = 0;
uint16_t expectedPacketCount = 0;
uint16_t receivedPacketCount = 0;
uint16_t imageWidth = 0; uint16_t imageHeight = 0; uint8_t blockSize = 0;

std::vector<uint8_t> imageBuffer;
std::vector<bool> packetsReceived;

// --- STRUKTURA DO STATYSTYK ---
struct LinkStats {
    float rssi_min;
    float rssi_max;
    float rssi_sum;
    float snr_min;
    float snr_max;
    float snr_sum;
    uint32_t total_toa;
    uint16_t frames_count;
    
    // Historia retransmisji (ile pakietów brakowało w każdej próbie NACK)
    std::vector<uint16_t> retransmissions; 
    
    void reset() {
        rssi_min = 0; rssi_max = -200; rssi_sum = 0;
        snr_min = 0; snr_max = -200; snr_sum = 0;
        total_toa = 0; frames_count = 0;
        retransmissions.clear();
    }
} stats;

// --- PROTOTYPY ---
void setLoRaModeRx(uint8_t sf);
void setLoRaModeTx(uint8_t sf);
void handleReceivedPacket(int packetSize);
void sendGrant(uint8_t targetSF);
void sendNackOrAck();
void sendImageToComputer();
void resetReceiverState();
void updateStats(float rssi, float snr, uint32_t toa);

void setup() {
    Serial.begin(115200); delay(2000);
    Serial.println("Starting Node C (Receiver + FULL STATS)...");
    
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(LORA_RST_PIN, OUTPUT);
    digitalWrite(LORA_RST_PIN, LOW); delay(10);
    digitalWrite(LORA_RST_PIN, HIGH); delay(10);
    SPI.begin(LORA_SCK_PIN, LORA_MISO_PIN, LORA_MOSI_PIN);

    int s = radio.begin();
    if (s != RADIOLIB_ERR_NONE) { Serial.printf("Lora Init Error: %d\n", s); while(1); }

    radio.setFrequency(LORA_FREQUENCY);
    radio.setBandwidth(LORA_BANDWIDTH);
    radio.setSpreadingFactor(CONTROL_SF); 
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setOutputPower(14);
    radio.setPreambleLength(LORA_PREAMBLE_LENGTH);
    radio.setCRC(true);
    radio.explicitHeader();
    
    stats.reset();

    Serial.printf("[LORA] Gotowy. Nasłuch na SF%d (Control)\n", CONTROL_SF);
    radio.startReceive();
}

void loop() {
    if (digitalRead(LORA_DIO1_PIN) == HIGH) {
        int packetSize = radio.getPacketLength();
        if (packetSize > 0) {
            digitalWrite(LED_BUILTIN, LOW);
            handleReceivedPacket(packetSize);
            digitalWrite(LED_BUILTIN, HIGH);
        }
    }
}

void updateStats(float rssi, float snr, uint32_t toa) {
    if (stats.frames_count == 0) {
        stats.rssi_min = rssi; stats.rssi_max = rssi;
        stats.snr_min = snr; stats.snr_max = snr;
    } else {
        if (rssi < stats.rssi_min) stats.rssi_min = rssi;
        if (rssi > stats.rssi_max) stats.rssi_max = rssi;
        if (snr < stats.snr_min) stats.snr_min = snr;
        if (snr > stats.snr_max) stats.snr_max = snr;
    }
    stats.rssi_sum += rssi;
    stats.snr_sum += snr;
    stats.total_toa += toa;
    stats.frames_count++;
}

void handleReceivedPacket(int packetSize) {
    if (packetSize < sizeof(LoRaHeader)) { setLoRaModeRx(currentSF); return; }
    
    uint8_t packetBuffer[256];
    int state = radio.readData(packetBuffer, packetSize);
    if (state != RADIOLIB_ERR_NONE) { 
        Serial.printf("[RX] Blad CRC: %d\n", state);
        setLoRaModeRx(currentSF); return; 
    }

    LoRaHeader* header = (LoRaHeader*)packetBuffer;
    float snr = radio.getSNR();
    float rssi = radio.getRSSI();
    uint32_t rxToA = radio.getTimeOnAir(packetSize) / 1000; 

    // Logowanie podstawowe
    Serial.printf("[%lu][RX] ID:%u T:%u S:%u SF:%d R:%.1f S:%.2f ToA:%lu\n", 
                  millis(), header->transaction_id, header->frame_type, header->sequence_num, currentSF, rssi, snr, rxToA);

    // Zbieranie statystyk (tylko dla aktywnej transakcji lub nowego STARTu)
    if ((cState == C_RECEIVING && header->transaction_id == currentTransactionId) || 
        (cState == C_IDLE && header->frame_type == FRAME_TYPE_START)) {
        
        // Reset statystyk przy nowym START
        if (cState == C_IDLE && header->frame_type == FRAME_TYPE_START) stats.reset();
        
        updateStats(rssi, snr, rxToA);
    }

    // --- MASZYNA STANÓW ---
    if (cState == C_IDLE) {
        if (header->frame_type == FRAME_TYPE_START) {
            // Parsowanie Payloadu START: Width(2), Height(2), Block(1), PktCnt(2), ImgSize(4)
            size_t offset = sizeof(LoRaHeader) + 1; // +1 pomija txType
            imageWidth = *(uint16_t*)(packetBuffer + offset); offset += 2;
            imageHeight = *(uint16_t*)(packetBuffer + offset); offset += 2; 
            blockSize = *(uint8_t*)(packetBuffer + offset); offset++; // To pole mogło być pominięte we wcześniejszej wersji, upewnij się
            expectedPacketCount = *(uint16_t*)(packetBuffer + offset); offset += 2;
            expectedImageSize = *(uint32_t*)(packetBuffer + offset);

            Serial.printf("[AITP] START. Size:%u Pkts:%u. Deciding SF...\n", expectedImageSize, expectedPacketCount);

            // Adaptacja SF
            uint8_t selectedSF = 12; 
            if (snr > -6.0) selectedSF = 7; 
            else if (snr > -9.0) selectedSF = 8;
            else if (snr > -11.5) selectedSF = 9;
            else if (snr > -14.0) selectedSF = 10;
            else if (snr > -16.0) selectedSF = 11;
            
            currentTransactionId = header->transaction_id;
            imageBuffer.clear();
            imageBuffer.resize(expectedImageSize);
            packetsReceived.assign(expectedPacketCount, false);
            receivedPacketCount = 0;

            sendGrant(selectedSF);
            
            cState = C_RECEIVING;
            currentSF = selectedSF;
            setLoRaModeRx(currentSF);
        }
    } 
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
                }
            }
            setLoRaModeRx(currentSF);
        }
        else if (header->frame_type == FRAME_TYPE_END || header->frame_type == FRAME_TYPE_RE_END) {
            sendNackOrAck();
        }
    }
}

void sendGrant(uint8_t targetSF) {
    setLoRaModeTx(CONTROL_SF); 
    delay(500); 
    uint8_t buf[sizeof(LoRaHeader) + 1];
    LoRaHeader* h = (LoRaHeader*)buf;
    h->transaction_id = currentTransactionId & 0x07;
    h->frame_type = FRAME_TYPE_ACK & 0x07;
    h->sequence_num = 0;
    buf[sizeof(LoRaHeader)] = targetSF; 
    
    radio.transmit(buf, sizeof(buf));
    // Nie przełączamy na Rx tutaj, zrobimy to po powrocie do funkcji wywołującej
}

void sendNackOrAck() {
    std::vector<uint16_t> missing;
    for (uint16_t i = 0; i < expectedPacketCount; i++) if (!packetsReceived[i]) missing.push_back(i);
    delay(20);

    // Rejestracja retransmisji
    if (!missing.empty()) {
        stats.retransmissions.push_back((uint16_t)missing.size());
    }

    setLoRaModeTx(currentSF); 
    
    if (missing.empty()) {
        // ACK
        uint8_t ack[sizeof(LoRaHeader)];
        LoRaHeader* h = (LoRaHeader*)ack;
        h->transaction_id = currentTransactionId & 0x07;
        h->frame_type = FRAME_TYPE_ACK & 0x07;
        h->sequence_num = 0;
        
        radio.transmit(ack, sizeof(ack));
        Serial.println("[TX] ACK FULL. Transfer ukonczony.");
        
        sendImageToComputer();
        resetReceiverState();
    } else {
        // NACK
        Serial.printf("[TX] NACK. Brak %u pkt. Próba #%u\n", missing.size(), stats.retransmissions.size());
        
        size_t count = min((size_t)missing.size(), (size_t)((255 - sizeof(LoRaHeader)-3)/2));
        size_t pSize = sizeof(LoRaHeader) + 3 + (count * 2);
        
        // Dynamiczna alokacja tablicy na stosie (VLA) lub vector
        std::vector<uint8_t> nackBuf(pSize);
        LoRaHeader* h = (LoRaHeader*)nackBuf.data();
        h->transaction_id = currentTransactionId & 0x07;
        h->frame_type = FRAME_TYPE_NACK & 0x07;
        h->sequence_num = 0;
        
        size_t off = sizeof(LoRaHeader);
        nackBuf[off++] = currentSF; 
        *(uint16_t*)(nackBuf.data() + off) = (uint16_t)count; off += 2;
        memcpy(nackBuf.data() + off, missing.data(), count * 2);
        
        radio.transmit(nackBuf.data(), pSize);
        setLoRaModeRx(currentSF);
    }
}

void sendImageToComputer() {
    if (imageBuffer.empty()) return;
    
    // Obliczenia średnich
    float avgRssi = (stats.frames_count > 0) ? (stats.rssi_sum / stats.frames_count) : 0.0;
    float avgSnr = (stats.frames_count > 0) ? (stats.snr_sum / stats.frames_count) : 0.0;
    
    // Budowanie historii retransmisji (CSV z średnikami)
    String nackHistoryStr = "";
    if (stats.retransmissions.empty()) {
        nackHistoryStr = "0";
    } else {
        for (size_t i = 0; i < stats.retransmissions.size(); i++) {
            nackHistoryStr += String(stats.retransmissions[i]);
            if (i < stats.retransmissions.size() - 1) nackHistoryStr += ";";
        }
    }

    // Format: STATS_DATA:minR,maxR,avgR,minS,maxS,avgS,cnt,ToA,nackCnt,nackHist
    Serial.printf("STATS_DATA:%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%u,%lu,%u,%s\n",
        stats.rssi_min, stats.rssi_max, avgRssi,
        stats.snr_min, stats.snr_max, avgSnr,
        stats.frames_count, stats.total_toa,
        stats.retransmissions.size(),
        nackHistoryStr.c_str()
    );

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
    stats.reset(); 
    setLoRaModeRx(currentSF);
    Serial.println("[AITP] Reset. Nasluch SF12.");
}