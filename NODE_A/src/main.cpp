#define CAMERA_MODEL_XIAO_ESP32S3  // <-- WYMAGANE przed "camera_pins.h"
#include "camera_pins.h"

#include <esp_camera.h>
#include <SD.h>
#include <SPI.h>
// #include <JPEGENC.h> // <-- Używamy JPEGDEC
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <JPEGENC.h>
#include <JPEGDEC.h>
#include "ble_frame_structure.h" // Twój plik nagłówkowy

constexpr uint8_t SD_CS = 21;
constexpr uint8_t SD_SCK = 7;
constexpr uint8_t SD_MISO = 8;
constexpr uint8_t SD_MOSI = 9;
SPIClass sdSPI(FSPI);

JPEGDEC jpegDec;  // Globalny obiekt dekodera
JPEGENC jpegEnc;  // Globalny obiekt enkodera

const int buttonPin = 0;

#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

const char* NODE_B_NAME = "NODE_B";
const size_t PAYLOAD_SIZE = 180;

BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pCharRx = nullptr;
BLERemoteCharacteristic* pCharTx = nullptr;

volatile uint8_t ackValue = 0x00;
unsigned long lastConnectAttempt = 0;
const unsigned long CONNECT_RETRY_INTERVAL = 3000;

int buttonState = HIGH;
int lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
enum DiffMethod {
  METHOD_MAD,             // Mean Absolute Difference (Twoja obecna metoda)
  METHOD_COSINE_DISTANCE, // Algorytm z Sekcji 2.6.2 (Cosine Similarity)
  METHOD_MSE,            // Mean Squared Error (MSE)
  METHOD_DSSIM
};
bool first_press = true;  // Stan: Pierwsze kliknięcie - full, kolejne - diff

// === ZMIENIONO ===
// Dostosowano do formatu QVGA (320x240) zgodnie z nazwami plików.
uint16_t img_width = 320;  // QVGA
uint16_t img_height = 240;
// =================
DiffMethod currentDiffMethod = METHOD_DSSIM;
float mad_threshold = 5.0;  // Próg MAD dla zmiany bloku
float cosine_dist_threshold = 0.005; // Próg odległości cosinusowej dla zmiany bloku
float mse_threshold = 180.0; // Próg MSE dla zmiany bloku
float dssim_threshold = 0.05; // Próg DSSIM dla zmiany bloku

uint8_t block_size = 16; // Rozmiar bloku 16x16 pikseli


uint8_t jpeg_encode_quality = 80; // Jakość dla Sparse JPEG (0-100)
uint8_t sparse_background_color = 0x00; // ZMIENIONO: Użyj 0x00 (czerń) jako tła, aby pasowało do skryptu Pythona

// Globalny bufor grayscale dla callbacka
static uint8_t* global_gray_buf = nullptr;
static uint16_t global_img_width = 0;
static uint8_t* prev_gray = nullptr; // Bufor na poprzedni obraz
static std::vector<uint8_t> encodedJpegData;

// === DODANO: Zmienne do zarządzania sekwencją plików ===
static int current_frame_index = 1; // Zaczyna od pliku _0001.jpg
const int max_frame_index = 6;      // Ostatni plik w sekwencji to _0006.jpg
const char* base_filename = "/frame_qvga_14719992_"; // Bazowa część nazwy pliku (zaczynamy od / dla katalogu głównego SD)
const char* file_extension = ".jpg";
static char current_filename_buffer[50]; // Globalny bufor na pełną nazwę pliku
// =======================================================


// ===============================================================
// === FUNKCJE POMOCNICZE (CALLBACKI) DLA BIBLIOTEKI JPEGDEC ===
// ===============================================================

static int32_t jpegReadCallback(JPEGFILE *pHandle, uint8_t *pBuffer, int32_t iLength) {
  File *f = static_cast<File *>(pHandle->fHandle);
  return f->read(pBuffer, iLength);
}

static int32_t jpegSeekCallback(JPEGFILE *pHandle, int32_t iPosition) {
  File *f = static_cast<File *>(pHandle->fHandle);
  if (f->seek(iPosition)) {
    return 0; // Sukces
  }
  Serial.printf("[JPEGDEC SEEK ERROR] Nie udało się przewinąć do pozycji: %d\n", iPosition);
  return -1; // Błąd
}

static void jpegCloseCallback(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  f->close();
}

