#include <WiFi.h>
#include <WebSocketsClient.h>
#include "driver/i2s.h"

// =====================
// Wi-Fi networks
// =====================
const char* WIFI_SSIDS[] = {
  "Leno",
  "DroiderV1",
  "SUSDNET"
};

const char* WIFI_PASSWORDS[] = {
  "LEENA2009",
  "LEENA2009",
  "ls1030"
};

const int WIFI_COUNT = 3;
const unsigned long WIFI_TRY_TIMEOUT_MS = 5000; // fast fallback

// =====================
// Render WebSocket server
// =====================
// Example: your-app.onrender.com
const char* WS_HOST = "YOUR_RENDER_HOSTNAME";
const uint16_t WS_PORT = 443;
const char* WS_PATH = "/ws?role=source";

// =====================
// INMP441 wiring
// =====================
#define I2S_BCLK   32
#define I2S_LRCLK   25
#define I2S_DIN     33

static const i2s_port_t I2S_PORT = I2S_NUM_0;

// Audio settings
const int SAMPLE_RATE = 16000;
const int FRAME_SAMPLES = 256;

WebSocketsClient webSocket;

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(true, true);
  delay(200);

  for (int i = 0; i < WIFI_COUNT; i++) {
    Serial.printf("\nTrying Wi-Fi %d: %s\n", i + 1, WIFI_SSIDS[i]);

    WiFi.begin(WIFI_SSIDS[i], WIFI_PASSWORDS[i]);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < WIFI_TRY_TIMEOUT_MS) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWi-Fi connected!");
      Serial.print("SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
      return;
    }

    Serial.println("Failed, moving to next network...");
    WiFi.disconnect(true, true);
    delay(100);
  }

  Serial.println("\nNo Wi-Fi networks available.");
}

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = FRAME_SAMPLES,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRCLK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_DIN
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install failed: %d\n", err);
    while (true) delay(1000);
  }

  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("i2s_set_pin failed: %d\n", err);
    while (true) delay(1000);
  }

  i2s_zero_dma_buffer(I2S_PORT);
}

void setupWebSocket() {
  webSocket.beginSSL(WS_HOST, WS_PORT, WS_PATH);

  // Easy first test setup. Later you can tighten security with certificate validation.
  webSocket.setInsecure();

  webSocket.setReconnectInterval(5000);

  webSocket.onEvent([](WStype_t type, uint8_t * payload, size_t length) {
    switch (type) {
      case WStype_CONNECTED:
        Serial.println("WebSocket connected");
        break;
      case WStype_DISCONNECTED:
        Serial.println("WebSocket disconnected");
        break;
      case WStype_TEXT:
        Serial.printf("WS text: %s\n", payload);
        break;
      default:
        break;
    }
  });
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  setupWiFi();
  setupI2S();
  setupWebSocket();
}

void loop() {
  webSocket.loop();

  static int32_t rawSamples[FRAME_SAMPLES];
  static int16_t pcmSamples[FRAME_SAMPLES];
  size_t bytesRead = 0;

  if (WiFi.status() == WL_CONNECTED && webSocket.isConnected()) {
    esp_err_t result = i2s_read(
      I2S_PORT,
      rawSamples,
      sizeof(rawSamples),
      &bytesRead,
      portMAX_DELAY
    );

    if (result == ESP_OK && bytesRead > 0) {
      const int samples = bytesRead / sizeof(int32_t);

      for (int i = 0; i < samples; i++) {
        pcmSamples[i] = (int16_t)(rawSamples[i] >> 16);
      }

      webSocket.sendBIN((uint8_t*)pcmSamples, samples * sizeof(int16_t));
    }
  } else {
    delay(10);
  }
}
