# MESFlow ESP32-S3 Kiosk v5.1.1

## LCD color correction

- The LCDWiki panel in use requires ILI9341 display inversion **ON**.
- Enforces `tft.invertDisplay(true)` after `begin()` and again after `setRotation()`.
- Fixes the exact complementary-color symptom: black→white, white→black, blue→orange/brown.
- Keeps the unified dark UI, UART scanner, demo QR catalog, Web Console, and kiosk logic unchanged.