static int jpegDrawCallback(JPEGDRAW *pDraw) {
  // === ZMIENIONO: Cała funkcja ===
  // Poprzednio: konwertowaliśmy z RGB565.
  // Teraz: Otrzymujemy bezpośrednio 8-bitową skalę szarości.
  uint8_t *pPixels = (uint8_t *)pDraw->pPixels;
  int x = pDraw->x, y = pDraw->y, w = pDraw->iWidth, h = pDraw->iHeight;

  // Szybsze kopiowanie, linia po linii
  for (int py = 0; py < h; py++) {
    int_fast32_t src_offset = py * w;
    int_fast32_t dst_offset = (y + py) * global_img_width + x;
    memcpy(global_gray_buf + dst_offset, pPixels + src_offset, w);
  }
  return 1; // Kontynuuj dekodowanie
}
void * myJpegEncOpen(const char *filename) {
    encodedJpegData.clear();
    encodedJpegData.reserve(img_width * img_height / 10);
    return (void *)&encodedJpegData;
}
void myJpegEncClose(JPEGE_FILE *p) { /* Nic */ }
int32_t myJpegEncWrite(JPEGE_FILE *p, uint8_t *buffer, int32_t length) {
    std::vector<uint8_t> *v = static_cast<std::vector<uint8_t> *>(p->fHandle);
    try { v->insert(v->end(), buffer, buffer + length); }
    catch (const std::bad_alloc& e) { Serial.println("[JPEGENC WRITE CB ERROR] Brak pamięci w wektorze!"); return 0; }
    return length;
}
int32_t myJpegEncRead(JPEGE_FILE *p, uint8_t *buffer, int32_t length) { return 0; } // Nieużywane
int32_t myJpegEncSeek(JPEGE_FILE *p, int32_t position) { return 0; } // Nieużywane

// ===============================================================
// === FUNKCJE APLIKACJI ===
// ===============================================================

void notifyCB(BLERemoteCharacteristic* rc, uint8_t* data, size_t len, bool isNotify) {
  if (len > 0) { ackValue = data[0]; }
}

void initCamera() {
  camera_config_t cfg = {
    .pin_pwdn = PWDN_GPIO_NUM, .pin_reset = RESET_GPIO_NUM, .pin_xclk = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM, .pin_sccb_scl = SIOC_GPIO_NUM, .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM, .pin_d5 = Y7_GPIO_NUM, .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM, .pin_d2 = Y4_GPIO_NUM, .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM, .pin_vsync = VSYNC_GPIO_NUM, .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM, .xclk_freq_hz = 20000000, .pixel_format = PIXFORMAT_JPEG,
    
    // === ZMIENIONO ===
    // Ustawiono na QVGA, aby pasowało do rozmiaru przetwarzanych obrazów
    .frame_size = FRAMESIZE_QVGA, // 320x240
    // =================
    
    .jpeg_quality = 20, .fb_count = 1
  };
  if (esp_camera_init(&cfg) != ESP_OK) { Serial.println("[ERROR] Camera init failed"); while (true) delay(1000); } 
  else { Serial.println("[OK] Camera initialized successfully"); }
}

static void sdInit() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 25000000)) { Serial.println("[ERROR] Inicjalizacja karty SD nie powiodła się!"); while (true) delay(1000); }
  if (SD.cardType() == CARD_NONE) { Serial.println("[ERROR] Nie wykryto karty SD"); while (true) delay(1000); }
  Serial.println("[OK] Karta SD zainicjalizowana pomyślnie.");

  Serial.println("[SD DEBUG] Lista plików w /:");
  File root = SD.open("/");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    Serial.print("- ");
    Serial.println(entry.name());
    entry.close();
  }
  root.close();
}

void initBLE() {
  BLEDevice::init("");
  pClient = BLEDevice::createClient();
}

void resetBLEClient() {
  if (pClient && pClient->isConnected()) { pClient->disconnect(); delay(200); }
}

bool connectToNodeB() {
  BLEScan* scan = BLEDevice::getScan();
  scan->setActiveScan(true); scan->setInterval(100); scan->setWindow(99);
  BLEScanResults results = scan->start(3, false);
  for (int i = 0; i < results.getCount(); ++i) {
    BLEAdvertisedDevice adv = results.getDevice(i);
    if (adv.getName() == NODE_B_NAME) {
      if (!pClient->connect(&adv)) { Serial.println("[BLE] Connection failed"); return false; }
      delay(500); pClient->setMTU(200); delay(200);
      auto svc = pClient->getService(SERVICE_UUID);
      if (!svc) { Serial.println("[BLE] Service not found"); return false; }
      pCharRx = svc->getCharacteristic(CHAR_UUID_RX);
      pCharTx = svc->getCharacteristic(CHAR_UUID_TX);
      if (!pCharRx || (!pCharTx)) { Serial.println("[BLE] Chars not found"); return false; }
      if (pCharTx->canNotify()) { pCharTx->registerForNotify(notifyCB); Serial.println("[BLE] Wywołano registerForNotify dla TX."); } 
      else { Serial.println("[BLE] TX nie obsługuje NOTIFY!"); return false; }
      return true;
    }
  }
  return false;
}


