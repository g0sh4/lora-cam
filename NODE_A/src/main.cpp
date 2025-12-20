#define CAMERA_MODEL_XIAO_ESP32S3  // <-- WYMAGANE przed "camera_pins.h"
#include "camera_pins.h"

#include <esp_camera.h>
#include <SD.h>
#include <SPI.h>
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

// === KONFIGURACJA ALGORYTMÓW RÓŻNICOWYCH ===
enum DiffMethod {
  METHOD_MAD,             // Mean Absolute Difference
  METHOD_COSINE_DISTANCE, // Cosine Distance
  METHOD_MSE,             // Mean Squared Error
  METHOD_DSSIM            // Structural Dissimilarity
};

// --- WYBIERZ METODĘ TUTAJ ---
DiffMethod currentDiffMethod = METHOD_DSSIM; 
// ----------------------------

bool first_press = true;  // Stan: Pierwsze kliknięcie - full, kolejne - diff

// Parametry obrazu (QVGA)
uint16_t img_width = 320;  
uint16_t img_height = 240;

// Progi detekcji
float mad_threshold = 5.0;  
float cosine_dist_threshold = 0.005; 
float mse_threshold = 180.0; 
float dssim_threshold = 0.05; 

uint8_t block_size = 16; 
uint8_t jpeg_encode_quality = 80; 
uint8_t sparse_background_color = 0x00; 

// Globalne bufory
static uint8_t* global_gray_buf = nullptr;
static uint16_t global_img_width = 0;
static uint8_t* prev_gray = nullptr; // Bufor na poprzedni obraz
static std::vector<uint8_t> encodedJpegData;

// === ZMIENNE DO ZAPISU PLIKÓW ===
static int capture_counter = 0;          // Licznik wykonanych zdjęć
static char current_filename_buffer[50]; // Bufor na nazwę pliku
// =================================

// === ZMIENNE DO CYKLICZNEGO DZIAŁANIA ===
unsigned long lastCaptureTime = 0;
const unsigned long CAPTURE_INTERVAL = 10000; // 10 sekund (w ms)
// ========================================


// ===============================================================
// === FUNKCJE POMOCNICZE (CALLBACKI) ===
// ===============================================================

static int32_t jpegReadCallback(JPEGFILE *pHandle, uint8_t *pBuffer, int32_t iLength) {
  File *f = static_cast<File *>(pHandle->fHandle);
  return f->read(pBuffer, iLength);
}

static int32_t jpegSeekCallback(JPEGFILE *pHandle, int32_t iPosition) {
  File *f = static_cast<File *>(pHandle->fHandle);
  if (f->seek(iPosition)) return 0;
  return -1;
}

static void jpegCloseCallback(void *pHandle) {
  File *f = static_cast<File *>(pHandle);
  f->close();
}

static int jpegDrawCallback(JPEGDRAW *pDraw) {
  uint8_t *pPixels = (uint8_t *)pDraw->pPixels;
  int x = pDraw->x, y = pDraw->y, w = pDraw->iWidth, h = pDraw->iHeight;
  for (int py = 0; py < h; py++) {
    int_fast32_t src_offset = py * w;
    int_fast32_t dst_offset = (y + py) * global_img_width + x;
    memcpy(global_gray_buf + dst_offset, pPixels + src_offset, w);
  }
  return 1;
}

