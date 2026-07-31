# Wokwi

Este proyecto está preparado para simular la XIAO ESP32-S3 con PlatformIO.

## Componentes simulados

- `board-xiao-esp32-s3` con 8 MB de flash.
- Dos tiras WS2812 de 24 píxeles conectadas a D0/GPIO1 y D1/GPIO2.
- LED de estado conectado a D2/GPIO3.
- Periférico GNSS NMEA personalizado en `chips/nmea-gps.chip.c`, conectado a D7/GPIO44.
- Wi-Fi virtual Wokwi-GUEST.

El simulador de tira es WS2812 de 24 bits; sirve para validar la señal RMT, el ritmo de actualización y los efectos, pero no sustituye la validación eléctrica de las tiras SK6812 RGBW reales.

## VS Code

1. Compilar el entorno `seeed_xiao_esp32s3` desde PlatformIO.
2. Abrir `diagram.json`.
3. Ejecutar `Wokwi: Request a new License` y activar la licencia desde el navegador.
4. Ejecutar `Wokwi: Start Simulator`.
5. Para abrir el portal HTTP en `http://localhost:8180`, activar el Wokwi Private IoT Gateway.

## CLI y prueba automática

El CLI necesita un token personal de Wokwi CI. Configúralo como variable de usuario, sin añadirlo al repositorio:

```powershell
[Environment]::SetEnvironmentVariable('WOKWI_CLI_TOKEN', '<TOKEN>', 'User')
```

Abre una terminal nueva y ejecuta:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e seeed_xiao_esp32s3
& "$env:USERPROFILE\.wokwi\bin\wokwi-cli.exe" . --scenario .\wokwi\boot.test.yaml --timeout 20000 --serial-log-file .\artifacts\wokwi-boot.log
```

La prueba espera el arranque del firmware, una posición GNSS válida y el estado AP activo.