uint8_t* decodeJpegToGrayscale(const char* path, uint16_t* out_width, uint16_t* out_height) {
  Serial.printf("[DECODE] Dekodowanie %s do grayscale (JPEGDEC)...\n", path);

  File jpgFile = SD.open(path, FILE_READ);
  if (!jpgFile) {
    Serial.printf("[DECODE ERROR] Nie można otworzyć pliku SD: %s\n", path);
    return nullptr;
  }
  
  int32_t fileSize = jpgFile.size();

  if (jpegDec.open(&jpgFile, fileSize, jpegCloseCallback, jpegReadCallback, jpegSeekCallback, jpegDrawCallback) != 1) {
    Serial.println("[DECODE ERROR] Błąd jpegDec.open()!");
    jpgFile.close();
    return nullptr;
  }
  
  *out_width = jpegDec.getWidth();
  *out_height = jpegDec.getHeight();

  // === DODANO SPRAWDZENIE ROZDZIELCZOŚCI ===
  if (*out_width != img_width || *out_height != img_height) {
    Serial.printf("[DECODE ERROR] Niezgodność rozmiaru! Oczekiwano %dx%d, plik ma %dx%d\n", img_width, img_height, *out_width, *out_height);
    jpegDec.close();
    return nullptr;
  }
  // ==========================================
  
  global_gray_buf = (uint8_t*)heap_caps_malloc((*out_width) * (*out_height), MALLOC_CAP_SPIRAM);
  if (!global_gray_buf) {
    Serial.println("[DECODE ERROR] Brak pamięci na gray_buf!");
    jpegDec.close(); 
    return nullptr;
  }
  global_img_width = *out_width;

  // === ZMIENIONO ===
  // jpegDec.setPixelType(JPEG_GRAYSCALE); // BŁĄD: Zła stała
  jpegDec.setPixelType(EIGHT_BIT_GRAYSCALE); // POPRAWKA: Użyj poprawnej stałej z biblioteki JPEGDEC
  // =================

  Serial.printf("[DECODE DEBUG] Przed dekodowaniem - Wolny Heap (wewn.): %d B\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));

  if (jpegDec.decode(0, 0, 0) != 1) {
    Serial.println("[DECODE ERROR] Błąd jpegDec.decode()!");
    heap_caps_free(global_gray_buf);
    global_gray_buf = nullptr;
    jpegDec.close(); 
    return nullptr;
  }

  jpegDec.close();
  
  Serial.printf("[DECODE OK] Dekodowano do grayscale: %ux%u\n", *out_width, *out_height);
  return global_gray_buf;
}

void sendFullImage(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[sendFullImage] BŁĄD: Nie można otworzyć pliku %s!\n", path);
    return;
  }

  uint32_t fileSize = f.size();

  if (fileSize == 0) {
    Serial.printf("[sendFullImage] BŁĄD: Plik %s jest pusty lub nie istnieje! Anulowanie wysyłania.\n", path);
    f.close();
    return;
  }
  f.close();

  uint16_t totalPkts = (fileSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
  Serial.println("\n====================================");
  Serial.println("[sendFullImage] Rozpoczynam wysyłanie pełnego obrazu z karty SD");
  Serial.printf("[sendFullImage] Plik: %s, Rozmiar: %lu B, Pakiety: %u\n", path, fileSize, totalPkts);
  Serial.println("====================================");

  BleHeader header;
  header.magic_start[0] = 0xAB;
  header.magic_start[1] = 0xCD;
  header.tx_type = BLE_TX_TYPE_FULL_IMAGE;
  header.data_size = fileSize;
  header.width = img_width;
  header.height = img_height;
  header.block_size = block_size;
  header.magic_end[0] = 0xEF;
  header.magic_end[1] = 0x12;

  ackValue = 0x00;
  pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
  Serial.println("[sendFullImage] Wysłano nagłówek. Czekam na ACK...");
  unsigned long startWait = millis();
  while (ackValue != 0x01 && millis() - startWait < 2000) { delay(10); }
  if (ackValue != 0x01) { Serial.println("[sendFullImage] Timeout lub zły ACK dla nagłówka! Przerwanie."); resetBLEClient(); return; }

  f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[sendFullImage] BŁĄD: Nie można było ponownie otworzyć pliku %s do wysłania!\n", path);
    return;
  }
  
  Serial.println("[sendFullImage] Otrzymano ACK dla nagłówka. Wysyłam dane...");
  uint8_t buf[PAYLOAD_SIZE];
  for (uint16_t i = 0; i < totalPkts; ++i) {
    size_t len = f.read(buf, PAYLOAD_SIZE);
    if (len > 0) { pCharRx->writeValue(buf, len, false); }
    delay(5);
  }
  f.close();
  Serial.println("[sendFullImage] Wszystkie pakiety wysłane. Czekam na finalny ACK...");
  
  startWait = millis();
  while (ackValue != 0xAA && millis() - startWait < 8000) { delay(10); }

  if (ackValue == 0xAA) {
    Serial.println("[sendFullImage] Otrzymano finalny ACK (0xAA). Transmisja udana.");

    Serial.println("[sendFullImage] Dekodowanie wysłanego obrazu do bufora 'prev_gray'...");
    
    if (prev_gray) {
      heap_caps_free(prev_gray);
      prev_gray = nullptr;
    }

    uint16_t w, h;
    prev_gray = decodeJpegToGrayscale(path, &w, &h);

    if (prev_gray && (w == img_width && h == img_height)) {
      Serial.println("[sendFullImage] Pomyślnie załadowano 'prev_gray'.");
      first_press = false;
      Serial.println("[DEBUG] Przełączam na mode diff (first_press = false).");
    } else {
      Serial.println("[sendFullImage] BŁĄD: Nie udało się załadować 'prev_gray' po wysłaniu!");
      if (prev_gray) heap_caps_free(prev_gray);
      prev_gray = nullptr;
    }

  } else {
    Serial.printf("[sendFullImage] Timeout na finalny ACK! Otrzymano: 0x%02X\n", ackValue);
    resetBLEClient();
  }
  ackValue = 0x00;
}