void * myJpegEncOpen(const char *filename) {
    encodedJpegData.clear();
    encodedJpegData.reserve(img_width * img_height / 10);
    return (void *)&encodedJpegData;
}
void myJpegEncClose(JPEGE_FILE *p) { }
int32_t myJpegEncWrite(JPEGE_FILE *p, uint8_t *buffer, int32_t length) {
    std::vector<uint8_t> *v = static_cast<std::vector<uint8_t> *>(p->fHandle);
    try { v->insert(v->end(), buffer, buffer + length); }
    catch (const std::bad_alloc& e) { return 0; }
    return length;
}
int32_t myJpegEncRead(JPEGE_FILE *p, uint8_t *buffer, int32_t length) { return 0; }
int32_t myJpegEncSeek(JPEGE_FILE *p, int32_t position) { return 0; }

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
    .frame_size = FRAMESIZE_QVGA, // 320x240
    .jpeg_quality = 12,           // Dobra jakość JPEG z kamery
    .fb_count = 1
  };
  if (esp_camera_init(&cfg) != ESP_OK) { Serial.println("[ERROR] Camera init failed"); while (true) delay(1000); } 
  else { 
    Serial.println("[OK] Camera initialized successfully");
    
    // === USTAWIENIE EFEKTU CZARNO-BIAŁEGO ===
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
      // 0: No Effect, 1: Negative, 2: Grayscale, 3: Red Tint, 4: Green Tint, 5: Blue Tint, 6: Sepia
      s->set_special_effect(s, 2); // 2 = Grayscale
      Serial.println("[CAMERA] Ustawiono tryb Grayscale (Czarno-Biały)");
    }
    // ========================================
  }
}

static void sdInit() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 25000000)) { Serial.println("[ERROR] SD Init Fail"); while (true) delay(1000); }
  Serial.println("[OK] SD Card initialized.");
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
      if (!pClient->connect(&adv)) return false;
      delay(500); pClient->setMTU(200); delay(200);
      auto svc = pClient->getService(SERVICE_UUID);
      if (!svc) return false;
      pCharRx = svc->getCharacteristic(CHAR_UUID_RX);
      pCharTx = svc->getCharacteristic(CHAR_UUID_TX);
      if (!pCharRx || !pCharTx) return false;
      if (pCharTx->canNotify()) pCharTx->registerForNotify(notifyCB);
      return true;
    }
  }
  return false;
}

uint8_t* decodeJpegToGrayscale(const char* path, uint16_t* out_width, uint16_t* out_height) {
  File jpgFile = SD.open(path, FILE_READ);
  if (!jpgFile) return nullptr;
  
  int32_t fileSize = jpgFile.size();
  if (jpegDec.open(&jpgFile, fileSize, jpegCloseCallback, jpegReadCallback, jpegSeekCallback, jpegDrawCallback) != 1) {
    jpgFile.close(); return nullptr;
  }
  
  *out_width = jpegDec.getWidth();
  *out_height = jpegDec.getHeight();

  if (*out_width != img_width || *out_height != img_height) {
    Serial.println("[DECODE ERROR] Wrong Resolution!");
    jpegDec.close(); return nullptr;
  }
  
  global_gray_buf = (uint8_t*)heap_caps_malloc((*out_width) * (*out_height), MALLOC_CAP_SPIRAM);
  if (!global_gray_buf) {
    jpegDec.close(); return nullptr;
  }
  global_img_width = *out_width;
  jpegDec.setPixelType(EIGHT_BIT_GRAYSCALE);

  if (jpegDec.decode(0, 0, 0) != 1) {
    heap_caps_free(global_gray_buf); global_gray_buf = nullptr;
    jpegDec.close(); return nullptr;
  }
  jpegDec.close();
  return global_gray_buf;
}

