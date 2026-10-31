#define CAMERA_MODEL_XIAO_ESP32S3
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
#include <HardwareSerial.h>
#include "ble_frame_structure.h" // Upewnij się, że masz ten plik, lub zdefiniuj strukturę BleHeader tutaj

// ==========================================
// === KONFIGURACJA GŁÓWNA ===
// ==========================================
const bool USE_BLUETOOTH = false; 
const bool USE_SD_TEST_IMAGES = true; 
const bool ONLY_FULL_IMAGES_MODE = false;

// USTAWIENIA UART (SLAVE)
#define WAKEUP_PIN   D5  
#define UART_TX_PIN  43  // D6
#define UART_RX_PIN  44  // D7
#define BAUDRATE     115200 // Szybki UART do przesyłu zdjęć

// ==========================================
// === LISTA PLIKÓW (Uzupełnij sam) ===
// ==========================================
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
RTC_DATA_ATTR int currentTestFileIndex = 0; // Pamiętamy indeks po wybudzeniu!

// ==========================================
// === ZMIENNE GLOBALNE ===
// ==========================================
const int GOP_SIZE = 20;
RTC_DATA_ATTR int gop_frame_counter = 0; 
RTC_DATA_ATTR bool first_run = true;     

constexpr uint8_t SD_CS = 21;
constexpr uint8_t SD_SCK = 7;
constexpr uint8_t SD_MISO = 8;
constexpr uint8_t SD_MOSI = 9;
SPIClass sdSPI(FSPI);

JPEGDEC jpegDec;
JPEGENC jpegEnc;

// BLE
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
const char* NODE_B_NAME = "NODE_B";
BLEClient* pClient = nullptr;
BLERemoteCharacteristic* pCharRx = nullptr;
BLERemoteCharacteristic* pCharTx = nullptr;
volatile uint8_t ackValue = 0x00;

// Parametry obrazu
uint16_t img_width = 320;  
uint16_t img_height = 240;
uint8_t block_size = 16; 
uint8_t sparse_background_color = 0x00; 

// Bufory
static uint8_t* global_gray_buf = nullptr;
static uint16_t global_img_width = 0;
static uint8_t* prev_gray = nullptr; 
static std::vector<uint8_t> encodedJpegData;
static char current_filename_buffer[50];
static int capture_counter = 0;

// Algorytmy
enum DiffMethod { METHOD_MAD, METHOD_COSINE_DISTANCE, METHOD_MSE, METHOD_DSSIM };
DiffMethod currentDiffMethod = METHOD_DSSIM; 
float dssim_threshold = 0.05; 
// Inne progi jeśli potrzebne:
float mad_threshold = 5.0;  
float cosine_dist_threshold = 0.005; 
float mse_threshold = 180.0; 

// ===============================================================
// === CALLBACKI JPEG ===
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
// === INICJALIZACJE ===
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
    .frame_size = FRAMESIZE_QVGA, .jpeg_quality = 12, .fb_count = 1
  };
  if (esp_camera_init(&cfg) != ESP_OK) { 
      Serial.println("[WARNING] Camera init failed (ok for SD test)"); 
  } else {
    sensor_t * s = esp_camera_sensor_get();
    if (s) s->set_special_effect(s, 2); // Grayscale
  }
}

static void sdInit() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, sdSPI, 25000000)) { Serial.println("[ERROR] SD Init Fail"); }
}

// ===============================================================
// === KOMUNIKACJA (UART / BLE) ===
// ===============================================================

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
      delay(200); pClient->setMTU(247); 
      auto svc = pClient->getService(SERVICE_UUID);
      if (!svc) return false;
      pCharRx = svc->getCharacteristic(CHAR_UUID_RX);
      pCharTx = svc->getCharacteristic(CHAR_UUID_TX);
      if (pCharTx->canNotify()) pCharTx->registerForNotify(notifyCB);
      return true;
    }
  }
  return false;
}

