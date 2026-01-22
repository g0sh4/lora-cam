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
#include <vector> 
#include <algorithm>
#include "ble_frame_structure.h" // Twój plik nagłówkowy

// ==========================================
// === KONFIGURACJA ŹRÓDŁA OBRAZU (NOWE) ===
// ==========================================
// true  = Tryb symulacji: pobiera gotowe pliki z listy testFiles (SD)
// false = Tryb Live: robi zdjęcia kamerą
const bool USE_SD_TEST_IMAGES = true; 

// Lista plików do testów (używana tylko gdy USE_SD_TEST_IMAGES = true)
const char* testFiles[] = {
"/img_20260116_102132.jpg",
"/img_20260116_102204.jpg",
"/img_20260116_102305.jpg",
"/img_20260116_102405.jpg",
"/img_20260116_102505.jpg",
"/img_20260116_102605.jpg",
"/img_20260116_102706.jpg",
"/img_20260116_102806.jpg",
"/img_20260116_102906.jpg",
"/img_20260116_103006.jpg",
"/img_20260116_103107.jpg",
"/img_20260116_103207.jpg",
"/img_20260116_103308.jpg",
"/img_20260116_103408.jpg",
"/img_20260116_103508.jpg",
"/img_20260116_103608.jpg",
"/img_20260116_103709.jpg",
"/img_20260116_103809.jpg",
"/img_20260116_103909.jpg",
"/img_20260116_104009.jpg",
"/img_20260116_104110.jpg",
"/img_20260116_104210.jpg",
"/img_20260116_104310.jpg",
"/img_20260116_104410.jpg",
"/img_20260116_104511.jpg",
"/img_20260116_104611.jpg",
"/img_20260116_104711.jpg",
"/img_20260116_104811.jpg",
"/img_20260116_104912.jpg",
"/img_20260116_105012.jpg",
"/img_20260116_105112.jpg",
"/img_20260116_105213.jpg",
"/img_20260116_105313.jpg",
"/img_20260116_105413.jpg",
"/img_20260116_105513.jpg",
"/img_20260116_105614.jpg",
"/img_20260116_105714.jpg",
"/img_20260116_105814.jpg",
"/img_20260116_105914.jpg",
"/img_20260116_110015.jpg",
"/img_20260116_110218.jpg",
"/img_20260116_110318.jpg",
"/img_20260116_110418.jpg",
"/img_20260116_110519.jpg",
"/img_20260116_110619.jpg",
"/img_20260116_110719.jpg",
"/img_20260116_110819.jpg",
"/img_20260116_110919.jpg",
"/img_20260116_111020.jpg",
"/img_20260116_111120.jpg",
"/img_20260116_111220.jpg",
"/img_20260116_111320.jpg",
"/img_20260116_111421.jpg",
"/img_20260116_111521.jpg",
"/img_20260116_111621.jpg",
"/img_20260116_111721.jpg",
"/img_20260116_111821.jpg",
"/img_20260116_111922.jpg",
"/img_20260116_112022.jpg",
"/img_20260116_112122.jpg",
"/img_20260116_112222.jpg",
"/img_20260116_112322.jpg",
"/img_20260116_112423.jpg",
"/img_20260116_112523.jpg",
"/img_20260116_112623.jpg",
"/img_20260116_112723.jpg",
"/img_20260116_112824.jpg",
"/img_20260116_112924.jpg",
"/img_20260116_113024.jpg",
"/img_20260116_113124.jpg",
"/img_20260116_113224.jpg",
"/img_20260116_113325.jpg",
"/img_20260116_113425.jpg",
"/img_20260116_113525.jpg",
"/img_20260116_113625.jpg",
"/img_20260116_113726.jpg",
"/img_20260116_113826.jpg",
"/img_20260116_113926.jpg",
"/img_20260116_114026.jpg",
"/img_20260116_114126.jpg",
"/img_20260116_114227.jpg",
"/img_20260116_114327.jpg",
"/img_20260116_114427.jpg",
"/img_20260116_114527.jpg",
"/img_20260116_114627.jpg",
"/img_20260116_114728.jpg",
"/img_20260116_114828.jpg",
"/img_20260116_114928.jpg",
"/img_20260116_115028.jpg",
"/img_20260116_115128.jpg",
"/img_20260116_115229.jpg",
"/img_20260116_115329.jpg",
"/img_20260116_115429.jpg",
"/img_20260116_115529.jpg",
"/img_20260116_115630.jpg",
"/img_20260116_115730.jpg",
"/img_20260116_115830.jpg",
"/img_20260116_115930.jpg",
"/img_20260116_120030.jpg",
"/img_20260116_120131.jpg",
"/img_20260116_120231.jpg",
"/img_20260116_120331.jpg",
"/img_20260116_120431.jpg",
"/img_20260116_120531.jpg",
"/img_20260116_120632.jpg",
"/img_20260116_120732.jpg",
"/img_20260116_120832.jpg",
"/img_20260116_120932.jpg",
"/img_20260116_121032.jpg",
"/img_20260116_121133.jpg",
"/img_20260116_121233.jpg",
"/img_20260116_121333.jpg",
"/img_20260116_121433.jpg",
"/img_20260116_121534.jpg",
"/img_20260116_121634.jpg",
"/img_20260116_121734.jpg",
"/img_20260116_121835.jpg",
"/img_20260116_121935.jpg",
"/img_20260116_122035.jpg",
"/img_20260116_122135.jpg",
"/img_20260116_122236.jpg",
"/img_20260116_122336.jpg",
"/img_20260116_122436.jpg",
"/img_20260116_122536.jpg",
"/img_20260116_122637.jpg",
"/img_20260116_122737.jpg",
"/img_20260116_122837.jpg",
"/img_20260116_122937.jpg",
"/img_20260116_123037.jpg",
"/img_20260116_123138.jpg",
"/img_20260116_123238.jpg",
"/img_20260116_123338.jpg",
"/img_20260116_123438.jpg",
"/img_20260116_123539.jpg",
"/img_20260116_123639.jpg",
"/img_20260116_123739.jpg",
"/img_20260116_123839.jpg",
"/img_20260116_123940.jpg",
"/img_20260116_124040.jpg",
"/img_20260116_124140.jpg",
"/img_20260116_124241.jpg",
"/img_20260116_124341.jpg",
"/img_20260116_124441.jpg",
"/img_20260116_124542.jpg",
"/img_20260116_124642.jpg",
"/img_20260116_124742.jpg",
"/img_20260116_124842.jpg",
"/img_20260116_124942.jpg",
"/img_20260116_125043.jpg",
"/img_20260116_125143.jpg",
"/img_20260116_125243.jpg",
"/img_20260116_125343.jpg",
"/img_20260116_125444.jpg",
"/img_20260116_125544.jpg",
"/img_20260116_125644.jpg",
"/img_20260116_125745.jpg",
"/img_20260116_125845.jpg",
"/img_20260116_125945.jpg",
"/img_20260116_130046.jpg",
"/img_20260116_130146.jpg",
"/img_20260116_130246.jpg",
"/img_20260116_130346.jpg",
"/img_20260116_130447.jpg",
"/img_20260116_130547.jpg",
"/img_20260116_130647.jpg",
"/img_20260116_130747.jpg",
"/img_20260116_130848.jpg",
"/img_20260116_130948.jpg",
"/img_20260116_131048.jpg",
"/img_20260116_131148.jpg",
"/img_20260116_131248.jpg",
"/img_20260116_131349.jpg",
"/img_20260116_131449.jpg",
"/img_20260116_131550.jpg",
"/img_20260116_131650.jpg",
"/img_20260116_131750.jpg",
"/img_20260116_131851.jpg",
"/img_20260116_131951.jpg",
"/img_20260116_132051.jpg",
"/img_20260116_132151.jpg",
"/img_20260116_132252.jpg",
"/img_20260116_132352.jpg",
"/img_20260116_132452.jpg",
"/img_20260116_132552.jpg",
"/img_20260116_132652.jpg",
"/img_20260116_132753.jpg",
"/img_20260116_132853.jpg",
"/img_20260116_132953.jpg",
"/img_20260116_133054.jpg",
"/img_20260116_133154.jpg",
"/img_20260116_133254.jpg",
"/img_20260116_133354.jpg",
"/img_20260116_133455.jpg",
"/img_20260116_133555.jpg",
"/img_20260116_133655.jpg",
"/img_20260116_133755.jpg",
"/img_20260116_133855.jpg",
"/img_20260116_133956.jpg",
"/img_20260116_134056.jpg",
"/img_20260116_134156.jpg",
"/img_20260116_134256.jpg",
"/img_20260116_134357.jpg",
"/img_20260116_134457.jpg",
"/img_20260116_134557.jpg",
"/img_20260116_134658.jpg",
"/img_20260116_134758.jpg",
"/img_20260116_134858.jpg",
"/img_20260116_134958.jpg",
"/img_20260116_135058.jpg",
"/img_20260116_135159.jpg",
"/img_20260116_135259.jpg",
"/img_20260116_135359.jpg",
"/img_20260116_135459.jpg",
"/img_20260116_135600.jpg",
"/img_20260116_135700.jpg",
"/img_20260116_135800.jpg",
"/img_20260116_135900.jpg",
"/img_20260116_140000.jpg",
"/img_20260116_140101.jpg",
"/img_20260116_140201.jpg",
"/img_20260116_140301.jpg",
"/img_20260116_140401.jpg",
"/img_20260116_140502.jpg",
"/img_20260116_140602.jpg",
"/img_20260116_140702.jpg",
"/img_20260116_140802.jpg",
"/img_20260116_140902.jpg",
"/img_20260116_141003.jpg",
"/img_20260116_141103.jpg",
"/img_20260116_141203.jpg",
"/img_20260116_141303.jpg",
"/img_20260116_141403.jpg",
"/img_20260116_141504.jpg",
"/img_20260116_141604.jpg",
"/img_20260116_141704.jpg",
"/img_20260116_141804.jpg",
"/img_20260116_141904.jpg",
"/img_20260116_142005.jpg",
"/img_20260116_142105.jpg",
"/img_20260116_142205.jpg",
"/img_20260116_142305.jpg",
"/img_20260116_142406.jpg",
"/img_20260116_142506.jpg",
"/img_20260116_142606.jpg",
"/img_20260116_142707.jpg",
"/img_20260116_142807.jpg",
"/img_20260116_142907.jpg",
"/img_20260116_143007.jpg",
"/img_20260116_143107.jpg",
"/img_20260116_143208.jpg",
"/img_20260116_143308.jpg",
"/img_20260116_143408.jpg",
"/img_20260116_143508.jpg",
"/img_20260116_143608.jpg",
"/img_20260116_143709.jpg",
"/img_20260116_143809.jpg",
"/img_20260116_143909.jpg",
"/img_20260116_144009.jpg",
"/img_20260116_144110.jpg",
"/img_20260116_144210.jpg",
"/img_20260116_144310.jpg",
"/img_20260116_144410.jpg",
"/img_20260116_144511.jpg",
"/img_20260116_144611.jpg",
"/img_20260116_144711.jpg",
"/img_20260116_144811.jpg",
"/img_20260116_144911.jpg",
"/img_20260116_145012.jpg",
"/img_20260116_145112.jpg",
"/img_20260116_145212.jpg",
"/img_20260116_145313.jpg",
"/img_20260116_145413.jpg",
"/img_20260116_145513.jpg",
"/img_20260116_145614.jpg",
"/img_20260116_145714.jpg",
"/img_20260116_145814.jpg",
"/img_20260116_145915.jpg",
"/img_20260116_150015.jpg",
"/img_20260116_150115.jpg",
"/img_20260116_150215.jpg",
"/img_20260116_150315.jpg",
"/img_20260116_150416.jpg",
"/img_20260116_150516.jpg",
"/img_20260116_150616.jpg",
"/img_20260116_150716.jpg",
"/img_20260116_150817.jpg",
"/img_20260116_150917.jpg",
"/img_20260116_151017.jpg",
"/img_20260116_151117.jpg",
"/img_20260116_151218.jpg",
"/img_20260116_151318.jpg",
"/img_20260116_151418.jpg",
"/img_20260116_151518.jpg",
"/img_20260116_151618.jpg",
"/img_20260116_151719.jpg",
"/img_20260116_151819.jpg",
"/img_20260116_151919.jpg",
"/img_20260116_152019.jpg",
"/img_20260116_152120.jpg",
"/img_20260116_152220.jpg",
"/img_20260116_152320.jpg",
"/img_20260116_152421.jpg",
"/img_20260116_152521.jpg",
"/img_20260116_152621.jpg",
"/img_20260116_152722.jpg",
"/img_20260116_152822.jpg",
"/img_20260116_152922.jpg",
"/img_20260116_153022.jpg",
"/img_20260116_153123.jpg",
"/img_20260116_153223.jpg",
"/img_20260116_153323.jpg",
"/img_20260116_153424.jpg",
"/img_20260116_153524.jpg",
"/img_20260116_153624.jpg",
"/img_20260116_153724.jpg",
"/img_20260116_153825.jpg",
"/img_20260116_153925.jpg",
"/img_20260116_154025.jpg",
"/img_20260116_154125.jpg",
"/img_20260116_154226.jpg",
"/img_20260116_154326.jpg",
"/img_20260116_154426.jpg",
"/img_20260116_154527.jpg",
"/img_20260116_154627.jpg",
"/img_20260116_154727.jpg",
"/img_20260116_154827.jpg",
"/img_20260116_154927.jpg",
"/img_20260116_155028.jpg",
"/img_20260116_155128.jpg",
"/img_20260116_155228.jpg",
"/img_20260116_155328.jpg",
"/img_20260116_155429.jpg",
"/img_20260116_155529.jpg",
"/img_20260116_155629.jpg",
"/img_20260116_155730.jpg",
"/img_20260116_155830.jpg",
"/img_20260116_155930.jpg",
"/img_20260116_160030.jpg",
"/img_20260116_160130.jpg",
"/img_20260116_160231.jpg",
"/img_20260116_160331.jpg",
"/img_20260116_160431.jpg",
"/img_20260116_160532.jpg",
"/img_20260116_160632.jpg",
"/img_20260116_160732.jpg",
"/img_20260116_160832.jpg",
"/img_20260116_160933.jpg",
"/img_20260116_161034.jpg"
};
const int testFilesCount = sizeof(testFiles) / sizeof(testFiles[0]);
int currentTestFileIndex = 0; // Indeks aktualnie przetwarzanego pliku testowego