// === ZMIENIONO ===
// Funkcja przyjmuje teraz ścieżkę do pliku jako argument
void computeAndSendSparseJpegDiff(const char* current_frame_path) {
  Serial.printf("\n[computeDiff] Obliczanie diff dla %s względem prev (tryb Sparse JPEG)...\n", current_frame_path);
  
  // Logowanie wybranej metody i progu
  if (currentDiffMethod == METHOD_MAD) {
    Serial.printf("[computeDiff] Używana metoda: MAD (Próg: %.2f)\n", mad_threshold);
  } else if (currentDiffMethod == METHOD_COSINE_DISTANCE) {
    Serial.printf("[computeDiff] Używana metoda: Cosine Distance (Próg: %.4f)\n", cosine_dist_threshold);
  } else if (currentDiffMethod == METHOD_MSE) {
    Serial.printf("[computeDiff] Używana metoda: MSE (Próg: %.2f)\n", mse_threshold);
  } else if (currentDiffMethod == METHOD_DSSIM) {
    Serial.printf("[computeDiff] Używana metoda: DSSIM (Próg: %.4f)\n", dssim_threshold);
  }

  // Sprawdzenie bufora poprzedniej klatki
  if (!prev_gray) { 
    Serial.println("[computeDiff] BŁĄD: Bufor 'prev_gray' jest pusty!"); 
    first_press = true; 
    return; 
  }

  uint16_t curr_w, curr_h;
  // Dekodowanie aktualnej klatki
  uint8_t* curr_gray = decodeJpegToGrayscale(current_frame_path, &curr_w, &curr_h); 
  
  if (!curr_gray) { 
    Serial.printf("[computeDiff] BŁĄD: Nie udało się zdekodować %s\n", current_frame_path); 
    return; 
  }
  
  if (curr_w != img_width || curr_h != img_height) { 
    Serial.println("[computeDiff] BŁĄD: Niezgodna rozdzielczość curr!"); 
    heap_caps_free(curr_gray); 
    return; 
  }

  // Alokacja bufora na wynikowy obraz "rzadki"
  uint8_t* sparse_gray_buf = (uint8_t*)heap_caps_malloc(img_width * img_height, MALLOC_CAP_SPIRAM);
  if (!sparse_gray_buf) { 
    Serial.println("[computeDiff] BŁĄD: Brak pamięci na sparse_gray_buf!"); 
    heap_caps_free(curr_gray); 
    return; 
  }
  // Wypełnij tłem (0x00)
  memset(sparse_gray_buf, sparse_background_color, img_width * img_height);

  int rows = img_height / block_size;
  int cols = img_width / block_size;
  int changed_count = 0;
  unsigned long diff_start_time = millis();

  // --- GŁÓWNA PĘTLA PO BLOKACH ---
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      int y = i * block_size, x = j * block_size;
      
      if (y + block_size > img_height || x + block_size > img_width) continue;
      
      bool is_block_changed = false;

      // =========================================================
      // === LOGIKA DETEKCJI ZMIAN - WYBÓR ALGORYTMU ===
      // =========================================================
      
      if (currentDiffMethod == METHOD_MAD) {
        // --- METODA 1: MAD (Mean Absolute Difference) ---
        float mad = 0.0;
        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t pixel_idx = (y + k) * img_width + (x + m);
                mad += abs(curr_gray[pixel_idx] - prev_gray[pixel_idx]); 
            } 
        }
        mad /= (block_size * block_size);
        if (mad > mad_threshold) is_block_changed = true;

      } else if (currentDiffMethod == METHOD_COSINE_DISTANCE) {
        // --- METODA 2: Cosine Distance ---
        double dot_product = 0.0;
        double norm_prev_sq = 0.0;
        double norm_curr_sq = 0.0;

        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t pixel_idx = (y + k) * img_width + (x + m);
                uint8_t val_prev = prev_gray[pixel_idx];
                uint8_t val_curr = curr_gray[pixel_idx];

                dot_product += (double)val_prev * val_curr;
                norm_prev_sq += (double)val_prev * val_prev;
                norm_curr_sq += (double)val_curr * val_curr;
            }
        }

        double similarity = 0.0;
        if (norm_prev_sq == 0 && norm_curr_sq == 0) similarity = 1.0; 
        else if (norm_prev_sq == 0 || norm_curr_sq == 0) similarity = 0.0; 
        else similarity = dot_product / (sqrt(norm_prev_sq) * sqrt(norm_curr_sq));

        double distance = 1.0 - similarity;
        if (distance > cosine_dist_threshold) is_block_changed = true;

      } else if (currentDiffMethod == METHOD_MSE) {
        // --- METODA 3: MSE (Mean Squared Error) ---
        float mse = 0.0;
        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t pixel_idx = (y + k) * img_width + (x + m);
                int16_t diff = (int16_t)curr_gray[pixel_idx] - (int16_t)prev_gray[pixel_idx];
                mse += (diff * diff); 
            } 
        }
        mse /= (block_size * block_size);
        if (mse > mse_threshold) is_block_changed = true;

      } else if (currentDiffMethod == METHOD_DSSIM) {
        // --- METODA 4: DSSIM (Structural Dissimilarity) ---
        // Implementacja SSIM dla bloku.
        // Stałe C1 i C2 dla L=255, K1=0.01, K2=0.03
        const float C1 = 6.5025f;  
        const float C2 = 58.5225f; 

        float sum_p = 0, sum_c = 0;
        float sum_sq_p = 0, sum_sq_c = 0, sum_pc = 0;
        int num_pixels = block_size * block_size;

        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t pixel_idx = (y + k) * img_width + (x + m);
                float val_prev = (float)prev_gray[pixel_idx];
                float val_curr = (float)curr_gray[pixel_idx];

                sum_p += val_prev;
                sum_c += val_curr;
                sum_sq_p += val_prev * val_prev;
                sum_sq_c += val_curr * val_curr;
                sum_pc += val_prev * val_curr;
            } 
        }

        float mu_p = sum_p / num_pixels;
        float mu_c = sum_c / num_pixels;

        // Wariancje i kowariancja
        float sigma_sq_p = (sum_sq_p / num_pixels) - (mu_p * mu_p);
        float sigma_sq_c = (sum_sq_c / num_pixels) - (mu_c * mu_c);
        float sigma_pc = (sum_pc / num_pixels) - (mu_p * mu_c);

        // SSIM
        float numerator = (2 * mu_p * mu_c + C1) * (2 * sigma_pc + C2);
        float denominator = (mu_p * mu_p + mu_c * mu_c + C1) * (sigma_sq_p + sigma_sq_c + C2);
        float ssim = numerator / denominator;

        // DSSIM (Distance): 0 = identyczne, 1 = max różnica
        float dssim = (1.0f - ssim) / 2.0f;

        if (dssim > dssim_threshold) is_block_changed = true;
      }
      
      // =========================================================

      // Jeśli wykryto zmianę, skopiuj blok do bufora sparse
      if (is_block_changed) {
        changed_count++;
        for (int k = 0; k < block_size; k++) {
          int_fast32_t offset = (y + k) * img_width + x;
          uint8_t* src_ptr = curr_gray + offset;
          uint8_t* dst_ptr = sparse_gray_buf + offset;
          for (int m = 0; m < block_size; m++) {
            if (*src_ptr == 0x00) {
              *dst_ptr = 0x01; // Zachowaj 0x00 dla tła
            } else {
              *dst_ptr = *src_ptr; 
            }
            src_ptr++;
            dst_ptr++;
          }
        }
      }
    }
  }
  Serial.printf("[computeDiff] Czas obliczania diff: %lu ms\n", millis() - diff_start_time);
  Serial.printf("[computeDiff] Zmienione bloki: %d / %d\n", changed_count, rows * cols);

  // Aktualizacja referencji
  heap_caps_free(prev_gray);
  prev_gray = curr_gray; 

  if (changed_count == 0) { 
    Serial.println("[computeDiff] Brak zmian - pomijam wysyłanie."); 
    heap_caps_free(sparse_gray_buf); 
    return; 
  }

  // --- KODOWANIE JPEG (Sparse) ---
  Serial.println("[JPEGENC] Rozpoczynam kodowanie Sparse JPEG...");
  unsigned long encode_start_time = millis();
  int result = JPEGE_SUCCESS;
  JPEGENCODE jpe; 

  result = jpegEnc.open("memory", myJpegEncOpen, myJpegEncClose, myJpegEncRead, myJpegEncWrite, myJpegEncSeek);

  if (result == JPEGE_SUCCESS) {
      int quality_level;
      if (jpeg_encode_quality >= 90) quality_level = JPEGE_Q_BEST;
      else if (jpeg_encode_quality >= 75) quality_level = JPEGE_Q_HIGH;
      else if (jpeg_encode_quality >= 50) quality_level = JPEGE_Q_MED;
      else quality_level = JPEGE_Q_LOW;

      result = jpegEnc.encodeBegin(&jpe, img_width, img_height, JPEGE_PIXEL_GRAYSCALE, JPEGE_SUBSAMPLE_444, quality_level);

      if (result == JPEGE_SUCCESS) {
          int mcu_w = jpe.cx; int mcu_h = jpe.cy; int stride = img_width;
          uint8_t mcu_buffer[mcu_w * mcu_h]; 

          int mcu_rows = (img_height + mcu_h - 1) / mcu_h;
          int mcu_cols = (img_width + mcu_w - 1) / mcu_w;

          for (int mcu_y = 0; mcu_y < mcu_rows && result == JPEGE_SUCCESS; mcu_y++) {
              for (int mcu_x = 0; mcu_x < mcu_cols && result == JPEGE_SUCCESS; mcu_x++) {
                  int src_y_start = mcu_y * mcu_h; int src_x_start = mcu_x * mcu_w;
                  for (int y_in_mcu = 0; y_in_mcu < mcu_h; y_in_mcu++) {
                      int src_y = src_y_start + y_in_mcu;
                      if (src_y < img_height) {
                          uint8_t *src_ptr = sparse_gray_buf + src_y * stride + src_x_start;
                          uint8_t *dst_ptr = mcu_buffer + y_in_mcu * mcu_w;
                          int copy_len = mcu_w;
                          if (src_x_start + mcu_w > img_width) copy_len = img_width - src_x_start;
                          memcpy(dst_ptr, src_ptr, copy_len);
                          if (copy_len < mcu_w) memset(dst_ptr + copy_len, sparse_background_color, mcu_w - copy_len);
                      } else {
                          memset(mcu_buffer + y_in_mcu * mcu_w, sparse_background_color, mcu_w);
                      }
                  }
                  result = jpegEnc.addMCU(&jpe, mcu_buffer, mcu_w);
              }
          }
      }
      jpegEnc.close();
  }

  unsigned long encode_time = millis() - encode_start_time;
  heap_caps_free(sparse_gray_buf); 

  if (result != JPEGE_SUCCESS) { 
      Serial.printf("[JPEGENC ERROR] Kodowanie nie powiodło się! Ostatni kod: %d\n", result);
      encodedJpegData.clear();
      return;
  }

  uint32_t encoded_size = encodedJpegData.size();
  Serial.printf("[JPEGENC OK] Kodowanie zakończone (%lu B) w %lu ms.\n", encoded_size, encode_time);

  // --- WYSYŁANIE BLE ---
  uint16_t totalPkts = (encoded_size + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
  Serial.printf("[sendSparse] Wysyłanie Sparse JPEG: Rozmiar: %u B, Pakiety: %u\n", encoded_size, totalPkts);

  BleHeader header;
  header.magic_start[0] = 0xAB; header.magic_start[1] = 0xCD;
  header.tx_type = BLE_TX_TYPE_SPARSE_JPEG; 
  header.data_size = encoded_size;
  header.width = img_width; header.height = img_height;
  header.block_size = block_size;
  header.magic_end[0] = 0xEF; header.magic_end[1] = 0x12;

  ackValue = 0x00;
  pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
  
  unsigned long startWait = millis();
  while (ackValue != 0x01 && millis() - startWait < 2000) { delay(10); }
  if (ackValue != 0x01) { Serial.println("[sendSparse] Timeout ACK nagłówka!"); return; }

  size_t offset = 0;
  while (offset < encoded_size) {
    size_t len = min(PAYLOAD_SIZE, encoded_size - offset);
    pCharRx->writeValue(&encodedJpegData[offset], len, false);
    offset += len;
    delay(5);
  }

  startWait = millis();
  while (ackValue != 0xAA && millis() - startWait < 8000) { delay(10); }
  if (ackValue == 0xAA) { Serial.println("[sendSparse] Finalny ACK OK."); }
  else { Serial.printf("[sendSparse] Timeout finalny ACK: 0x%02X\n", ackValue); resetBLEClient(); }
  ackValue = 0x00;
}