void sendFullImage(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) return;
  uint32_t fileSize = f.size();
  f.close();

  uint16_t totalPkts = (fileSize + PAYLOAD_SIZE - 1) / PAYLOAD_SIZE;
  Serial.printf("[sendFullImage] Plik: %s, Rozmiar: %lu B\n", path, fileSize);

  BleHeader header;
  header.magic_start[0] = 0xAB; header.magic_start[1] = 0xCD;
  header.tx_type = BLE_TX_TYPE_FULL_IMAGE;
  header.data_size = fileSize;
  header.width = img_width; header.height = img_height;
  header.block_size = block_size;
  header.magic_end[0] = 0xEF; header.magic_end[1] = 0x12;

  ackValue = 0x00;
  pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
  
  unsigned long startWait = millis();
  while (ackValue != 0x01 && millis() - startWait < 2000) delay(10);
  if (ackValue != 0x01) { resetBLEClient(); return; }

  f = SD.open(path, FILE_READ);
  uint8_t buf[PAYLOAD_SIZE];
  for (uint16_t i = 0; i < totalPkts; ++i) {
    size_t len = f.read(buf, PAYLOAD_SIZE);
    if (len > 0) pCharRx->writeValue(buf, len, false);
    delay(5);
  }
  f.close();
  
  startWait = millis();
  while (ackValue != 0xAA && millis() - startWait < 8000) delay(10);

  if (ackValue == 0xAA) {
    Serial.println("[sendFullImage] Sukces. Ładowanie prev_gray...");
    if (prev_gray) { heap_caps_free(prev_gray); prev_gray = nullptr; }
    
    uint16_t w, h;
    prev_gray = decodeJpegToGrayscale(path, &w, &h); // Dekoduj to co wysłaliśmy jako bazę
    
    if (prev_gray) {
      first_press = false; // Przełącz na tryb Diff
      Serial.println("[DEBUG] Tryb Diff aktywny.");
    }
  } else {
    resetBLEClient();
  }
  ackValue = 0x00;
}

