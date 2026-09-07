# ESP32-CAM Bird Deterrence System

## Project Overview

This repository contains the firmware and trained Edge Impulse machine learning model for a low-power, AI-driven bird deterrent system. Designed to operate autonomously, the system follows a strict operational flow: PIR detects motion -> wake from deep sleep -> capture a photo -> run inference -> trigger the audio deterrent if a bird is found -> wait for the PIR to clear -> return to deep sleep. Deep sleep is utilized specifically to power down the camera sensor between triggers, preventing the hardware from overheating.

## Hardware Architecture & Wiring

* **Power System:** 18650 Lithium-ion battery pack managed by a solar charge controller, with an LM2596 buck converter dropping the voltage to a stable 5V rail for the components.
* **Camera:** AI-Thinker ESP32-CAM featuring the OV3660 camera sensor (utilizing the onboard ribbon connector, no external wiring needed).


* **PIR Motion Sensor:**
* VCC ➔ 5V (verify specific PIR supply voltage requirements).


* GND ➔ GND.


* OUT ➔ ESP32-CAM GPIO 13 (configured to wake the board from deep sleep).




* **Audio Output:** DY-HV20T module configured in UART mode (DIP switch settings: CON3=ON, CON2=OFF, CON1=OFF).


* *Note: Power the module separately per its 6-35V spec; do not power it directly from the ESP32*.


* ESP32 GPIO 14 ➔ Module TXD (IO0).


* ESP32 GPIO 15 ➔ Module RXD (IO1).


* ESP32 GND ➔ Module GND.





## Software Setup & Flashing

Before flashing the `.ino` sketch to your board, ensure the following configurations are set in the Arduino IDE:

1. **Memory & Partitioning:** Enable **PSRAM** (Tools ➔ PSRAM ➔ Enabled) and select a partition scheme with adequate application space, such as **Huge APP (3MB No OTA/1MB SPIFFS)**. Skipping this step is the most common reason Edge Impulse sketches fail to compile or crash upon boot.


2. **Edge Impulse Library:** Import the custom machine learning model via Sketch ➔ Include Library ➔ Add .ZIP Library. The sketch expects the library header to be named `Demo_1_inferencing.h`; update the `#include` directive in the code if your exported project name differs.


3. **Target Labeling:** Update the `TARGET_LABEL` definition in the code to match the exact class name used in your Edge Impulse model (e.g., "bird", "crow", "pigeon"). If this string does not match exactly, the deterrence logic will never execute.



## Edge Impulse Model & Failsafe Logic

* **Inference Pipeline:** When motion triggers the system, the camera flushes dummy frames to adjust auto-exposure, captures a live image, and runs classification against a 70% confidence threshold.
* **RTC Failsafe Timer:** To account for edge-case inference failures, the system tracks missed detections in RTC memory. If three motion events occur within a 60-second window without a positive model classification, the system automatically triggers the audio deterrent to ensure protection.

## Status Indicators (LED Language)

The system uses LEDs wired via a ~330Ω resistor to ground to communicate its current operational state:

* **GPIO 2 (LED_WAKE):** Indicates the system is awake and active. If solid after the main sequence, it is waiting for the PIR sensor to clear before sleeping.


* **GPIO 4 (LED_CAMERA):** Utilizes the onboard white flash LED to indicate a photo is being captured.


* **GPIO 12 (LED_INFERENCE):** Indicates the Edge Impulse neural network is processing the image.


* **GPIO 16 (LED_AUDIO):** Indicates a deterrent command is being transmitted to the HV20T.


* *Solid then off:* Deterrent was successfully triggered.


* *Double-blink:* Cycle finished, no bird detected, deterrent skipped.





**Error State:** A rapid 6x blink on any LED indicates that specific stage of the cycle failed.
