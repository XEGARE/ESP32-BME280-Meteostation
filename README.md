# ESP32 BME280 Meteostation

English | [Русский](README_RU.md)

Weather station for **ESP32-WROOM-32** with an I2C BME280 sensor and a web interface displaying temperature and humidity.

<img width="825" height="507" alt="1790417222" src="https://github.com/user-attachments/assets/18d02ebb-dc3e-48c8-a252-49ab7b6be09c" />

## Features

- Temperature and humidity on the main page, refreshed every two seconds.
- Wi-Fi and sea-level pressure settings through a web interface.
- Setup access point and captive portal for Android, Apple and Windows devices.
- English and Russian interface; language stored on the ESP32 and used by the API.
- Wi-Fi credentials, reference pressure and language retained after power loss.
- LED connection and sensor status indication.
- Automatic Wi-Fi reconnection and sensor recovery.
- Localized `/get` message and numeric readings for integrations, including Alice and “Домовёнок Кузя”.

## Wiring and installation

Use an **ESP32-WROOM-32** board and an **I2C BME280**. BMP280 does not measure humidity and is not supported.

| Signal | ESP32-WROOM-32 |
| --- | --- |
| BME280 SDA | GPIO21 |
| BME280 SCL | GPIO22 |
| BME280 VCC | 3.3 V |
| BME280 GND | GND |
| Status LED | GPIO2, active HIGH |

Check your board pinout. Adjust `BME_SDA_PIN`, `BME_SCL_PIN`, `STATUS_LED_PIN`, `STATUS_LED_ON` and `STATUS_LED_OFF` near the beginning of the sketch as needed. GPIO2 assumes a board with an active HIGH status LED on that pin. Sensor addresses `0x76` and `0x77` are detected automatically.

1. Install **esp32 by Espressif Systems** through Arduino IDE Boards Manager.
2. Install **Adafruit BME280 Library** through Library Manager; dependencies are installed automatically.
3. Open [ESP32_BME280_Meteostation.ino](ESP32_BME280_Meteostation/ESP32_BME280_Meteostation.ino).
4. Select **ESP32 Dev Module** and your board's port, then upload.
5. Open Serial Monitor at **115200 baud** to see the device IP and diagnostics.

Wi-Fi, DNS, EEPROM and Wire are included in the ESP32 Arduino core. ESPping is not needed.

## First setup

Connect to the setup network after first boot:

```text
SSID: ESP32-BME280-Meteostation
Password: password123
```

Defaults are defined by `AP_SSID` and `AP_PASSWORD` in the sketch. Captive portal opens the settings page; if it does not open, visit [http://192.168.4.1](http://192.168.4.1).

<img width="825" height="733" alt="1790417244" src="https://github.com/user-attachments/assets/8cb19820-d1a1-4249-b91f-3569ef761d5e" />

Choose EN or RU, enter your Wi-Fi SSID/password and sea-level reference pressure, then select **Save and restart**. Empty password supports an open network. SSID limit: 32 UTF-8 bytes. WPA passwords: 8–63 bytes or a 64-character hexadecimal key.

After restart, reconnect to your home Wi-Fi and open the ESP32 IP from Serial Monitor or your router. The main page displays temperature in °C and relative humidity in %. The gear icon opens `/settings`.

## Pressure and language

Sea-level pressure defaults to **1020.00 hPa**; accepted range **800–1200 hPa**. Enter the current reference pressure for your location. This setting calculates `altitude_m` in `/get` and `/status`; it does not replace actual measured pressure. The main page displays temperature and humidity only.

EN/RU buttons immediately save language in ESP32 EEPROM, without a restart or submitting settings. All browsers and `/get` use the same saved language. Default: English. Temperature remains Celsius in both languages; labels and number formatting change with language.

## API

Replace `<ESP32_IP>` with the local device IP. Default HTTP port: **80**, set by `WEB_PORT`.

| Method | Path | Result |
| --- | --- | --- |
| GET | `/` | Readings; settings while AP is active |
| GET | `/settings` | Wi-Fi and pressure settings |
| GET | `/get` | Localized message and numeric readings |
| GET | `/status` | Numeric readings and sensor status |
| GET | `/get_config` | Saved settings, language and AP state |
| POST | `/save_config` | Save settings and restart |
| POST | `/set_language` | Save `en` or `ru` without restart |
| POST | `/clear_config` | Restore defaults and restart into setup AP |

`/get` returns a localized `message` and separate numeric readings. The message uses integer rounding:

```json
{
  "status": "ok",
  "message": "Temperature: 23, Humidity: 45",
  "temperature": 23.4,
  "humidity": 45.2,
  "pressure_hpa": 1000.00,
  "altitude_m": 166.7
}
```

With Russian selected:

```json
{
  "status": "ok",
  "message": "Температура: 23, Влажность: 45",
  "temperature": 23.4,
  "humidity": 45.2,
  "pressure_hpa": 1000.00,
  "altitude_m": 166.7
}
```

If the sensor is unavailable, `message` contains a localized error, all four numeric readings are `null`, and `status` remains `ok`. Integrations using the former `data` field must now read `message`.

Example `/status`:

```json
{
  "sensor_ok": true,
  "language": "en",
  "temperature": 23.4,
  "humidity": 45.2,
  "pressure_hpa": 1000.00,
  "altitude_m": 166.7,
  "ApMode": false
}
```

Numeric JSON keys stay unchanged across languages. When the sensor is unavailable, `/status` returns `sensor_ok: false`; both `/get` and `/status` return `null` for all four readings.

Save language:

```bash
curl -X POST http://<ESP32_IP>/set_language -d "language=ru"
```

Save settings (`language` is optional; omitting it preserves the saved language):

```bash
curl -X POST http://<ESP32_IP>/save_config --data-urlencode "ssid=Home Wi-Fi" --data-urlencode "password=your-password" -d "sea_level_pressure=1020.00" -d "language=en"
```

Reset all settings:

```bash
curl -X POST http://<ESP32_IP>/clear_config
```

`/save_config` requires `ssid`, `password` and `sea_level_pressure`. Invalid input returns HTTP 400; persistence failure returns HTTP 500. Successful writes return `{"status":"ok"}`. `/set_language` also returns `language`.

`/get_config` includes the saved Wi-Fi password. Interface and API have no authentication; use a trusted local network.

## LED and recovery

| LED behavior | Meaning |
| --- | --- |
| Continuously on | Setup AP active; main Wi-Fi disconnected |
| On/off every 250 ms | Connecting to main Wi-Fi |
| On/off every second | Wi-Fi connected; sensor responds |
| Five quick flashes, then one second off | Wi-Fi connected; sensor does not respond |

Quick flashes last 100 ms, with 100 ms between flashes. One-second pause starts after the fifth flash.

At boot with saved credentials, Wi-Fi gets up to three minutes before setup AP starts. After an established connection drops, AP starts after 30 seconds. Reconnection attempts continue during AP mode; AP and captive DNS stop when main Wi-Fi returns.

An absent sensor does not block Wi-Fi setup or API access. Readings refresh every two seconds; a failed sensor is checked again every five seconds. Errors replace old readings until recovery.

## Troubleshooting

- **No readings:** check 3.3 V power, ground, SDA/SCL and sensor model. Both supported I2C addresses are tried automatically.
- **No Wi-Fi:** use a 2.4 GHz network; check credentials through the fallback setup AP.
- **No portal popup:** open `http://192.168.4.1` while connected to setup AP.
- **Wrong language:** select EN/RU on either page. Language belongs to ESP32 and affects every client.
- **Incorrect LED:** adjust GPIO and active polarity for your board.
