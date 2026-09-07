 ESP32-CAM BIRD DETERRENCE SYSTEM
  --------------------------------------------------------------------------
  PIR detects motion -> wake from deep sleep -> capture a photo -> run it
  through your Edge Impulse model -> if a bird is detected, tell the HV20T
  to play a random deterrent track -> wait for the PIR to clear -> back to
  deep sleep.

  Deep sleep is used specifically because it's the only mode that actually
  powers the camera down between triggers - that's what stops it overheating.


  ------------------------------------------------------------------
  BEFORE YOU FLASH THIS
  ------------------------------------------------------------------
  1. Set TARGET_LABEL below to match whatever your Edge Impulse model
     actually calls the "bird" class (check your EI project's labels -
     it might be "bird", "crow", "pigeon", etc, not necessarily "bird").
     If it doesn't match exactly, detection will just never fire - that's
     a safe default, not a crash, but you do need to fix it.
  2. Arduino IDE board settings: enable PSRAM (Tools -> PSRAM -> Enabled,
     or OPI PSRAM depending on your core version) and select a partition
     scheme with enough app space for the model (e.g. "Huge APP (3MB No
     OTA/1MB SPIFFS)"). Forgetting either of these is the most common
     reason EI camera sketches fail to build or crash on boot.
  3. Your exported Edge Impulse Arduino library must be installed
     (Sketch -> Include Library -> Add .ZIP Library). This sketch expects
     it to be called "Demo_1_inferencing.h" - change the #include below
     if your project has a different name.

  ------------------------------------------------------------------
  WIRING
  ------------------------------------------------------------------
    Camera: onboard ribbon connector, no wiring needed.

    PIR sensor:
      PIR VCC  ---->  5V (check your PIR's supply voltage requirement)
      PIR GND  ---->  GND
      PIR OUT  ---->  ESP32-CAM GPIO13   (also wakes it from deep sleep)

    HV20T audio module (UART mode, DIP: CON3=ON, CON2=OFF, CON1=OFF):
      ESP32-CAM GPIO14  ---->  DY-HV20T TXD (its IO0)   [ESP32 RX]
      ESP32-CAM GPIO15  ---->  DY-HV20T RXD (its IO1)   [ESP32 TX]
      ESP32-CAM GND     ---->  DY-HV20T GND
      Module powered separately per its spec (6-35V, NOT from ESP32).

    Status LEDs (each: GPIO -> resistor (~330R) -> LED -> GND):
      GPIO2   LED_WAKE       "system awake / active"
      GPIO4   LED_CAMERA     "capturing photo" - this is the ESP32-CAM's
                              BUILT-IN white flash LED, no wiring needed.
      GPIO12  LED_INFERENCE  "running the Edge Impulse model"
      GPIO16  LED_AUDIO      "sending deterrent command to HV20T"

  ------------------------------------------------------------------
  LED LANGUAGE
  ------------------------------------------------------------------
    Solid ON during a stage       -> that stage is running normally
    Rapid blink (6x)              -> that stage failed
    LED_AUDIO double-blink        -> cycle finished, no bird detected,
                                      deterrent NOT triggered
    LED_AUDIO solid then off      -> deterrent WAS triggered
    LED_WAKE solid after the      -> waiting for PIR to clear before
    main sequence                    going back to sleep (expected)

  ------------------------------------------------------------------
  PIN CAUTIONS
  ------------------------------------------------------------------
  GPIO2 and GPIO12 are ESP32 boot-strapping pins. A simple GPIO -> resistor
  -> LED -> GND circuit is safe on both since nothing external pulls them
  during boot. Just don't add a pull-up resistor to 3.3V on GPIO12.
