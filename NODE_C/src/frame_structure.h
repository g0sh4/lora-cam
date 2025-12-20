#pragma once
#include <stdint.h>
#include <Arduino.h>

#pragma pack(push, 1)

// Definicje typów ramek (zgodnie z Twoim schematem)
const uint8_t FRAME_TYPE_START  = 1; // 001 (REQ)
const uint8_t FRAME_TYPE_NACK   = 2; // 010 (Prośba o retransmisję)
const uint8_t FRAME_TYPE_ACK    = 3; // 011 (Potwierdzenie / GRANT)
const uint8_t FRAME_TYPE_RE_END = 4; // 100 (Koniec retransmisji)
const uint8_t FRAME_TYPE_DATA   = 5; // 101 (Dane)
const uint8_t FRAME_TYPE_END    = 7; // 111 (Koniec transmisji)

// Typy transmisji w payloadzie START
const uint8_t TX_TYPE_FULL_IMAGE = 0x01;
const uint8_t TX_TYPE_DIFF_RLE   = 0x02;

// Struktura 2-bajtowego nagłówka LoRa (Bitfields)
struct LoRaHeader {
    // ESP32 jest Little-Endian, więc kolejność w bajcie jest od LSB
    uint16_t sequence_num   : 10; // Bity 0-9: Numer sekwencyjny (0-1023)
    uint16_t frame_type     : 3;  // Bity 10-12: Typ ramki
    uint16_t transaction_id : 3;  // Bity 13-15: ID transakcji (0-7)
};

#pragma pack(pop)