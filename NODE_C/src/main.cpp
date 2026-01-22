#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <vector>
#include <WiFi.h>
#include <HTTPClient.h>
#include "frame_structure.h" // Twój plik z definicją LoRaHeader

// --- KONFIGURACJA SIECI WIFI ---
const char* WIFI_SSID = "NJU_Swiatlowod_EC20";
const char* WIFI_PASS = "J3JQ446ZNTJR";
const char* SERVER_URL = "http://192.168.1.17:5000/upload"; 

// --- KONFIGURACJA PINÓW (SX1262 - XIAO ESP32S3) ---
#define LORA_SCK_PIN 7
#define LORA_MISO_PIN 8
#define LORA_MOSI_PIN 9
#define LORA_NSS_PIN 41
#define LORA_BUSY_PIN 40
#define LORA_DIO1_PIN 39
#define LORA_RST_PIN 42

// --- KONFIGURACJA LORA ---
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
uint8_t currentTxType = 0; // <--- NOWA ZMIENNA: Przechowuje typ (1=FULL, 2=DIFF)

uint32_t expectedImageSize = 0;
uint16_t expectedPacketCount = 0;
uint16_t receivedPacketCount = 0;
uint16_t imageWidth = 0; uint16_t imageHeight = 0; uint8_t blockSize = 0;

std::vector<uint8_t> imageBuffer;
std::vector<bool> packetsReceived;

// --- STRUKTURA DO STATYSTYK ---
struct LinkStats {
    float rssi_min; float rssi_max; float rssi_sum;
    float snr_min; float snr_max; float snr_sum;
    uint32_t total_toa;
    uint16_t frames_count;
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
void sendDataOverWiFi();
void resetReceiverState();
void updateStats(float rssi, float snr, uint32_t toa);

void setup() {
    Serial.begin(115200); 
    
    // 1. Połączenie z Wi-Fi
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("[WIFI] Laczenie");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] Polaczono! IP: %s\n", WiFi.localIP().toString().c_str());

    // 2. Inicjalizacja LoRa
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

    Serial.printf("[LORA] Gotowy. Nasluch na SF%d (Control)\n", CONTROL_SF);
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

    // Zbieranie statystyk
    if ((cState == C_RECEIVING && header->transaction_id == currentTransactionId) || 
        (cState == C_IDLE && header->frame_type == FRAME_TYPE_START)) {
        
        if (cState == C_IDLE && header->frame_type == FRAME_TYPE_START) stats.reset();
        updateStats(rssi, snr, rxToA);
    }

