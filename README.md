# ESP32 Motion-Activated RGB LED Controller

An ESP32-based RGB LED strip controller with:
- PIR motion sensor wake-up
- Button-triggered web interface
- RGB + brightness control via web UI
- Deep sleep power saving
- OTA firmware updates

## Hardware
- ESP32
- WS2812B LED strip
- PIR motion sensor
- Push button
- Photoresistor

## Features
- Wakes from deep sleep using PIR or button
- Web-based RGB control
- Saves color settings in flash
- OTA updates supported

## Notes
- WiFi credentials handled via WiFiManager
- GPIO pins may need adjustment depending on board