void computeAndSendSparseJpegDiff(const char* current_frame_path) {
  Serial.printf("\n[computeDiff] Obliczanie diff dla %s...\n", current_frame_path);
  
  if (currentDiffMethod == METHOD_MAD) Serial.printf("Metoda: MAD (%.2f)\n", mad_threshold);
  else if (currentDiffMethod == METHOD_COSINE_DISTANCE) Serial.printf("Metoda: Cosine (%.4f)\n", cosine_dist_threshold);
  else if (currentDiffMethod == METHOD_MSE) Serial.printf("Metoda: MSE (%.2f)\n", mse_threshold);
  else if (currentDiffMethod == METHOD_DSSIM) Serial.printf("Metoda: DSSIM (%.4f)\n", dssim_threshold);

  if (!prev_gray) { 
    Serial.println("[ERROR] Brak prev_gray! Wymuszam Full Image."); 
    first_press = true; 
    return; 
  }

  uint16_t curr_w, curr_h;
  uint8_t* curr_gray = decodeJpegToGrayscale(current_frame_path, &curr_w, &curr_h); 
  if (!curr_gray) return;

  uint8_t* sparse_gray_buf = (uint8_t*)heap_caps_malloc(img_width * img_height, MALLOC_CAP_SPIRAM);
  if (!sparse_gray_buf) { heap_caps_free(curr_gray); return; }
  memset(sparse_gray_buf, sparse_background_color, img_width * img_height);

  int rows = img_height / block_size;
  int cols = img_width / block_size;
  int changed_count = 0;
  unsigned long diff_start_time = millis();

  for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
      int y = i * block_size, x = j * block_size;
      if (y + block_size > img_height || x + block_size > img_width) continue;
      
      bool is_block_changed = false;

      // === ALGORYTMY ===
      if (currentDiffMethod == METHOD_MAD) {
        float mad = 0.0;
        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t px = (y + k) * img_width + (x + m);
                mad += abs(curr_gray[px] - prev_gray[px]); 
            } 
        }
        if ((mad / (block_size * block_size)) > mad_threshold) is_block_changed = true;

      } else if (currentDiffMethod == METHOD_COSINE_DISTANCE) {
        double dot = 0, norm_p = 0, norm_c = 0;
        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t px = (y + k) * img_width + (x + m);
                dot += (double)prev_gray[px] * curr_gray[px];
                norm_p += (double)prev_gray[px] * prev_gray[px];
                norm_c += (double)curr_gray[px] * curr_gray[px];
            }
        }
        double sim = (norm_p == 0 || norm_c == 0) ? (norm_p == norm_c ? 1.0 : 0.0) : dot / (sqrt(norm_p) * sqrt(norm_c));
        if ((1.0 - sim) > cosine_dist_threshold) is_block_changed = true;

      } else if (currentDiffMethod == METHOD_MSE) {
        float mse = 0.0;
        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t px = (y + k) * img_width + (x + m);
                int16_t d = (int16_t)curr_gray[px] - (int16_t)prev_gray[px];
                mse += (d * d); 
            } 
        }
        if ((mse / (block_size * block_size)) > mse_threshold) is_block_changed = true;

      } else if (currentDiffMethod == METHOD_DSSIM) {
        const float C1 = 6.5025f, C2 = 58.5225f; 
        float s_p = 0, s_c = 0, s_sq_p = 0, s_sq_c = 0, s_pc = 0;
        int n_px = block_size * block_size;

        for (int k = 0; k < block_size; k++) { 
            for (int m = 0; m < block_size; m++) {
                uint32_t px = (y + k) * img_width + (x + m);
                float vp = prev_gray[px], vc = curr_gray[px];
                s_p += vp; s_c += vc;
                s_sq_p += vp * vp; s_sq_c += vc * vc; s_pc += vp * vc;
            } 
        }
        float mu_p = s_p / n_px, mu_c = s_c / n_px;
        float sig_sq_p = (s_sq_p / n_px) - (mu_p * mu_p);
        float sig_sq_c = (s_sq_c / n_px) - (mu_c * mu_c);
        float sig_pc = (s_pc / n_px) - (mu_p * mu_c);
        float ssim = ((2 * mu_p * mu_c + C1) * (2 * sig_pc + C2)) / ((mu_p * mu_p + mu_c * mu_c + C1) * (sig_sq_p + sig_sq_c + C2));
        if (((1.0f - ssim) / 2.0f) > dssim_threshold) is_block_changed = true;
      }

      if (is_block_changed) {
        changed_count++;
        for (int k = 0; k < block_size; k++) {
          int_fast32_t off = (y + k) * img_width + x;
          uint8_t* src = curr_gray + off;
          uint8_t* dst = sparse_gray_buf + off;
          for (int m = 0; m < block_size; m++) {
            *dst = (*src == 0x00) ? 0x01 : *src;
            src++; dst++;
          }
        }
      }
    }
  }
  // --- DODANO LOGOWANIE ZMIENIONYCH BLOKÓW ---
  Serial.printf("[computeDiff] Zmiana: %d bloków (%lu ms)\n", changed_count, millis() - diff_start_time);
  // ---------------------------------------------

  heap_caps_free(prev_gray);
  prev_gray = curr_gray; 

  if (changed_count == 0) { 
    Serial.println("Brak zmian."); heap_caps_free(sparse_gray_buf); return; 
  }

  // --- KODOWANIE ---
  int result = jpegEnc.open("memory", myJpegEncOpen, myJpegEncClose, myJpegEncRead, myJpegEncWrite, myJpegEncSeek);
  JPEGENCODE jpe; 
  if (result == JPEGE_SUCCESS) {
      jpegEnc.encodeBegin(&jpe, img_width, img_height, JPEGE_PIXEL_GRAYSCALE, JPEGE_SUBSAMPLE_444, JPEGE_Q_HIGH);
      int mcu_w = jpe.cx, mcu_h = jpe.cy;
      uint8_t mcu_buf[mcu_w * mcu_h]; 
      int mrows = (img_height + mcu_h - 1) / mcu_h, mcols = (img_width + mcu_w - 1) / mcu_w;

      for (int my = 0; my < mrows; my++) {
          for (int mx = 0; mx < mcols; mx++) {
              int sy_start = my * mcu_h, sx_start = mx * mcu_w;
              for (int ymc = 0; ymc < mcu_h; ymc++) {
                  int sy = sy_start + ymc;
                  if (sy < img_height) {
                      uint8_t *src = sparse_gray_buf + sy * img_width + sx_start;
                      uint8_t *dst = mcu_buf + ymc * mcu_w;
                      int cpy = (sx_start + mcu_w > img_width) ? img_width - sx_start : mcu_w;
                      memcpy(dst, src, cpy);
                      if (cpy < mcu_w) memset(dst + cpy, sparse_background_color, mcu_w - cpy);
                  } else memset(mcu_buf + ymc * mcu_w, sparse_background_color, mcu_w);
              }
              jpegEnc.addMCU(&jpe, mcu_buf, mcu_w);
          }
      }
      jpegEnc.close();
  }
  heap_caps_free(sparse_gray_buf); 

  uint32_t enc_size = encodedJpegData.size();
  Serial.printf("[sendSparse] Rozmiar: %u B\n", enc_size);

  BleHeader header;
  header.magic_start[0] = 0xAB; header.magic_start[1] = 0xCD;
  header.tx_type = BLE_TX_TYPE_SPARSE_JPEG; 
  header.data_size = enc_size;
  header.width = img_width; header.height = img_height;
  header.block_size = block_size;
  header.magic_end[0] = 0xEF; header.magic_end[1] = 0x12;

  ackValue = 0x00;
  pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
  unsigned long st = millis();
  while (ackValue != 0x01 && millis() - st < 2000) delay(10);
  if (ackValue != 0x01) return;

  size_t off = 0;
  while (off < enc_size) {
    size_t len = min(PAYLOAD_SIZE, enc_size - off);
    pCharRx->writeValue(&encodedJpegData[off], len, false);
    off += len;
    delay(5);
  }
  st = millis();
  while (ackValue != 0xAA && millis() - st < 8000) delay(10);
  ackValue = 0x00;
}