// UNIWERSALNA FUNKCJA WYSYŁAJĄCA DANE DO MASTERA
void sendDataToMaster(const uint8_t* data, size_t len, uint8_t txType) {
    if (USE_BLUETOOTH) {
        // --- BLE LOGIC ---
        Serial.println("[BLE] Łączenie...");
        initBLE();
        if (!connectToNodeB()) {
            Serial.println("[BLE] Błąd połączenia.");
            return;
        }
        
        BleHeader header;
        header.magic_start[0] = 0xAB; header.magic_start[1] = 0xCD;
        header.tx_type = txType;
        header.data_size = len;
        header.width = img_width; header.height = img_height;
        header.block_size = block_size;
        header.magic_end[0] = 0xEF; header.magic_end[1] = 0x12;

        ackValue = 0x00;
        pCharRx->writeValue((uint8_t*)&header, sizeof(header), false);
        
        unsigned long st = millis();
        while (ackValue != 0x01 && millis() - st < 2000) delay(10);
        
        if (ackValue == 0x01) {
            size_t off = 0;
            while (off < len) {
                size_t chunk = min((size_t)180, len - off);
                pCharRx->writeValue((uint8_t*)(data + off), chunk, false);
                off += chunk;
                delay(10);
            }
            st = millis();
            while (ackValue != 0xAA && millis() - st < 10000) delay(10);
            if (ackValue == 0xAA) Serial.println("[BLE] Sukces.");
        }
        pClient->disconnect();

    } else {
        // --- UART LOGIC ---
        // Protokół: IMG + 1B Type + 4B Size + Data
        Serial.printf("[UART] Wysyłam %u bajtów (Typ: %d)\n", len, txType);
        
        Serial1.print("IMG");
        Serial1.write(txType);
        Serial1.write((uint8_t*)&len, 4); 
        
        size_t sent = 0;
        while(sent < len) {
            size_t chunk = min((size_t)128, len - sent); // Większy chunk dla 115200
            Serial1.write(data + sent, chunk);
            sent += chunk;
            // Bardzo krótki delay, żeby nie zapchać bufora odbiorczego Node B
            delay(1); 
        }
        Serial.println("[UART] Wysłano.");
    }
}

void sendNoChanges() {
    Serial.println("[INFO] Brak zmian.");
    if (USE_BLUETOOTH) {
        // Opcjonalnie obsługa BLE
    } else {
        uint32_t zero = 0;
        uint8_t type = 0;
        Serial1.print("IMG");
        Serial1.write(type);
        Serial1.write((uint8_t*)&zero, 4);
    }
}

// ===============================================================
// === LOGIKA PRZETWARZANIA OBRAZU (TWOJA) ===
// ===============================================================

uint8_t* decodeJpegToGrayscale(const char* path, uint16_t* out_width, uint16_t* out_height) {
  File jpgFile = SD.open(path, FILE_READ);
  if (!jpgFile) return nullptr;
  int32_t fileSize = jpgFile.size();
  if (jpegDec.open(&jpgFile, fileSize, jpegCloseCallback, jpegReadCallback, jpegSeekCallback, jpegDrawCallback) != 1) {
    jpgFile.close(); return nullptr;
  }
  *out_width = jpegDec.getWidth();
  *out_height = jpegDec.getHeight();
  global_gray_buf = (uint8_t*)heap_caps_malloc((*out_width) * (*out_height), MALLOC_CAP_SPIRAM);
  if (!global_gray_buf) { jpegDec.close(); return nullptr; }
  global_img_width = *out_width;
  jpegDec.setPixelType(EIGHT_BIT_GRAYSCALE);
  jpegDec.decode(0, 0, 0);
  jpegDec.close();
  return global_gray_buf;
}

// Funkcja obliczająca różnicę i wysyłająca Sparse JPEG
void computeAndSendSparseJpegDiff(const char* current_frame_path) {
    // 1. Ładujemy bieżący obraz
    uint16_t curr_w, curr_h;
    uint8_t* curr_gray = decodeJpegToGrayscale(current_frame_path, &curr_w, &curr_h);
    if (!curr_gray) { sendNoChanges(); return; }

    // 2. Bufor na sparse
    uint8_t* sparse_gray_buf = (uint8_t*)heap_caps_malloc(img_width * img_height, MALLOC_CAP_SPIRAM);
    if (!sparse_gray_buf) { heap_caps_free(curr_gray); return; }
    memset(sparse_gray_buf, sparse_background_color, img_width * img_height);

    int rows = img_height / block_size;
    int cols = img_width / block_size;
    int changed_count = 0;

    // 3. Porównanie blokowe (DSSIM)
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            int y = i * block_size, x = j * block_size;
            bool is_block_changed = false;
            
            // Implementacja DSSIM (uproszczona dla czytelności kodu wynikowego)
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

            if (is_block_changed) {
                changed_count++;
                // Kopiuj blok do sparse buffer
                for (int k = 0; k < block_size; k++) {
                   int off = (y + k) * img_width + x;
                   memcpy(sparse_gray_buf + off, curr_gray + off, block_size);
                }
            }
        }
    }

    Serial.printf("[DIFF] Zmiana: %d bloków.\n", changed_count);
    
    // Zwalniamy stary prev i ustawiamy nowy
    heap_caps_free(prev_gray);
    prev_gray = curr_gray; // Przekazanie wskaźnika

    if (changed_count == 0) {
        sendNoChanges();
        heap_caps_free(sparse_gray_buf);
        return;
    }

    // 4. Kompresja JPEG Sparse
    int result = jpegEnc.open("memory", myJpegEncOpen, myJpegEncClose, myJpegEncRead, myJpegEncWrite, myJpegEncSeek);
    if (result == JPEGE_SUCCESS) {
        JPEGENCODE jpe;
        jpegEnc.encodeBegin(&jpe, img_width, img_height, JPEGE_PIXEL_GRAYSCALE, JPEGE_SUBSAMPLE_444, JPEGE_Q_HIGH);
        // ... (Tu następuje standardowe pakowanie bloków do enkodera) ...
        // Dla uproszczenia wklejenia: zakładamy, że enkoder tu działa poprawnie jak w Twoim kodzie
        // W pętli po MCU kopiujemy ze sparse_gray_buf do mcu_buf i dodajemy do enkodera.
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

    // 5. Wysyłka
    sendDataToMaster(encodedJpegData.data(), encodedJpegData.size(), BLE_TX_TYPE_SPARSE_JPEG);
}