/**
 * @brief Oblicza różnice (diff) i wysyła je przez BLE.
 * === ZMIENIONO: Ta funkcja również została zaktualizowana, aby przyjmować ścieżkę ===
 */
void computeAndSendDiff(const char* current_frame_path) {
  Serial.printf("\n[computeDiff] Obliczanie diff dla %s względem prev...\n", current_frame_path);

  if (!prev_gray) {
    Serial.println("[computeDiff] BŁĄD: Bufor 'prev_gray' jest pusty (nullptr)!");
    Serial.println("[computeDiff] Wymuszam ponowne wysłanie pełnego obrazu.");
    first_press = true;
    return;
  }
  
  // Dekoduj curr (używając ścieżki)
  uint16_t curr_w, curr_h;
  uint8_t* curr_gray = decodeJpegToGrayscale(current_frame_path, &curr_w, &curr_h);
  if (!curr_gray) {
      Serial.printf("[computeDiff] BŁĄD: Nie udało się zdekodować %s\n", current_frame_path);
      return;
  }
  if (curr_w != img_width || curr_h != img_height) {
    Serial.println("[computeDiff] BŁĄD: Niezgodna rozdzielczość curr!");
    heap_caps_free(curr_gray);
    return;
  }

  int rows = img_height / block_size;
  int cols = img_width / block_size;
  std::vector<uint8_t> diff_data;
  diff_data.push_back(rows);
  diff_data.push_back(cols);

  int changed_count = 0;
  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      int y = i * block_size, x = j * block_size;
      uint8_t* block_prev = prev_gray + y * img_width + x;
      uint8_t* block_curr = curr_gray + y * img_width + x;

      float mad = 0.0;
      for (int k = 0; k < block_size; k++) {
        for (int m = 0; m < block_size; m++) {
          mad += abs(block_curr[k * img_width + m] - block_prev[k * img_width + m]);
        }
      }
      mad /= (block_size * block_size);

      // Serial.printf("[computeDiff BLOCK %d,%d] MAD: %.2f\n", i, j, mad); // Trochę zbyt szczegółowe

      if (mad > mad_threshold) {
        changed_count++;
        diff_data.push_back(i);
        diff_data.push_back(j);
        uint16_t bl_len = block_size * block_size;
        diff_data.insert(diff_data.end(), (uint8_t*)&bl_len, (uint8_t*)&bl_len + 2);
        for (int k = 0; k < block_size; k++) {
          diff_data.insert(diff_data.end(), block_curr + k * img_width, block_curr + k * img_width + block_size);
        }
      }
    }
  }

  Serial.printf("[computeDiff] Zmienione bloki: %d, Rozmiar diff_data: %d B\n", changed_count, diff_data.size());

  if (changed_count > 0) {
    uint32_t data_size = diff_data.size();
    uint16_t totalPkts = (data_size + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
    Serial.printf("[sendDiff] Wysyłanie diff: Rozmiar: %u B, Pakiety: %u\n", data_size, totalPkts);

    BleHeader header;
    header.magic_start[0] = 0xAB;
    header.magic_start[1] = 0xCD;
    header.tx_type = BLE_TX_TYPE_DIFF_RLE;
    header.data_size = data_size;
    header.width = img_width;
    header.height = img_height;
    header.block_size = block_size;
    header.magic_end[0] = 0xEF;
    header.magic_end[1] = 0x12;

    ackValue = 0x00;
    pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
    Serial.println("[sendDiff] Wysłano nagłówek. Czekam na ACK...");
    unsigned long startWait = millis();
    while (ackValue != 0x01 && millis() - startWait < 2000) { delay(10); }
    if (ackValue != 0x01) { Serial.println("[sendDiff] Timeout ACK nagłówka!"); heap_caps_free(curr_gray); return; }
    Serial.println("[sendDiff] ACK nagłówka OK. Wysyłam dane...");

    size_t offset = 0;
    while (offset < data_size) {
      size_t len = min(PAYLOAD_SIZE, data_size - offset);
      pCharRx->writeValue(&diff_data[offset], len, false);
      offset += len;
      delay(5);
    }

    Serial.println("[sendDiff] Dane wysłane. Czekam na finalny ACK...");
    startWait = millis();
    while (ackValue != 0xAA && millis() - startWait < 8000) { delay(10); }
    if (ackValue == 0xAA) { Serial.println("[sendDiff] Finalny ACK OK."); } 
    else { Serial.printf("[sendDiff] Timeout finalny ACK: 0x%02X\n", ackValue); resetBLEClient(); }
    ackValue = 0x00;
  } else {
    Serial.println("[computeDiff] Brak zmian - pomijam wysyłanie.");
  }

  // Aktualizuj prev na curr
  heap_caps_free(prev_gray);
  prev_gray = curr_gray;
}

