#pragma once

#include <stdint.h>

#pragma pack(push, 1)

const uint8_t BLE_TX_TYPE_FULL_IMAGE = 0xF1; // F od Full (Pełny)
const uint8_t BLE_TX_TYPE_DIFF_RLE   = 0xD1; // D od Diff (Różnicowy)
const uint8_t  BLE_TX_TYPE_SPARSE_JPEG = 0xC1 ;

struct BleHeader {
    uint8_t  magic_start[2]; // Wartość: {0xAB, 0xCD}
    uint8_t  tx_type;
    uint32_t data_size;
    uint16_t width;          // Nowe: Szerokość obrazu (np. 640)
    uint16_t height;         // Nowe: Wysokość obrazu (np. 480)
    uint8_t  block_size;     // Nowe: Wielkość bloku (np. 16)
    uint8_t  magic_end[2];   // Wartość: {0xEF, 0x12}
};

#pragma pack(pop)