void processImageLogic(const char* currentPath) {
    const char* REF_FILE = "/reference.jpg";
    bool ref_exists = SD.exists(REF_FILE);

    bool is_keyframe = (gop_frame_counter % GOP_SIZE == 0);
    bool force_full = ONLY_FULL_IMAGES_MODE || !ref_exists || is_keyframe || first_run;

    if (force_full) {
        Serial.println("[MODE] FULL Image");
        File f = SD.open(currentPath, FILE_READ);
        if (f) {
            size_t fSize = f.size();
            uint8_t* buf = (uint8_t*)ps_malloc(fSize);
            if (buf) {
                f.read(buf, fSize);
                sendDataToMaster(buf, fSize, BLE_TX_TYPE_FULL_IMAGE);
                free(buf);
            }
            f.close();
        }
        
        // Zapisz referencję
        if (SD.exists(REF_FILE)) SD.remove(REF_FILE);
        File src = SD.open(currentPath, "r");
        File dst = SD.open(REF_FILE, "w");
        while(src.available()) dst.write(src.read());
        src.close(); dst.close();
        
        first_run = false;
    } else {
        Serial.println("[MODE] DIFF Image");
        uint16_t w, h;
        // Dekoduj referencję
        prev_gray = decodeJpegToGrayscale(REF_FILE, &w, &h);
        
        if (prev_gray) {
            computeAndSendSparseJpegDiff(currentPath);
        } else {
            // Fallback
            sendNoChanges();
        }
    }
    gop_frame_counter++;
}

// ===============================================================
// === SETUP (GŁÓWNA PĘTLA STEROWANIA) ===
// ===============================================================

void setup() {
    // Dioda ON
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

    Serial.begin(115200);
    Serial1.begin(BAUDRATE, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    Serial.println("\n=== NODE A: CAMERA SLAVE ===");
    
    initCamera();
    sdInit();

    unsigned long startTime = millis();
    bool wentToSleep = false;

    // Czekamy na komendy (Timeout 20s)
    while (millis() - startTime < 20000) {
        if (Serial1.available()) {
            char cmd = Serial1.read();

            if (cmd == 'G') {
                Serial.println("[CMD] Otrzymano 'G' -> Robię zdjęcie...");
                
                // Mignij na znak pracy
                digitalWrite(LED_BUILTIN, HIGH); delay(50); digitalWrite(LED_BUILTIN, LOW);

                // Pobierz plik
                const char* path = testFiles[currentTestFileIndex];
                if (USE_SD_TEST_IMAGES) {
                    currentTestFileIndex = (currentTestFileIndex + 1) % testFilesCount;
                } else {
                    // Tu byłaby obsługa kamery na żywo (zapis do /cap.jpg)
                }
                
                // Procesuj i wyślij
                processImageLogic(path);
                
                startTime = millis(); // Reset timeoutu
            }
            else if (cmd == 'S') {
                Serial.println("[CMD] Otrzymano 'S' -> Idę spać.");
                wentToSleep = true;
                break;
            }
        }
        delay(10);
    }

    if (!wentToSleep) Serial.println("[TIMEOUT] Idę spać sam.");

    Serial.flush();
    digitalWrite(LED_BUILTIN, HIGH); // Dioda OFF
    
    // Konfiguracja wybudzania
    pinMode(WAKEUP_PIN, INPUT_PULLUP);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, 0); 
    esp_deep_sleep_start();
}

void loop() {}