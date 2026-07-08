/**
 * =========================================================================
 * SUBALI HOUSE - ESP32-CAM CONTROLLER (FIXED WITH FIREBASE)
 * =========================================================================
 * Fix: Camera capture error, button debounce, inverted image orientation,
 * integrated with Firebase RTDB to trigger the Main ESP32 Door Controller.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FirebaseESP32.h>

// =========================================================================
// KONFIGURASI
// =========================================================================
const char* WIFI_SSID       = "RIOZY";
const char* WIFI_PASSWORD   = "zazaly2005";
const char* SERVER_BASE_URL = "https://subali-house.openhostyee.web.id";
const char* DEVICE_ID       = "esp32cam-001";

// Firebase RTDB Configuration (samakan dengan esp32_main)
#define FIREBASE_HOST "https://smart-kos-8af31-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "FwZZH961DICw0gIVkIO5KYi9KkkMdO3Xi9PdlBDR"

FirebaseData firebaseData;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;
WiFiClientSecure secureClient;

// =========================================================================
// PIN
// =========================================================================
#define FLASH_LED_PIN    4

// =========================================================================
// KONFIGURASI KAMERA (AI-Thinker - FIX)
// =========================================================================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15  // <- GANTI dari 0 ke 15 (GPIO0 sering Bermasalah)
#define SIOD_GPIO_NUM    26
#define SIOC_GPIO_NUM    27
#define Y9_GPIO_NUM      35
#define Y8_GPIO_NUM      34
#define Y7_GPIO_NUM      39
#define Y6_GPIO_NUM      36
#define Y5_GPIO_NUM      21
#define Y4_GPIO_NUM      19
#define Y3_GPIO_NUM     18
#define Y2_GPIO_NUM      5
#define VSYNC_GPIO_NUM   25
#define HREF_GPIO_NUM   23
#define PCLK_GPIO_NUM    22

// =========================================================================
// STATE
// =========================================================================
bool     wifiOk        = false;
unsigned long lastCapture = 0;
const unsigned long CAPTURE_INTERVAL = 15000; // ms

// =========================================================================
// SETUP
// =========================================================================
void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== SUBALI HOUSE ESP32-CAM ==="));
  Serial.println(F("=============================="));

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  // INIT KAMERA
  Serial.println(F("\n[SETUP] Memanggil initCamera..."));
  initCamera();
  
  // CONNECT WIFI
  Serial.println(F("\n[SETUP] Memanggil connectWiFi..."));
  connectWiFi();
  
  if (wifiOk) {
    Serial.println(F("\n[SETUP] Menginisialisasi Firebase..."));
    setupFirebase();
    lastCapture = millis() - CAPTURE_INTERVAL;
  }

  Serial.println(F("\n[SYSTEM] SIAP!"));
  Serial.println(F("[SYSTEM] Menggunakan auto-capture, tanpa tombol."));
  Serial.println(F("[SYSTEM] Menangkap wajah setiap 15 detik."));
  Serial.println(F("=============================="));
}

// =========================================================================
// LOOP
// =========================================================================
void loop() {
  maintainWiFi();

  unsigned long now = millis();
  if (wifiOk && now - lastCapture >= CAPTURE_INTERVAL) {
    lastCapture = now;
    Serial.println(F("\n[AUTO] Memulai auto-capture wajah..."));
    captureAndVerifyFace();
  }

  delay(100);
}

// =========================================================================
// Inisialisasi Kamera - FIXED
// =========================================================================
void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // FIX: Pakai resolusi rendah dulu (CIF) untuk test
  config.frame_size   = FRAMESIZE_CIF;   // 400x296 - lebih stabil
  config.jpeg_quality = 12;
  config.fb_count     = 1;

  Serial.println(F("[CAM] Init dengan CIF (400x296)..."));

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init GAGAL: 0x%x (%d)\n", err, err);
    Serial.println(F("Status Kamera: Tidak aktif (Gagal Init)")); // ---> TAMBAHAN LOG
    
    // Coba lagi dengan konfigurasi default
    Serial.println(F("[CAM] Coba lagi dengan default..."));
    config.frame_size = FRAMESIZE_SVGA;
    config.fb_count = 1;
    err = esp_camera_init(&config);
    if (err != ESP_OK) {
      Serial.printf("[CAM] Init GAGAL LAGI: 0x%x\n", err);
      Serial.println(F("[CAM] CEK:"));
      Serial.println(F("[CAM] 1. Kamera tercolok dengan benar?"));
      Serial.println(F("[CAM] 2. Kabel kamera tidak longgar?"));
      Serial.println(F("[CAM] 3. Coba reset ESP32-CAM"));
      return;
    }
  }

  Serial.println(F("[CAM] Kamera INIT BERHASIL!"));
  Serial.println(F("Status Kamera: Kamera aktif nyala")); // ---> TAMBAHAN LOG

  // Sensor settings
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_gain_ctrl(s, 1);

    // Perbaikan posisi kamera jika dipasang terbalik (port micro-usb di bawah)
    s->set_vflip(s, 1);    // Aktifkan vertical flip
    s->set_hmirror(s, 1);  // Aktifkan horizontal mirror
  }

  Serial.println(F("[CAM] Sensor dikonfigurasi."));
}

// =========================================================================
// WiFi
// =========================================================================
void connectWiFi() {
  Serial.printf("[WiFi] Connecting: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiOk = true;
    secureClient.setInsecure();
    Serial.printf("\n[WiFi] TERHUBUNG! IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WiFi] Signal (RSSI): %d dBm\n", WiFi.RSSI());
  } else {
    Serial.println(F("\n[WiFi] GAGAL!"));
    wifiOk = false;
  }
}

void maintainWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiOk) {
      wifiOk = false;
      Serial.println(F("[WiFi] Terputus, reconnect..."));
    }
    WiFi.reconnect();
    delay(1000);
  } else if (!wifiOk) {
    wifiOk = true;
    Serial.println(F("[WiFi] Kembali terhubung"));
  }
}

// =========================================================================
// Setup Firebase
// =========================================================================
void setupFirebase() {
  fbConfig.host = FIREBASE_HOST;
  fbConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);
  Serial.println(F("[FIREBASE] Inisialisasi Berhasil"));
}

// =========================================================================
// CAPTURE & VERIFIKASI
// =========================================================================
void captureAndVerifyFace() {
  if (!wifiOk) {
    Serial.println(F("[ERROR] WiFi tidak terhubung!"));
    return;
  }

  // Nyalakan flash sebentar
  Serial.println(F("[CAM] Flash ON..."));
  digitalWrite(FLASH_LED_PIN, HIGH);
  delay(200);

  // Ambil frame
  Serial.println(F("[CAM] Mengambil foto..."));
  camera_fb_t* fb = esp_camera_fb_get();
  digitalWrite(FLASH_LED_PIN, LOW);

  if (!fb) {
    Serial.println(F("[ERROR] GAGAL AMBIL FOTO!"));
    Serial.println(F("Status Kamera: Tidak aktif / error")); // ---> TAMBAHAN LOG
    Serial.println(F("[ERROR] CEK:"));
    Serial.println(F("[ERROR] 1. Kamera OV2640 tercolok?"));
    Serial.println(F("[ERROR] 2. Kabel kamera bagus?"));
    return;
  }
  
  Serial.println(F("Status Kamera: Kamera aktif nyala")); // ---> TAMBAHAN LOG
  Serial.printf("[CAM] FOTO BERHASIL! Size: %u bytes\n", fb->len);
  Serial.printf("[CAM] Format: %d, Width: %d, Height: %d\n", 
                fb->format, fb->width, fb->height);

  // Encode ke Base64
  String b64Image = encodeBase64(fb->buf, fb->len);
  esp_camera_fb_return(fb);

  if (b64Image.isEmpty()) {
    Serial.println(F("[ERROR] Encoding gagal!"));
    return;
  }

  Serial.printf("[B64] Base64: %d chars\n", b64Image.length());

  // Kirim ke server
  sendFaceToServer(b64Image);
}

// =========================================================================
// Base64 Encode
// =========================================================================
String encodeBase64(const uint8_t* data, size_t len) {
  const char* b64chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  String result = "";
  result.reserve((len / 3 + 1) * 4 + 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t group = (uint32_t)data[i] << 16;
    if (i + 1 < len) group |= (uint32_t)data[i + 1] << 8;
    if (i + 2 < len) group |= data[i + 2];

    result += b64chars[(group >> 18) & 0x3F];
    result += b64chars[(group >> 12) & 0x3F];
    result += (i + 1 < len) ? b64chars[(group >> 6) & 0x3F] : '=';
    result += (i + 2 < len) ? b64chars[group & 0x3F]        : '=';
  }
  return result;
}

// =========================================================================
// Kirim ke Server
// =========================================================================
void sendFaceToServer(String& b64Image) {
  if (!wifiOk) {
    Serial.println(F("[ERROR] Tidak dapat kirim; WiFi tidak terhubung."));
    return;
  }

  HTTPClient http;
  String url = String(SERVER_BASE_URL) + "/api/iot/verify-face";

  Serial.printf("[HTTP] POST: %s\n", url.c_str());

  if (!http.begin(secureClient, url)) {
    Serial.println(F("[HTTP] Gagal mulai koneksi HTTPS"));
    return;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.setTimeout(15000);

  String body = "{\"image\":\"data:image/jpeg;base64,";
  body += b64Image;
  body += "\",\"device_id\":\"";
  body += DEVICE_ID;
  body += "\"}";

  Serial.printf("[HTTP] Body: %d bytes\n", body.length());

  int code = http.POST(body);
  Serial.printf("[HTTP] Response: %d\n", code);

  if (code == 200) {
    String response = http.getString();
    Serial.printf("[HTTP] Response: %s\n", response.c_str());

    StaticJsonDocument<512> res;
    DeserializationError err = deserializeJson(res, response);

    if (!err) {
      bool success = res["success"] | false;
      bool found   = res["found"]   | false;
      const char* name = res["tenant_name"] | "";
      const char* room = res["room_number"] | "";
      const char* msg  = res["message"]     | "";

      if (success && found) {
        Serial.printf("[OK] Wajah: %s, Kamar: %s\n", name, room);
        Serial.println(F("akses di terima")); // ---> TAMBAHAN LOG
        callEsp32MainOpen(String(room), String(name));
      } else {
        Serial.printf("[DENIED] %s\n", msg);
        Serial.println(F("akses di tolak")); // ---> TAMBAHAN LOG
      }
    } else {
      Serial.printf("[ERROR] JSON parse: %s\n", err.c_str());
    }
  } else {
    Serial.printf("[ERROR] HTTP: %d - %s\n", code, http.errorToString(code).c_str());
  }

  http.end();
}

// =========================================================================
// Panggil ESP32 Main (Lewat Firebase RTDB)
// =========================================================================
void callEsp32MainOpen(String room, String name) {
  if (room.isEmpty()) {
    Serial.println(F("[DOOR] Room kosong!"));
    return;
  }

  Serial.println(F("[FIREBASE] Mengirim trigger buka pintu..."));
  
  // Buat JSON data trigger
  FirebaseJson json;
  json.set("room", room);
  json.set("name", name);
  json.set("timestamp", millis()); // Penanda waktu unik

  // Push ke path "/triggers" di Realtime Database
  if (Firebase.pushJSON(firebaseData, "/triggers", json)) {
    Serial.printf("[FIREBASE] Trigger terkirim ke path: %s\n", firebaseData.pathAvailable().c_str());
  } else {
    Serial.printf("[FIREBASE] Gagal kirim! Alasan: %s\n", firebaseData.errorReason().c_str());
  }
}
