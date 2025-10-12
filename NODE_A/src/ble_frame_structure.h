#pragma once

#include <stdint.h>

#pragma pack(push, 1)

// Definicje typów transmisji, które Węzeł A wysyła do Węzła B
const uint8_t BLE_TX_TYPE_FULL_IMAGE = 0xF1; // F od Full (Pełny)
const uint8_t BLE_TX_TYPE_DIFF_RLE   = 0xD1; // D od Diff (Różnicowy)

// Finalna, 9-bajtowa struktura nagłówka BLE
struct BleHeader {
    // 2 bajty "magiczne" na początek, aby zweryfikować poprawność ramki
    uint8_t  magic_start[2]; // Wartość: {0xAB, 0xCD}

    // 1 bajt informujący, jakiego rodzaju dane będą przesyłane
    uint8_t  tx_type;

    // 4 bajty określające, jak dużo danych (w bajtach) będzie wysłanych po nagłówku
    uint32_t data_size;

    // 2 bajty "magiczne" na koniec, jako dodatkowa weryfikacja
    uint8_t  magic_end[2];   // Wartość: {0xEF, 0x12}
};

#pragma pack(pop)