// === NOWA FUNKCJA POMOCNICZA DO ZAPISU ===
bool savePhotoToSD(camera_fb_t* fb, const char* filename) {
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
        Serial.printf("[SD ERROR] Nie można otworzyć pliku: %s\n", filename);
        return false;
    }
    file.write(fb->buf, fb->len);
    file.close();
    Serial.printf("[SD] Zapisano: %s (%u B)\n", filename, fb->len);
    return true;
}

/**
 * @brief Główna funkcja wyzwalana przyciskiem.
 * Pobiera zdjęcie -> Zapisuje na SD -> Wysyła (Full lub Diff)
 */
void captureAndProcessImage() {
  Serial.println("\n[ACTION] Auto Capture - start sekwencji.");

  // 1. Zrób zdjęcie
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAMERA ERROR] Błąd pobierania klatki!");
    return;
  }

  // 2. Wygeneruj nową nazwę pliku
  capture_counter++;
  sprintf(current_filename_buffer, "/cap_%04d.jpg", capture_counter);

  // 3. Zapisz na SD
  bool saved = savePhotoToSD(fb, current_filename_buffer);

  // 4. Zwolnij pamięć kamery (Kluczowe dla stabilności!)
  esp_camera_fb_return(fb);

  if (!saved) return;

  // 5. Wyślij
  if (first_press) {
    sendFullImage(current_filename_buffer);
  } else {
    computeAndSendSparseJpegDiff(current_filename_buffer);
  }
}

// ===============================================================
// === SETUP & LOOP ===
// ===============================================================

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("Starting Node A (Camera Capture Mode)...");

  pinMode(buttonPin, INPUT_PULLUP);

  initCamera();
  sdInit();
  initBLE();
  
  // Opcjonalnie: odczekaj chwilę po starcie przed pierwszym zdjęciem
  delay(2000);
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

  // === CYKLICZNE WYKONYWANIE ZDJĘĆ ===
  unsigned long now = millis();
  if (now - lastCaptureTime > CAPTURE_INTERVAL) {
    captureAndProcessImage();
    lastCaptureTime = now;
  }
  // ===================================

  // --- Zakomentowana obsługa przycisku ---
  /*
  int reading = digitalRead(buttonPin);
  if (reading != lastButtonState) lastDebounceTime = millis();

  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) { // Przycisk wciśnięty
        captureAndProcessImage();
      }
    }
  }
  lastButtonState = reading;
  */
}