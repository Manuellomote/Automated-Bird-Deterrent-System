#include <Arduino.h>
#include <Demo_1_inferencing.h>              // <-- rename if your EI project differs
#include "edge-impulse-sdk/dsp/image/image.hpp"
#include "esp_camera.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_task_wdt.h" 
#include "mbedtls/base64.h"
#include <time.h>

SET_LOOP_TASK_STACK_SIZE(16 * 1024); // Expands loopTask stack for Edge Impulse


// ==========================================================================
//  Bird detection tuning - EDIT THESE to match your model
// ==========================================================================
#define TARGET_LABEL         "Bird"     // must match a label from your EI model
#define DETECTION_THRESHOLD  0.6f       // confidence (0.0-1.0) required to trigger

// ---------------- Camera pins (AI-Thinker ESP32-CAM) ----------------
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ---------------- Peripheral pins ----------------
#define PIR_PIN              13
#define AUDIO_RX_PIN         14   // ESP32 RX  <- module TXD
#define AUDIO_TX_PIN         15   // ESP32 TX  -> module RXD

#define LED_WAKE               2
#define LED_CAMERA              4   // onboard flash LED
#define LED_INFERENCE          12
#define LED_AUDIO               33


HardwareSerial audioSerial(2);

// ==========================================================================
//  Edge Impulse camera plumbing (from your exported example, adapted)
// ==========================================================================
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS   320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS   240
#define EI_CAMERA_FRAME_BYTE_SIZE           3

static bool is_initialised = false;
uint8_t *snapshot_buf = nullptr;

static camera_config_t camera_config = {
    .pin_pwdn = PWDN_GPIO_NUM,
    .pin_reset = RESET_GPIO_NUM,
    .pin_xclk = XCLK_GPIO_NUM,
    .pin_sscb_sda = SIOD_GPIO_NUM,
    .pin_sscb_scl = SIOC_GPIO_NUM,

    .pin_d7 = Y9_GPIO_NUM,
    .pin_d6 = Y8_GPIO_NUM,
    .pin_d5 = Y7_GPIO_NUM,
    .pin_d4 = Y6_GPIO_NUM,
    .pin_d3 = Y5_GPIO_NUM,
    .pin_d2 = Y4_GPIO_NUM,
    .pin_d1 = Y3_GPIO_NUM,
    .pin_d0 = Y2_GPIO_NUM,
    .pin_vsync = VSYNC_GPIO_NUM,
    .pin_href = HREF_GPIO_NUM,
    .pin_pclk = PCLK_GPIO_NUM,

    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,
    .frame_size = FRAMESIZE_QVGA,     // do not go above QVGA when not JPEG

    .jpeg_quality = 12,
    .fb_count = 1,
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY,
};

bool ei_camera_init(void) {
    if (is_initialised) return true;

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    // OV3660 Specific Fixes:
    s->set_vflip(s, 1);        // Flip image vertically (1 or 0 depending on mounting)
    s->set_hmirror(s, 1);      // Mirror image horizontally (1 or 0)
    
    // Enable Auto Exposure, Auto White Balance, and Auto Gain
    s->set_gain_ctrl(s, 1);     // Auto gain control ON
    s->set_exposure_ctrl(s, 1); // Auto exposure control ON
    s->set_awb_gain(s, 1);      // Auto white balance ON
    
    // Small contrast/brightness tuning for phone screens & bright light
    s->set_brightness(s, 0);   // Range: -2 to 2
    s->set_contrast(s, 1);     // Range: -2 to 2
}
    is_initialised = true;
    return true;
}

void ei_camera_deinit(void) {
    esp_err_t err = esp_camera_deinit();
    if (err != ESP_OK) return;
    is_initialised = false;
}


void printFrameAsBase64(camera_fb_t *fb) {
    if (!fb || !fb->buf || fb->len == 0) return;

    size_t out_len = 0;
    // Calculate required output buffer size
    mbedtls_base64_encode(NULL, 0, &out_len, fb->buf, fb->len);

    unsigned char *base64_buf = (unsigned char *)malloc(out_len + 1);
    if (!base64_buf) {
        Serial.println("[ERROR] Could not allocate PSRAM/RAM for Base64 string");
        return;
    }

    size_t encoded_bytes = 0;
    if (mbedtls_base64_encode(base64_buf, out_len, &encoded_bytes, fb->buf, fb->len) == 0) {
        base64_buf[encoded_bytes] = '\0';
        
        Serial.println("\n========== BASE64 IMAGE DUMP START ==========");
        Serial.print("data:image/jpeg;base64,");
        Serial.println((char *)base64_buf);
        Serial.println("========== BASE64 IMAGE DUMP END ============\n");
    } else {
        Serial.println("[ERROR] Base64 encoding failed");
    }

    free(base64_buf);
}


bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf) {
    bool do_resize = false;

    if (!is_initialised) return false;

    // Flush 15 dummy frames so OV3660 auto-exposure fully adjusts from deep sleep
    for (int i = 0; i < 15; i++) {
        camera_fb_t * dummy_fb = esp_camera_fb_get();
        if (dummy_fb) {
            esp_camera_fb_return(dummy_fb);
        }
        delay(40);
    }

    camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
        Serial.println("[ERROR] OV3660 capture failed!");
        return false;
    }

// --- SPIT OUT BASE64 IMAGE TO SERIAL MONITOR --- 
    //printFrameAsBase64(fb); 

    bool converted = fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, snapshot_buf);
    esp_camera_fb_return(fb);

    if (!converted) return false;

    if ((img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS)
        || (img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS)) {
        do_resize = true;
    }

    if (do_resize) {
        ei::image::processing::crop_and_interpolate_rgb888(
            out_buf,
            EI_CAMERA_RAW_FRAME_BUFFER_COLS,
            EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
            out_buf,
            img_width,
            img_height);
    }

    return true;
}

static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr) {
    size_t pixel_ix = offset * 3;
    size_t pixels_left = length;
    size_t out_ptr_ix = 0;

    while (pixels_left != 0) {
        // Pass standard RGB straight through
        out_ptr[out_ptr_ix] = (snapshot_buf[pixel_ix] << 16)
                             + (snapshot_buf[pixel_ix + 1] << 8)
                             +  snapshot_buf[pixel_ix + 2];
        out_ptr_ix++;
        pixel_ix += 3;
        pixels_left--;
    }
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif

// ==========================================================================
//  HV20T UART protocol (confirmed working)
//  Frame: [0xAA][CMD][LEN][data...][CHECKSUM]
// ==========================================================================
#define CMD_QUERY_PLAY_STATUS     0x01
#define CMD_PLAY                  0x02
#define CMD_SPECIFY_SONG          0x07
#define CMD_SWITCH_DRIVE          0x0B
#define CMD_QUERY_SONG_COUNT      0x0C
#define CMD_SET_VOLUME             0x13
#define DRIVE_SD                  0x01

// State that survives deep sleep - the HV20T stays powered continuously,
// so its drive/volume/track-count don't need re-sending every wake cycle.
RTC_DATA_ATTR bool     audioConfigured = false;
RTC_DATA_ATTR uint16_t totalTracks     = 0;


// ==========================================================================
//  Failsafe Motion Trigger Settings (Persists through Deep Sleep)
// ==========================================================================
#define FAILSAFE_TRIGGER_LIMIT   3    // Force audio after 3 consecutive missed detections
#define FAILSAFE_TIMEOUT_SEC     60   // 1 minute window in seconds

RTC_DATA_ATTR int motionTriggerCount = 0;     // Persists across deep sleep
RTC_DATA_ATTR time_t lastTriggerTimeSec = 0;  // Tracks wake timestamp in seconds across deep sleep


void sendCommand(uint8_t cmdType, const uint8_t *data, uint8_t dataLen) {
    uint8_t packet[16];
    uint8_t idx = 0;

    packet[idx++] = 0xAA;
    packet[idx++] = cmdType;
    packet[idx++] = dataLen;
    for (uint8_t i = 0; i < dataLen; i++) packet[idx++] = data[i];

    uint16_t sum = 0;
    for (uint8_t i = 0; i < idx; i++) sum += packet[i];
    packet[idx++] = (uint8_t)(sum & 0xFF);

    audioSerial.write(packet, idx);
}

void cmdSwitchDrive(uint8_t d) { sendCommand(CMD_SWITCH_DRIVE, &d, 1); }
void cmdSetVolume(uint8_t v)   { sendCommand(CMD_SET_VOLUME, &v, 1); }
void cmdQuerySongCount()       { sendCommand(CMD_QUERY_SONG_COUNT, nullptr, 0); }

void cmdPlaySpecificTrack(uint16_t trackNum) {
    uint8_t data[2];
    data[0] = (trackNum >> 8) & 0xFF;
    data[1] = trackNum & 0xFF;
    sendCommand(CMD_SPECIFY_SONG, data, 2);
}

void pollAudioResponses() {
    static uint8_t buf[16];
    static uint8_t idx = 0;
    static unsigned long lastByteTime = 0;

    while (audioSerial.available()) {
        uint8_t b = audioSerial.read();
        if (idx == 0 && b != 0xAA) continue;
        if (idx < sizeof(buf)) buf[idx++] = b;
        lastByteTime = millis();

        if (idx >= 3) {
            uint8_t dataLen  = buf[2];
            uint8_t totalLen = 3 + dataLen + 1;
            if (totalLen > sizeof(buf)) { idx = 0; continue; }
            if (idx >= totalLen) {
                uint16_t sum = 0;
                for (uint8_t i = 0; i < totalLen - 1; i++) sum += buf[i];
                if ((uint8_t)(sum & 0xFF) == buf[totalLen - 1]) {
                    if (buf[1] == CMD_QUERY_SONG_COUNT && dataLen >= 2) {
                        totalTracks = (buf[3] << 8) | buf[4];
                    }
                }
                idx = 0;
            }
        }
    }

    if (idx > 0 && millis() - lastByteTime > 200) idx = 0;
}

void waitWhilePolling(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) pollAudioResponses();
}

