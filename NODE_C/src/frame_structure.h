#pragma once
#include <stdint.h>
#include <Arduino.h>

#pragma pack(push, 1)

// --- STAŁE TYPÓW RAMEK (Zgodne z Twoim opisem) ---
const uint8_t FRAME_TYPE_START  = 1; // 001 (REQ)
const uint8_t FRAME_TYPE_NACK   = 2; // 010 (Prośba o retransmisję)
const uint8_t FRAME_TYPE_ACK    = 3; // 011 (Potwierdzenie / GRANT)
const uint8_t FRAME_TYPE_RE_END = 4; // 100 (Koniec retransmisji)
const uint8_t FRAME_TYPE_DATA   = 5; // 101 (Dane)
const uint8_t FRAME_TYPE_END    = 7; // 111 (Koniec transmisji)

// --- TYPY TREŚCI ---
const uint8_t TX_TYPE_FULL_IMAGE = 0x01;
const uint8_t TX_TYPE_DIFF_RLE   = 0x02;
// (Możesz dodać mapowanie dla swojego typu ze Sparse JPEG jeśli jest inny)

// --- STRUKTURA NAGŁÓWKA (Bitfields na uint16_t) ---
// WAŻNE: Na ESP32 (Little Endian) kolejność bitów w uint16 to LSB -> MSB
struct LoRaHeader {
    uint16_t sequence_num   : 10; // Bity 0-9   : Numer sekwencyjny (0-1023)
    uint16_t frame_type     : 3;  // Bity 10-12 : Typ ramki (0-7)
    uint16_t transaction_id : 3;  // Bity 13-15 : ID transakcji (0-7)
};

#pragma pack(pop)