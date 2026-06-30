#include <WiFi.h>
#include <WebSocketsClient.h>
#include "driver/i2s.h"

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// Example: your-app.onrender.com
const char* WS_HOST = "YOUR_RENDER_HOSTNAME";
const uint16_t WS_PORT = 443;
const char* WS_PATH = "/ws?role=source";

#define I2S_BCLK   32
#define I2S_LRCLK   25
#define I2S_DIN     33

static const i2s_port_t I2S_PORT = I2S_NUM_0;

const int SAMPLE_RATE = 16000;
const int FRAME_SAMPLES = 256;

WebSocketsClient webSocket;

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Wi-Fi connected, IP: ");
  Serial.println(WiFi.localIP());
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

  // Easiest setup for a first test.
  // For stricter security later, replace this with certificate validation.
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
        // INMP441 data arrives in the high bits of the 32-bit word.
        // This converts it to signed 16-bit PCM for streaming.
        pcmSamples[i] = (int16_t)(rawSamples[i] >> 16);
      }

      webSocket.sendBIN((uint8_t*)pcmSamples, samples * sizeof(int16_t));
    }
  } else {
    delay(10);
  }
}
