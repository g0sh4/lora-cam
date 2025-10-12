#pragma once // import only once
#include <stdint.h>

#pragma pack(push, 1)

const uint8_t FRAME_TYPE_START = 1; // 001
const uint8_t FRAME_TYPE_DATA  = 5; // 101
const uint8_t FRAME_TYPE_END   = 7; // 111

// START FRAME
const uint8_t TX_TYPE_FULL_IMAGE = 0x01;
const uint8_t TX_TYPE_DIFF_RLE   = 0x02;

// Struktura 2-bajtowego nagłówka LoRa
struct LoRaHeader {
    // little-endian structure
    uint16_t sequence_num   : 10; // Bits 0-9: Numer sekwencyjny (0-1023)
    uint16_t frame_type     : 3;  // Bits 10-12: Typ ramki
    uint16_t transaction_id : 3;  // Bits 13-15: ID transakcji (ramdom)
};

#pragma pack(pop)