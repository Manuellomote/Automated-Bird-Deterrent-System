## Introduction

This repository contains the firmware and trained Edge Impulse machine learning model for a low-power, AI-driven bird deterrent system. Designed to operate autonomously, the system remains in deep sleep to conserve power until motion is detected. Upon waking, the ESP32-CAM captures an image, processes it through an onboard neural network to verify the presence of a bird, and triggers a high-decibel audio deterrent if a positive match is found.

## Hardware Architecture

* **Microcontroller:** AI-Thinker ESP32-CAM featuring the OV3660 camera sensor for capturing environmental image data.
* **Sensing:** Standard PIR motion sensor configured to wake the ESP32 from deep sleep via GPIO 13.
* **Audio Output:** DY-HV20T audio playback module, communicating via UART to play targeted deterrent tracks stored on an SD card.
* **Power System:** 18650 Lithium-ion battery pack managed by a solar charge controller, with an LM2596 buck converter dropping the voltage to a stable 5V rail for the components.

## Software & Installation

* Install the ESP32 board package within your Arduino IDE board manager.
* Enable **PSRAM** in the Arduino IDE Tools menu, as the camera buffer requires it to process image data.
* Import the custom machine learning model by navigating to **Sketch > Include Library > Add .ZIP Library** and selecting the included Edge Impulse `.zip` file.
* Flash the main `.ino` sketch to the ESP32-CAM using an FTDI programmer or dedicated flashing board.

## Edge Impulse Model & Logic

* **Custom Neural Network:** The repository includes a trained Edge Impulse model optimized for local bird detection, designed to run entirely on the edge without internet connectivity.
* **Inference Pipeline:** When motion triggers the system, the camera flushes dummy frames to adjust auto-exposure, captures a live image, and runs classification against a 70% confidence threshold.
* **RTC Failsafe Timer:** To account for edge-case inference failures, the system tracks missed detections in RTC memory. If three motion events occur within a 60-second window without a positive model classification, the system automatically triggers the audio deterrent.

Would you like to include an open-source license, such as MIT or GPL, to the bottom of the repository?