void ensureAudioReady() {
    if (audioConfigured) return;

    cmdSwitchDrive(DRIVE_SD);
    waitWhilePolling(300);

    cmdSetVolume(30);
    waitWhilePolling(300);

    cmdQuerySongCount();
    waitWhilePolling(300);

    audioConfigured = true;
}

void playRandomTrack() {
    digitalWrite(LED_AUDIO, LOW);

    ensureAudioReady();
    if (totalTracks == 0) {
        cmdQuerySongCount();
        waitWhilePolling(300);
    }

    if (totalTracks == 0) {
        digitalWrite(LED_AUDIO, HIGH);
        for (int i = 0; i < 6; i++) {
            
            digitalWrite(LED_AUDIO, LOW); delay(100);
            digitalWrite(LED_AUDIO, HIGH);  delay(100);
        }
        return;
    }

    uint16_t track = random(1, totalTracks + 1);
    cmdPlaySpecificTrack(track);
    waitWhilePolling(200);

    digitalWrite(LED_AUDIO, HIGH);
}

// ==========================================================================
//  LED helpers
// ==========================================================================
void blinkError(int pin) {
    for (int i = 0; i < 6; i++) {
        digitalWrite(pin, HIGH); delay(100);
        digitalWrite(pin, LOW);  delay(100);
    }
}

void blinkSkipped() {
    // ran fine, just no bird detected - short double-blink on LED_AUDIO
    digitalWrite(LED_AUDIO, LOW); delay(80);
    digitalWrite(LED_AUDIO, HIGH);  delay(80);
    digitalWrite(LED_AUDIO, LOW); delay(80);
    digitalWrite(LED_AUDIO, HIGH);
}

// ==========================================================================
//  Capture a photo and run it through the Edge Impulse model.
//  Returns true if TARGET_LABEL was detected above DETECTION_THRESHOLD.
// ==========================================================================
bool runInference() {
    bool birdDetected = false;

    // Allocate frame buffer in PSRAM
    snapshot_buf = (uint8_t *)ps_malloc(
        EI_CAMERA_RAW_FRAME_BUFFER_COLS * EI_CAMERA_RAW_FRAME_BUFFER_ROWS * EI_CAMERA_FRAME_BYTE_SIZE);

    if (snapshot_buf == nullptr) {
        Serial.println("[ERROR] Failed to allocate PSRAM for frame buffer!");
        //blinkError(LED_CAMERA);
        return false;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

   // digitalWrite(LED_CAMERA, HIGH);
    bool captured = ei_camera_capture((size_t)EI_CLASSIFIER_INPUT_WIDTH,
                                       (size_t)EI_CLASSIFIER_INPUT_HEIGHT,
                                       snapshot_buf);
   // digitalWrite(LED_CAMERA, LOW);

    if (!captured) {
        Serial.println("[ERROR] Camera capture failed!");
        free(snapshot_buf);
        snapshot_buf = nullptr;
        //blinkError(LED_CAMERA);
        return false;
    }

    Serial.println("[INFO] Running Edge Impulse classifier...");
    digitalWrite(LED_INFERENCE, HIGH);
    ei_impulse_result_t result = { 0 };

    // Execute neural network classification
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);

    digitalWrite(LED_INFERENCE, LOW);

    free(snapshot_buf);
    snapshot_buf = nullptr;

    if (err != EI_IMPULSE_OK) {
        Serial.printf("[ERROR] Classifier failed with code: %d\n", err);
        blinkError(LED_INFERENCE);
        return false;
    }

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) continue;
        if (strcmp(bb.label, TARGET_LABEL) == 0 && bb.value >= DETECTION_THRESHOLD) {
            birdDetected = true;
            break;
        }
    }