// ==========================================
// === KONFIGURACJA TRYBU PRACY ===
// ==========================================
// true  = ZAWSZE wysyłaj pełne zdjęcia (cyklicznie). Wyłącza algorytmy różnicowe.
// false = Pierwsze zdjęcie pełne, kolejne wysyłane jako różnice (Sparse JPEG).
const bool ONLY_FULL_IMAGES_MODE = false; 
// ==========================================
const int GOP_SIZE = 20;
int gop_frame_counter = 0;
// ==========================================
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
uint8_t jpeg_encode_quality = 90; 
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
const unsigned long CAPTURE_INTERVAL = 50000; // 15 sekund (w ms)
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
  // Jeśli używamy trybu SD, błąd kamery nie powinien blokować programu, ale ostrzegamy
  if (esp_camera_init(&cfg) != ESP_OK) { 
      Serial.println("[WARNING] Camera init failed (ok if in SD Test Mode)"); 
      if (!USE_SD_TEST_IMAGES) { while (true) delay(1000); }
  } 
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

void updateReferenceFile(const char* sourcePath, const char* destPath) {
  File source = SD.open(sourcePath, FILE_READ);
  if (!source) {
    Serial.printf("[SD ERROR] Nie mozna otworzyc zrodla: %s\n", sourcePath);
    return;
  }


  File dest = SD.open(destPath, FILE_WRITE);
  if (!dest) {
    Serial.printf("[SD ERROR] Nie mozna utworzyc pliku ref: %s\n", destPath);
    source.close();
    return;
  }

  uint8_t buf[512];
  while (source.available()) {
    size_t len = source.read(buf, 512);
    dest.write(buf, len);
  }

  source.close();
  dest.close();
  Serial.println("[SD] Zaktualizowano plik referencyjny (/reference.jpg)");
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
  if (!f) {
      Serial.printf("[ERROR] Cannot open file: %s\n", path);
      return;
  }
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
    Serial.println("[sendFullImage] Sukces.");
    
    // === OPTYMALIZACJA ===
    if (!ONLY_FULL_IMAGES_MODE) {
        Serial.println("[DEBUG] Ładowanie prev_gray dla przyszłych różnic...");
        if (prev_gray) { heap_caps_free(prev_gray); prev_gray = nullptr; }
        
        uint16_t w, h;
        prev_gray = decodeJpegToGrayscale(path, &w, &h); // Dekoduj to co wysłaliśmy jako bazę
        
        if (prev_gray) {
          first_press = false; // Przełącz na tryb Diff
          Serial.println("[DEBUG] Tryb Diff aktywny.");
        }
    } else {
        Serial.println("[DEBUG] Tryb FULL - pomijam ładowanie prev_gray.");
    }
    // =====================
    
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
  // --- LOGOWANIE ZMIENIONYCH BLOKÓW ---
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
 * @brief Główna funkcja wyzwalana automatycznie.
 * W trybie SD: Pobiera ścieżkę z tablicy -> Wysyła.
 * W trybie Kamery: Robi zdjęcie -> Zapisuje -> Wysyła.
 */
void captureAndProcessImage() {
  const char* pathToSend = nullptr;

  // ------------------------------------------------------
  // KROK 1: POZYSKANIE OBRAZU BIEŻĄCEGO (Zapis na SD)
  // ------------------------------------------------------
  if (USE_SD_TEST_IMAGES) {
    // --- TRYB TESTOWY (Z PLIKÓW) ---
    pathToSend = testFiles[currentTestFileIndex];
    
    if (!SD.exists(pathToSend)) {
       Serial.printf("[ERROR] Brak pliku testowego: %s\n", pathToSend);
       currentTestFileIndex = (currentTestFileIndex + 1) % testFilesCount;
       return;
    }
    // Inkrementacja licznika plików testowych
    currentTestFileIndex++;
    if (currentTestFileIndex >= testFilesCount) currentTestFileIndex = 0;

  } else {
    // --- TRYB KAMERY (ZDJĘCIE) ---
    Serial.println("\n[ACTION] Capture -> SD (Tryb Persistent).");

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("[CAMERA ERROR] Błąd pobierania klatki!");
      return;
    }

    capture_counter++;
    sprintf(current_filename_buffer, "/cap_%04d.jpg", capture_counter);

    bool saved = savePhotoToSD(fb, current_filename_buffer);
    esp_camera_fb_return(fb); 

    if (!saved) return;
    pathToSend = current_filename_buffer;
  }

  // ------------------------------------------------------
  // KROK 2: PRZYGOTOWANIE REFERENCJI
  // ------------------------------------------------------
  const char* REF_FILE = "/reference.jpg";

  // Czyścimy RAM (symulacja startu od zera dla każdej klatki)
  if (prev_gray) {
    heap_caps_free(prev_gray);
    prev_gray = nullptr;
  }

  bool ref_exists = SD.exists(REF_FILE);

  // ------------------------------------------------------
  // KROK 3: DECYZJA (TRYB FULL vs TRYB GOP)
  // ------------------------------------------------------
  
  if (ONLY_FULL_IMAGES_MODE) {
      // === SCENARIUSZ A: ZAWSZE PEŁNE ZDJĘCIA ===
      Serial.println("[MODE] ONLY_FULL_IMAGES_MODE = true. Wysyłam FULL.");
      sendFullImage(pathToSend);
      
      // W tym trybie resetujemy licznik GOP, żeby po przełączeniu trybu zaczął od 0 (Keyframe)
      gop_frame_counter = 0; 

  } else {
      // === SCENARIUSZ B: TRYB RÓŻNICOWY Z GOP ===
      
      // Sprawdzamy, czy to czas na Keyframe (reszta z dzielenia == 0)
      bool is_keyframe = (gop_frame_counter % GOP_SIZE == 0);

      Serial.printf("[GOP] Klatka %d w cyklu (GOP=%d). Keyframe: %s\n", 
                    gop_frame_counter, GOP_SIZE, is_keyframe ? "TAK" : "NIE");

      // Decyzja: Wysyłamy FULL jeśli:
      // 1. Wypada Keyframe z licznika
      // 2. LUB nie mamy pliku referencyjnego (np. pierwsze uruchomienie systemu)
      if (is_keyframe || !ref_exists) {
          
          if (!ref_exists) Serial.println("[GOP] Brak referencji na SD -> Wymuszam FULL.");
          else Serial.println("[GOP] Czas na odświeżenie (Keyframe) -> Wysyłam FULL.");
          
          sendFullImage(pathToSend);

      } else {
          // Wysyłamy DIFF (P-Frame)
          Serial.println("[GOP] Klatka pośrednia -> Próba wysłania DIFF.");
          
          // Musimy załadować poprzednie zdjęcie, żeby policzyć różnicę
          uint16_t w, h;
          prev_gray = decodeJpegToGrayscale(REF_FILE, &w, &h);
          
          if (prev_gray) {
              computeAndSendSparseJpegDiff(pathToSend);
          } else {
              Serial.println("[ERROR] Błąd dekodowania referencji! Fallback -> FULL.");
              sendFullImage(pathToSend);
          }
      }

      // Inkrementujemy licznik tylko w trybie GOP
      gop_frame_counter++;
      // Zabezpieczenie przed przepełnieniem inta (po wielu latach)
      if (gop_frame_counter > 30000) gop_frame_counter = 0;
  }

  // ------------------------------------------------------
  // KROK 4: ZAPIS STANU NA PRZYSZŁOŚĆ
  // ------------------------------------------------------
  // To co wysłaliśmy (pathToSend) staje się nową referencją
  updateReferenceFile(pathToSend, REF_FILE);

  // ------------------------------------------------------
  // KROK 5: SPRZĄTANIE
  // ------------------------------------------------------
  if (prev_gray) {
    heap_caps_free(prev_gray);
    prev_gray = nullptr;
  }
}

// ===============================================================
// === SETUP & LOOP ===
// ===============================================================

void setup() {
  Serial.begin(115200); delay(1000);
  Serial.println("Starting Node A (Camera Capture / Test Mode)...");

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

  // === CYKLICZNE WYKONYWANIE ZDJĘĆ / TESTÓW ===
  unsigned long now = millis();
  if (now - lastCaptureTime > CAPTURE_INTERVAL) {
    captureAndProcessImage();
    lastCaptureTime = now;
  }
}