/**
 * @brief Główna funkcja wyzwalana przyciskiem.
 * === ZMIENIONO: Cała logika sekwencji plików jest tutaj ===
 */
void captureAndProcessImage() {
  
  // Sprawdź, czy sekwencja się nie skończyła
  if (current_frame_index > max_frame_index) {
    current_frame_index = 1; // Zresetuj do pierwszej klatki
    first_press = true;    // Wymuś ponowne wysłanie pełnej klatki bazowej
    Serial.println("\n[ACTION] Sekwencja zakończona. Restartuję od klatki bazowej.");
  }

  // Wygeneruj nazwę pliku na podstawie aktualnego indeksu
  // Format: /frame_qvga_2746004_0001.jpg
  sprintf(current_filename_buffer, "%s%04d%s", base_filename, current_frame_index, file_extension);

  Serial.printf("\n[ACTION] Przycisk wciśnięty. Przetwarzam plik: %s\n", current_filename_buffer);
  Serial.printf("[ACTION] Aktualny stan flagi first_press: %s\n", first_press ? "true" : "false");

  if (first_press) {
    // Upewnij się, że na pewno wysyłamy pierwszą klatkę jako bazową
    if (current_frame_index != 1) {
        Serial.println("[ACTION ERROR] Niespójność stanu! first_press=true, ale indeks != 1. Resetuję.");
        current_frame_index = 1;
        first_press = true;
        sprintf(current_filename_buffer, "%s%04d%s", base_filename, current_frame_index, file_extension);
    }
    sendFullImage(current_filename_buffer);
  } else {
    // Wyślij klatkę różnicową
    computeAndSendSparseJpegDiff(current_filename_buffer);
  }

  // Przygotuj się na następne naciśnięcie przycisku
  current_frame_index++;
}

// ===============================================================
// === SETUP & LOOP ===
// ===============================================================

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("Starting Node A (Diff Version)...");

  pinMode(buttonPin, INPUT_PULLUP);
  Serial.println("[OK] Przycisk skonfigurowany.");

  initCamera();
  sdInit();
  initBLE();
}

void loop() {
  if (!pClient || !pClient->isConnected()) {
    unsigned long now = millis();
    if (now - lastConnectAttempt > CONNECT_RETRY_INTERVAL) {
      lastConnectAttempt = now;
      Serial.println("[BLE] Próba połączenia...");
      resetBLEClient();
      initBLE();
      if (connectToNodeB()) {
        Serial.println("[BLE] Połączono!");
        delay(500);
      } else { Serial.println("[BLE] Błąd połączenia."); }
    }
    delay(100);
    return;
  }

  int reading = digitalRead(buttonPin);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // Przycisk wciśnięty (LOW, bo PULLUP)
        captureAndProcessImage();
      }
    }
  }
  lastButtonState = reading;
}