#else
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (strcmp(ei_classifier_inferencing_categories[i], TARGET_LABEL) == 0
            && result.classification[i].value >= DETECTION_THRESHOLD) {
            birdDetected = true;
            break;
        }
    }
#endif

    // Print target detection status to Serial Monitor
    if (birdDetected) {
        Serial.println(">>> INFERENCE RESULT: BIRD DETECTED! Triggering audio...");
    } else {
        Serial.println(">>> INFERENCE RESULT: NO BIRD DETECTED. Skipping deterrent.");
    }

    return birdDetected;
}

// ==========================================================================
//  Main trigger cycle: capture -> infer -> deter (if needed) -> done
// ==========================================================================
void runCycle() {
    digitalWrite(LED_WAKE, HIGH);

    // ------------------------------------------------------------------
    //  TIME-DELTA CHECK: Reset counter if >1 min has passed since last wake
    // ------------------------------------------------------------------
    time_t currentTimeSec = time(NULL);
    
    if (lastTriggerTimeSec > 0 && (currentTimeSec - lastTriggerTimeSec) > FAILSAFE_TIMEOUT_SEC) {
        Serial.println("[FAILSAFE] >1 min elapsed since last motion. Resetting trigger counter.");
        motionTriggerCount = 0;
    }
    
    // Store current timestamp for the next wake cycle
    lastTriggerTimeSec = currentTimeSec;

    if (!ei_camera_init()) {
        digitalWrite(LED_WAKE, LOW);
        return;
    }

    bool birdDetected = runInference();

    // Power down camera promptly to save battery
    ei_camera_deinit();

    if (birdDetected) {
        Serial.println("[INFO] Bird confirmed by EI model. Resetting failsafe counter.");
        motionTriggerCount = 0; // Reset counter on valid detection
        
        audioSerial.begin(9600, SERIAL_8N1, AUDIO_RX_PIN, AUDIO_TX_PIN);
        delay(50);
        playRandomTrack();
    } else {
        motionTriggerCount++; // Increment missed detection counter
        Serial.printf("[FAILSAFE] No bird detected. Motion trigger count: %d / %d\n", 
                      motionTriggerCount, FAILSAFE_TRIGGER_LIMIT);

        if (motionTriggerCount >= FAILSAFE_TRIGGER_LIMIT) {
            Serial.println("[FAILSAFE WARNING] Consecutive missed limit reached! Forcing audio deterrent...");
            motionTriggerCount = 0; // Reset counter after triggering failsafe
            
            audioSerial.begin(9600, SERIAL_8N1, AUDIO_RX_PIN, AUDIO_TX_PIN);
            delay(50);
            playRandomTrack();
        } else {
            blinkSkipped();
        }
    }

    digitalWrite(LED_WAKE, LOW);
}


void setup() {
    Serial.begin(115200);
if (!psramFound()) {
    Serial.println("CRITICAL: Hardware PSRAM not detected by ESP32!");
} else {
    Serial.printf("PSRAM Initialized successfully. Total size: %d bytes\n", ESP.getPsramSize());
}
// Subscribe the main thread to the Task Watchdog Timer
    esp_task_wdt_add(NULL);
    esp_task_wdt_delete(NULL);

    pinMode(PIR_PIN, INPUT);

    pinMode(LED_WAKE, OUTPUT);      digitalWrite(LED_WAKE, LOW);
    //pinMode(LED_CAMERA, OUTPUT);    digitalWrite(LED_CAMERA, LOW);
    pinMode(LED_INFERENCE, OUTPUT); digitalWrite(LED_INFERENCE, LOW);
    pinMode(LED_AUDIO, OUTPUT);     digitalWrite(LED_AUDIO, HIGH);


    randomSeed(esp_random());

    runCycle();

    // ext0 wakeup is level-triggered: if we sleep while PIR is still HIGH,
    // the ESP32 wakes instantly instead of actually sleeping. Wait here
    // (LED_WAKE on) until the PIR output has returned LOW.
    digitalWrite(LED_WAKE, HIGH);
    while (digitalRead(PIR_PIN) == HIGH) {
        delay(50);
    }
    digitalWrite(LED_WAKE, LOW);
    delay(200);   // brief settle time

    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIR_PIN, 1);   // wake on HIGH
    esp_deep_sleep_start();
}

void loop() {
    // never reached - deep sleep resets straight back into setup() on wake
}
