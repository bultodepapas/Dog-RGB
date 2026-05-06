# AP Basic Test

Minimal ESP32-S3 SoftAP diagnostic firmware for Dog-RGB.

This project is intentionally separate from `Platformio/Dog-RGB` and does not
initialize LEDs, GPS, BLE, NVS, captive DNS, or the production portal. It only
starts a SoftAP and a tiny HTTP server so AP behavior can be tested in isolation.

## AP settings

- SSID: `DogRGB-APTEST`
- Password: `Dog12345`
- URL: `http://192.168.4.1/`
- Channel: `1`
- Max clients: `1`

## Build

```powershell
cd Platformio/AP-Basic-Test
C:\Users\bulto\.platformio\penv\Scripts\pio.exe run -e seeed_xiao_esp32s3
```

## Upload

```powershell
cd Platformio/AP-Basic-Test
C:\Users\bulto\.platformio\penv\Scripts\pio.exe run -e seeed_xiao_esp32s3 -t upload
```

## Monitor

```powershell
cd Platformio/AP-Basic-Test
C:\Users\bulto\.platformio\penv\Scripts\pio.exe device monitor -e seeed_xiao_esp32s3
```