    // --- MASZYNA STANÓW ---
    if (cState == C_IDLE) {
        if (header->frame_type == FRAME_TYPE_START) {
            // --- POPRAWKA: Prawidłowe czytanie Payloadu START ---
            size_t offset = sizeof(LoRaHeader); // Zaczynamy zaraz po nagłówku
            
            // 1. Odczyt Typu Transmisji (1 bajt)
            currentTxType = packetBuffer[offset]; 
            offset++; 

            // 2. Reszta parametrów
            imageWidth = *(uint16_t*)(packetBuffer + offset); offset += 2;
            imageHeight = *(uint16_t*)(packetBuffer + offset); offset += 2; 
            blockSize = *(uint8_t*)(packetBuffer + offset); offset++; 
            expectedPacketCount = *(uint16_t*)(packetBuffer + offset); offset += 2;
            expectedImageSize = *(uint32_t*)(packetBuffer + offset);

            Serial.printf("[START] Type: %s (0x%02X) | Size:%u Pkts:%u\n", 
                (currentTxType == TX_TYPE_FULL_IMAGE ? "FULL" : "DIFF"), 
                currentTxType, expectedImageSize, expectedPacketCount);

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
}

void sendNackOrAck() {
    std::vector<uint16_t> missing;
    for (uint16_t i = 0; i < expectedPacketCount; i++) if (!packetsReceived[i]) missing.push_back(i);
    delay(20);

    if (!missing.empty()) {
        stats.retransmissions.push_back((uint16_t)missing.size());
    }

    setLoRaModeTx(currentSF); 
    
    if (missing.empty()) {
        // --- ACK & SUCCESS ---
        uint8_t ack[sizeof(LoRaHeader)];
        LoRaHeader* h = (LoRaHeader*)ack;
        h->transaction_id = currentTransactionId & 0x07;
        h->frame_type = FRAME_TYPE_ACK & 0x07;
        h->sequence_num = 0;
        
        radio.transmit(ack, sizeof(ack));
        Serial.println("[TX] ACK FULL. Start uploadu po WiFi...");
        
        sendDataOverWiFi();
        
        resetReceiverState();
    } else {
        // --- NACK ---
        Serial.printf("[TX] NACK. Brak %u pkt.\n", missing.size());
        
        size_t count = min((size_t)missing.size(), (size_t)((255 - sizeof(LoRaHeader)-3)/2));
        size_t pSize = sizeof(LoRaHeader) + 3 + (count * 2);
        
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

// --- FUNKCJA WYSYŁAJĄCA DANE DO PYTHON FLASK ---
void sendDataOverWiFi() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Utracono polaczenie! Próba reconnectu...");
        WiFi.reconnect();
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) delay(100);
        if (WiFi.status() != WL_CONNECTED) return;
    }

    HTTPClient http;
    WiFiClient client; 

    // Konstrukcja JSON ze statystykami
    float avgRssi = (stats.frames_count > 0) ? (stats.rssi_sum / stats.frames_count) : 0.0;
    float avgSnr = (stats.frames_count > 0) ? (stats.snr_sum / stats.frames_count) : 0.0;
    
    String retransJson = "[";
    for(size_t i=0; i<stats.retransmissions.size(); i++) {
        retransJson += String(stats.retransmissions[i]);
        if(i < stats.retransmissions.size()-1) retransJson += ",";
    }
    retransJson += "]";

    String jsonStats = "{";
    // --- NOWOŚĆ: Dodajemy img_type do JSON ---
    jsonStats += "\"img_type\":" + String(currentTxType) + ","; 
    jsonStats += "\"rssi_min\":" + String(stats.rssi_min) + ",";
    jsonStats += "\"rssi_max\":" + String(stats.rssi_max) + ",";
    jsonStats += "\"rssi_avg\":" + String(avgRssi) + ",";
    jsonStats += "\"snr_min\":" + String(stats.snr_min) + ",";
    jsonStats += "\"snr_max\":" + String(stats.snr_max) + ",";
    jsonStats += "\"snr_avg\":" + String(avgSnr) + ",";
    jsonStats += "\"toa_ms\":" + String(stats.total_toa) + ",";
    jsonStats += "\"frames_cnt\":" + String(stats.frames_count) + ",";
    jsonStats += "\"retrans_cnt\":" + String(stats.retransmissions.size()) + ",";
    jsonStats += "\"retrans_hist\":" + retransJson + ","; 
    jsonStats += "\"img_width\":" + String(imageWidth) + ",";
    jsonStats += "\"img_height\":" + String(imageHeight) + ",";
    jsonStats += "\"block_size\":" + String(blockSize);
    jsonStats += "}";

    String boundary = "---ESP32Boundary7MA4YWxkTrZu0gW";
    String headStats = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"stats\"\r\n\r\n" + jsonStats + "\r\n";
    String headFile = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"image\"; filename=\"cam.bin\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    String tail = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = headStats.length() + headFile.length() + imageBuffer.size() + tail.length();

    // Parsowanie IP z SERVER_URL (żeby nie wpisywać ręcznie)
    String url = String(SERVER_URL);
    int ipStart = url.indexOf("//") + 2;
    int ipEnd = url.indexOf(":", ipStart);
    String ipStr = url.substring(ipStart, ipEnd);
    int port = 5000; 

    // Manualne wysyłanie po TCP (dla pamięci i streamowania)
    if (client.connect(ipStr.c_str(), port)) { 
        client.println("POST /upload HTTP/1.1");
        client.print("Host: "); client.print(ipStr); client.print(":"); client.println(port);
        client.println("Content-Type: multipart/form-data; boundary=" + boundary);
        client.print("Content-Length: "); client.println(totalLen);
        client.println("Connection: close");
        client.println();
        
        client.print(headStats);
        client.print(headFile);
        client.write(imageBuffer.data(), imageBuffer.size()); // Wysyłamy binarny obraz
        client.print(tail);
        
        // Odbiór odpowiedzi
        unsigned long timeout = millis();
        while(client.connected() && !client.available()) {
            if(millis() - timeout > 5000) break;
            delay(10);
        }
        while(client.available()) { 
            String line = client.readStringUntil('\n'); 
        }
        
        client.stop();
        Serial.println("[HTTP] Upload zakonczony sukcesem.");
    } else {
        Serial.println("[HTTP] Blad polaczenia TCP");
    }
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