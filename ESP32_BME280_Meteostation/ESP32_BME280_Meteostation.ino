#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <math.h>
#include <stdlib.h>
#include <ctype.h>

// Status LED — ESP32-WROOM-32, active HIGH
#define STATUS_LED_PIN 2
#define STATUS_LED_ON HIGH
#define STATUS_LED_OFF LOW

// BME280 — I2C pins and reference pressure
#define BME_SDA_PIN 21
#define BME_SCL_PIN 22
#define DEFAULT_SEA_LEVEL_PRESSURE_HPA 1020.00f

// Setup access point
#define AP_SSID "ESP32-BME280-Meteostation"
#define AP_PASSWORD "password123"

// Saved configuration
#define EEPROM_SIZE 512
#define CONFIG_MAGIC 0x424D4501

// HTTP and captive DNS ports
#define WEB_PORT 80
#define DNS_PORT 53

// Wi-Fi connection and recovery intervals
#define WIFI_CONNECT_TIMEOUT_MS 180000UL
#define WIFI_AP_FALLBACK_MS 30000UL
#define WIFI_RECONNECT_INTERVAL_MS 5000UL

// Sensor reading and recovery intervals
#define SENSOR_READ_INTERVAL_MS 2000UL
#define SENSOR_RETRY_INTERVAL_MS 5000UL

// Sensor-error LED pattern
#define LED_FAST_BLINK_MS 100UL
#define LED_BLINK_COUNT 5UL
#define LED_PAUSE_MS 1000UL

struct Config
{
  uint32_t magic;
  char wifi_ssid[33];
  char wifi_password[65];
  float sea_level_pressure;
  char language[3];
  bool configured;
};
static_assert(sizeof(Config) <= EEPROM_SIZE, "Configuration exceeds EEPROM size");

WebServer WebServerInstance(WEB_PORT);
DNSServer DnsServerInstance;
Adafruit_BME280 bme;
Config config;
bool EepromReady = false;
bool ApMode = false;
bool SensorInitialized = false;
bool SensorStatus = false;
uint8_t SensorAddress = 0;
float Temperature = NAN;
float Humidity = NAN;
float PressureHpa = NAN;
unsigned long LastSensorRead = 0;
unsigned long LastSensorRetry = 0;
bool RestartPending = false;
unsigned long RestartRequestedAt = 0;
unsigned long WiFiDisconnectedAt = 0;
unsigned long LastReconnectAttempt = 0;
bool WiFiWasConnected = false;
bool WiFiEverConnected = false;

// Web interface; all assets served locally.
const char html_index[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title data-i18n="pageTitle">BME280 Meteostation</title>
    <style>
        :root {
            --bg: #090d14;
            --panel: #111827;
            --panel2: #182235;
            --text: #f4f7fb;
            --muted: #8b98ac;
            --accent: #00d4ff;
            --accent2: #087ea4;
            --green: #26d980;
            --yellow: #ffb020;
            --red: #ff4d67;
            --border: #263247;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        button,
        input {
            font: inherit;
        }

        body {
            min-height: 100vh;
            display: grid;
            place-items: center;
            padding: 24px;
            font-family:
                system-ui,
                -apple-system,
                BlinkMacSystemFont,
                "Segoe UI",
                sans-serif;
            font-size: 15px;
            line-height: 1.5;
            color: var(--text);
            background: radial-gradient(
                circle at top,
                #122036 0,
                #090d14 52%
            );
        }

        .panel {
            width: min(100%, 560px);
            padding: 32px;
            border: 1px solid var(--border);
            border-radius: 24px;
            background: linear-gradient(
                145deg,
                rgba(24, 34, 53, 0.96),
                rgba(13, 19, 30, 0.98)
            );
            box-shadow: 0 24px 70px rgba(0, 0, 0, 0.45);
        }

        .brand {
            display: flex;
            align-items: center;
            gap: 14px;
            margin-bottom: 30px;
        }

        .brand-copy {
            flex: 1;
            min-width: 0;
        }

        .brand .header-link {
            margin-left: auto;
        }

        .logo {
            flex-shrink: 0;
            display: grid;
            place-items: center;
            width: 48px;
            height: 48px;
            border: 1px solid rgba(0, 212, 255, 0.22);
            border-radius: 14px;
            background: linear-gradient(145deg, #173044, #0c1725);
            color: #dceef7;
            box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.06);
            transition: border-color 0.2s, background 0.2s;
        }

        .logo:hover {
            color: #fff;
            border-color: var(--accent);
            background: #173044;
        }

        .logo:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 4px;
        }

        .header-link {
            display: grid;
            place-items: center;
            flex-shrink: 0;
            width: 44px;
            height: 44px;
            border: 1px solid var(--border);
            border-radius: 12px;
            color: var(--muted);
            background: rgba(7, 12, 20, 0.3);
            transition: color 0.2s, border-color 0.2s;
        }

        .header-link[hidden] {
            display: none;
        }

        .header-link:hover {
            color: var(--accent);
            border-color: var(--accent);
        }

        .header-link:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 3px;
        }

        h1 {
            font-size: clamp(22px, 5vw, 30px);
            font-weight: 600;
            line-height: 1.2;
        }

        .subtitle {
            margin-top: 4px;
            color: var(--muted);
            font-size: 14px;
        }

        .status {
            display: flex;
            align-items: center;
            justify-content: space-between;
            gap: 16px;
            padding: 22px;
            margin-bottom: 22px;
            border: 1px solid var(--border);
            border-radius: 18px;
            background: rgba(7, 12, 20, 0.55);
        }

        .status-copy span {
            display: block;
            color: var(--muted);
            font-size: 12px;
            text-transform: uppercase;
            letter-spacing: 0.08em;
        }

        .status-copy strong {
            display: block;
            margin-top: 5px;
            font-size: 22px;
            font-weight: 600;
            line-height: 1.3;
            letter-spacing: -0.015em;
        }

        .indicator {
            width: 16px;
            height: 16px;
            border-radius: 50%;
            background: var(--muted);
            box-shadow: 0 0 0 7px rgba(139, 152, 172, 0.1);
        }

        .status.off .indicator {
            background: var(--red);
            box-shadow: 0 0 0 7px rgba(255, 77, 103, 0.12);
        }

        .status.starting .indicator,
        .status.shutting .indicator {
            background: var(--yellow);
            box-shadow: 0 0 0 7px rgba(255, 176, 32, 0.12);
            animation: pulse 1s infinite;
        }

        .status.on .indicator {
            background: var(--green);
            box-shadow: 0 0 0 7px rgba(38, 217, 128, 0.12);
            animation: breathe 2.4s ease-in-out infinite;
        }

        .controls {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }

        .button {
            min-height: 54px;
            border: 0;
            border-radius: 14px;
            padding: 14px 18px;
            color: #fff;
            background: #27344a;
            font-size: 15px;
            font-weight: 600;
            cursor: pointer;
            transition: 0.2s;
        }

        .button:hover {
            transform: translateY(-2px);
            filter: brightness(1.12);
        }

        .button:disabled {
            opacity: 0.55;
            cursor: wait;
            transform: none;
        }

        .button.primary {
            grid-column: 1/-1;
            color: #001018;
            background: linear-gradient(135deg, var(--accent), #4be1ff);
        }

        .button.danger {
            background: #512532;
            color: #ffb8c2;
        }

        .confirmation {
            margin: auto;
            width: min(440px, calc(100% - 32px));
            max-height: calc(100% - 32px);
            overflow: auto;
            padding: 28px;
            border: 1px solid var(--border);
            border-radius: 22px;
            color: var(--text);
            background: linear-gradient(145deg, #182235, #0d131e);
            box-shadow: 0 24px 70px rgba(0, 0, 0, 0.55);
        }

        .confirmation::backdrop {
            background: rgba(3, 7, 13, 0.75);
            backdrop-filter: blur(5px);
        }

        .confirmation h2 {
            font-size: 22px;
            font-weight: 600;
            line-height: 1.3;
        }

        .confirmation p {
            margin: 12px 0 24px;
            color: var(--muted);
        }

        .confirmation-actions {
            display: flex;
            flex-wrap: wrap;
            gap: 12px;
        }

        .confirmation-actions .button {
            flex: 1 1 140px;
        }

        .button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 4px;
        }

        .language-switch {
            display: inline-flex;
            gap: 2px;
            padding: 3px;
            border: 1px solid var(--border);
            border-radius: 10px;
            flex-shrink: 0;
        }

        .language-switch button {
            border: 0;
            border-radius: 7px;
            padding: 6px 9px;
            background: transparent;
            color: var(--muted);
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
        }

        .language-switch button[aria-pressed="true"] {
            background: #27344a;
            color: var(--text);
        }

        .language-switch button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 2px;
        }

        .footer {
            flex-wrap: wrap;
            gap: 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 24px;
            padding-top: 20px;
            border-top: 1px solid var(--border);
        }

        a {
            color: var(--muted);
            text-decoration: none;
            font-weight: 600;
        }

        .author {
            margin-left: auto;
            font-size: 12px;
            color: #536177;
        }

        a:hover {
            color: var(--accent);
        }

        @keyframes pulse {
            50% {
                transform: scale(0.75);
                opacity: 0.55;
            }
        }

        @keyframes breathe {
            0%, 100% {
                transform: scale(0.85);
                opacity: 0.6;
                box-shadow: 0 0 0 4px rgba(38, 217, 128, 0.06);
            }
            50% {
                transform: scale(1.1);
                opacity: 1;
                box-shadow: 0 0 0 9px rgba(38, 217, 128, 0.18);
            }
        }

        @media (max-width: 480px) {
            .panel {
                padding: 22px;
            }

            .controls {
                grid-template-columns: 1fr;
            }

            .button.primary {
                grid-column: auto;
            }

        }
    </style>
</head>
<body>
    <main class="panel">
        <div class="brand">
            <a class="logo"
                href="https://github.com/XEGARE/ESP32-BME280-Meteostation"
                target="_blank" rel="noopener noreferrer"
                data-i18n-aria="github" aria-label="View project on GitHub (opens in a new tab)">
                <svg aria-hidden="true" focusable="false" width="32" height="32"
                    viewBox="0 0 32 32" fill="none" stroke="currentColor"
                    stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                    <g stroke="var(--accent)">
                        <circle cx="10" cy="9" r="4" />
                        <path d="M10 2v1M3 9H2M5 4 4 3m11 1 1-1m1 6h1" />
                    </g>
                    <path d="M9 26h15a5 5 0 0 0 .6-10A7 7 0 0 0 11.3 14 6 6 0 0 0 9 26Z"
                        fill="var(--panel)" />
                </svg>
            </a>
            <div class="brand-copy">
                <h1 data-i18n="heading">Meteostation</h1>
                <div class="subtitle" data-i18n="subtitle">Temperature and humidity</div>
            </div>
            <a class="header-link" href="/settings" aria-label="Settings" data-i18n-aria="settings">
                <svg aria-hidden="true" focusable="false" width="22" height="22"
                    viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">
                    <path d="m9 3-.5 2.2-1.6.9-2.2-.6-3 5.2 1.7 1.5v1.8l-1.7 1.5 3 5.2 2.2-.6 1.6.9L9 23h6l.5-2.2 1.6-.9 2.2.6 3-5.2-1.7-1.5V12l1.7-1.5-3-5.2-2.2.6-1.6-.9L15 3Z" transform="translate(1.2 .3) scale(.9)" />
                    <circle cx="12" cy="12" r="3" />
                </svg>
            </a>
        </div>
        <section id="temperatureStatus" class="status" aria-live="polite">
            <div class="status-copy">
                <span data-i18n="temperature">Temperature</span>
                <strong id="temperature" data-i18n="checking">Checking...</strong>
            </div>
            <div class="indicator" aria-hidden="true"></div>
        </section>
        <section id="humidityStatus" class="status" aria-live="polite">
            <div class="status-copy">
                <span data-i18n="humidity">Humidity</span>
                <strong id="humidity" data-i18n="checking">Checking...</strong>
            </div>
            <div class="indicator" aria-hidden="true"></div>
        </section>
        <div class="footer">
            <div class="language-switch" role="group" aria-label="Language" data-i18n-aria="language">
                <button type="button" data-language="en" lang="en" aria-label="English" aria-pressed="true">EN</button>
                <button type="button" data-language="ru" lang="ru" aria-label="Русский" aria-pressed="false">RU</button>
            </div>
            <a
                class="author"
                href="https://xegare.com"
                target="_blank"
                rel="noopener"
                 data-i18n="author">by XEGARE</a
            >
        </div>
    </main>
    <script>
        
        const translations = {
        "en": {
                "language": "Language",
                "author": "by XEGARE",
                "github": "View project on GitHub (opens in a new tab)",
                "pageTitle": "BME280 Meteostation",
                "heading": "Meteostation",
                "subtitle": "Temperature and humidity",
                "settings": "Settings",
                "temperature": "Temperature",
                "humidity": "Humidity",
                "checking": "Checking...",
                "sensorError": "BME280 is not responding",
                "connectionError": "Device is unavailable",
                "settingsTitle": "Settings | BME280 Meteostation",
                "settingsHeading": "Settings",
                "settingsSubtitle": "Wi-Fi and sea-level pressure",
                "ssid": "Wi-Fi network",
                "password": "Wi-Fi password",
                "pressure": "Sea-level pressure (hPa)",
                "show": "Show",
                "hide": "Hide",
                "save": "Save and restart",
                "back": "Back to readings",
                "saving": "Saving...",
                "saved": "Saved. Restarting...",
                "reconnect": "If connection is lost, reconnect to your Wi-Fi and open the device IP shown by your router or Serial Monitor.",
                "saveError": "Failed to save settings",
                "languageError": "Failed to save language",
                "loadError": "Failed to load settings"
        },
        "ru": {
                "language": "Язык",
                "author": "от XEGARE",
                "github": "Проект на GitHub (откроется в новой вкладке)",
                "pageTitle": "Метеостанция BME280",
                "heading": "Метеостанция",
                "subtitle": "Температура и влажность",
                "settings": "Настройки",
                "temperature": "Температура",
                "humidity": "Влажность",
                "checking": "Проверка...",
                "sensorError": "Датчик BME280 не отвечает",
                "connectionError": "Устройство недоступно",
                "settingsTitle": "Настройки | Метеостанция BME280",
                "settingsHeading": "Настройки",
                "settingsSubtitle": "Wi-Fi и давление на уровне моря",
                "ssid": "Сеть Wi-Fi",
                "password": "Пароль Wi-Fi",
                "pressure": "Давление на уровне моря (гПа)",
                "show": "Показать",
                "hide": "Скрыть",
                "save": "Сохранить и перезапустить",
                "back": "К показаниям",
                "saving": "Сохранение...",
                "saved": "Сохранено. Перезапуск...",
                "reconnect": "Если связь пропала, подключитесь к своей сети Wi-Fi и откройте IP устройства из роутера или монитора порта.",
                "saveError": "Не удалось сохранить настройки",
                "languageError": "Не удалось сохранить язык",
                "loadError": "Не удалось загрузить настройки"
        }
}
        let language = 'en'
        function Translate(key) {
            return translations[language][key] || translations.en[key] || key
        }
        function SetText(element, key) {
            element.dataset.i18n = key
            element.textContent = Translate(key)
        }
        function ApplyLanguage() {
            document.documentElement.lang = language
            document.querySelectorAll('[data-i18n]').forEach(element => {
                element.textContent = Translate(element.dataset.i18n)
            })
            document.querySelectorAll('[data-i18n-aria]').forEach(element => {
                element.setAttribute('aria-label', Translate(element.dataset.i18nAria))
            })
            document.querySelectorAll('[data-language]').forEach(button => {
                button.setAttribute('aria-pressed', String(button.dataset.language === language))
            })
        }
        async function Request(url, options = {}) {
            const response = await fetch(new URL(url, window.location.origin), {
                cache: 'no-store', ...options, signal: AbortSignal.timeout(5000)
            })
            if (!response.ok) throw new Error('HTTP ' + response.status)
            return response.json()
        }
        document.querySelectorAll('[data-language]').forEach(button => {
            button.addEventListener('click', async () => {
                const buttons = document.querySelectorAll('[data-language]')
                buttons.forEach(item => { item.disabled = true })
                try {
                    const data = await Request('/set_language', {
                        method: 'POST', body: new URLSearchParams({language: button.dataset.language})
                    })
                    language = data.language
                    ApplyLanguage()
                    if (typeof RefreshReadings === 'function') await RefreshReadings()
                } catch (error) {
                    alert(Translate('languageError'))
                } finally {
                    buttons.forEach(item => { item.disabled = false })
                }
            })
        })
        ApplyLanguage()

        let refreshing = false
        function SetReading(id, value, errorKey) {
            const element = document.getElementById(id)
            document.getElementById(id + 'Status').className = 'status ' + (errorKey ? 'off' : 'on')
            if (errorKey) SetText(element, errorKey)
            else {
                delete element.dataset.i18n
                element.textContent = value
            }
        }
        async function RefreshReadings() {
            if (refreshing) return
            refreshing = true
            try {
                const data = await Request('/status')
                language = data.language
                ApplyLanguage()
                const errorKey = data.sensor_ok ? null : 'sensorError'
                const locale = language === 'ru' ? 'ru-RU' : 'en-US'
                const format = value => Number(value).toLocaleString(locale, {maximumFractionDigits: 1, minimumFractionDigits: 1})
                SetReading('temperature', format(data.temperature) + ' °C', errorKey)
                SetReading('humidity', format(data.humidity) + ' %', errorKey)
            } catch (error) {
                SetReading('temperature', '', 'connectionError')
                SetReading('humidity', '', 'connectionError')
            } finally {
                refreshing = false
            }
        }
        RefreshReadings()
        setInterval(RefreshReadings, 2000)
    </script>
</body>
</html>
)rawliteral";
const char html_config[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8" />
    <meta name="viewport" content="width=device-width,initial-scale=1" />
    <title data-i18n="settingsTitle">Settings | BME280 Meteostation</title>
    <style>
        :root {
            --bg: #090d14;
            --panel: #111827;
            --text: #f4f7fb;
            --muted: #8b98ac;
            --accent: #00d4ff;
            --border: #263247;
            --input: #0b111c;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        button,
        input {
            font: inherit;
        }

        body {
            min-height: 100vh;
            display: grid;
            place-items: center;
            padding: 24px;
            font-family:
                system-ui,
                -apple-system,
                BlinkMacSystemFont,
                "Segoe UI",
                sans-serif;
            font-size: 15px;
            line-height: 1.5;
            color: var(--text);
            background: radial-gradient(
                circle at top,
                #122036 0,
                #090d14 52%
            );
        }

        .panel {
            width: min(100%, 560px);
            padding: 32px;
            border: 1px solid var(--border);
            border-radius: 24px;
            background: linear-gradient(
                145deg,
                rgba(24, 34, 53, 0.96),
                rgba(13, 19, 30, 0.98)
            );
            box-shadow: 0 24px 70px rgba(0, 0, 0, 0.45);
        }

        .header-link {
            display: grid;
            place-items: center;
            flex-shrink: 0;
            width: 44px;
            height: 44px;
            border: 1px solid var(--border);
            border-radius: 12px;
            color: var(--muted);
            background: rgba(7, 12, 20, 0.3);
            transition: color 0.2s, border-color 0.2s;
        }

        .header-link[hidden] {
            display: none;
        }

        .header-link:hover {
            color: var(--accent);
            border-color: var(--accent);
        }

        .header-link:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 3px;
        }

        h1 {
            font-size: clamp(22px, 5vw, 28px);
            font-weight: 600;
            line-height: 1.2;
        }

        .subtitle {
            margin: 7px 0 28px;
            color: var(--muted);
        }

        .settings-header {
            display: flex;
            align-items: center;
            gap: 14px;
        }

        .settings-header h1 {
            min-width: 0;
        }

        .field {
            margin-bottom: 18px;
        }

        label {
            display: block;
            margin-bottom: 8px;
            color: #c7d0df;
            font-size: 13px;
            font-weight: 600;
        }

        input {
            width: 100%;
            height: 50px;
            padding: 0 15px;
            border: 1px solid var(--border);
            border-radius: 12px;
            outline: none;
            background: var(--input);
            color: var(--text);
            font-size: 15px;
        }

        input:focus {
            border-color: var(--accent);
            box-shadow: 0 0 0 3px rgba(0, 212, 255, 0.12);
        }

        .password {
            position: relative;
        }

        .password input {
            padding-right: 108px;
        }

        .toggle {
            position: absolute;
            right: 8px;
            top: 7px;
            height: 36px;
            padding: 0 10px;
            border: 0;
            border-radius: 9px;
            background: #1b293d;
            color: var(--muted);
            cursor: pointer;
        }

        .save {
            width: 100%;
            height: 52px;
            margin-top: 8px;
            border: 0;
            border-radius: 13px;
            background: linear-gradient(135deg, var(--accent), #4be1ff);
            color: #001018;
            font-size: 15px;
            font-weight: 600;
            cursor: pointer;
        }

        .save:disabled {
            opacity: 0.6;
            cursor: wait;
        }

        .language-switch {
            display: inline-flex;
            gap: 2px;
            padding: 3px;
            border: 1px solid var(--border);
            border-radius: 10px;
            flex-shrink: 0;
        }

        .language-switch button {
            border: 0;
            border-radius: 7px;
            padding: 6px 9px;
            background: transparent;
            color: var(--muted);
            font-size: 12px;
            font-weight: 600;
            cursor: pointer;
        }

        .language-switch button[aria-pressed="true"] {
            background: #27344a;
            color: var(--text);
        }

        .language-switch button:focus-visible {
            outline: 2px solid var(--accent);
            outline-offset: 2px;
        }

        .footer {
            flex-wrap: wrap;
            gap: 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 24px;
            padding-top: 20px;
            border-top: 1px solid var(--border);
        }

        a {
            color: var(--muted);
            text-decoration: none;
            font-weight: 600;
        }

        .author {
            margin-left: auto;
            font-size: 12px;
            color: #536177;
        }

        a:hover {
            color: var(--accent);
        }

        @media (max-width: 480px) {
            .panel {
                padding: 22px;
            }

        }
    </style>
</head>
<body>
    <main class="panel">
        <div class="settings-header">
            <a id="backToReadings" class="header-link" href="/" hidden
                aria-label="Back to readings" data-i18n-aria="back">
                <svg aria-hidden="true" focusable="false" width="22" height="22"
                    viewBox="0 0 24 24" fill="none" stroke="currentColor"
                    stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
                    <path d="m12 5-7 7 7 7M5 12h14" />
                </svg>
            </a>
            <h1 data-i18n="settingsHeading">Settings</h1>
        </div>
        <div class="subtitle" data-i18n="settingsSubtitle">Wi-Fi and sea-level pressure</div>
        <form id="configForm">
            <fieldset id="settingsFields" disabled style="border:0;min-width:0">
                <div class="field">
                    <label for="ssid" data-i18n="ssid">Wi-Fi network</label>
                    <input id="ssid" name="ssid" maxlength="32" required autocomplete="off" />
                </div>
                <div class="field">
                    <label for="password" data-i18n="password">Wi-Fi password</label>
                    <div class="password">
                        <input type="password" id="password" name="password" maxlength="64" autocomplete="off" />
                        <button class="toggle" type="button" onclick="TogglePassword()" data-i18n="show">Show</button>
                    </div>
                </div>
                <div class="field">
                    <label for="sea_level_pressure" data-i18n="pressure">Sea-level pressure (hPa)</label>
                    <input type="number" id="sea_level_pressure" name="sea_level_pressure"
                        min="800" max="1200" step="0.01" inputmode="decimal" required />
                </div>
                <button id="saveButton" class="save" type="submit" data-i18n="save">Save and restart</button>
            </fieldset>
        </form>
        <p id="saveNotice" class="subtitle" role="status" hidden></p>
        <div class="footer">
            <div class="language-switch" role="group" aria-label="Language" data-i18n-aria="language">
                <button type="button" data-language="en" lang="en" aria-label="English" aria-pressed="true">EN</button>
                <button type="button" data-language="ru" lang="ru" aria-label="Русский" aria-pressed="false">RU</button>
            </div>
            <a
                class="author"
                href="https://xegare.com"
                target="_blank"
                rel="noopener"
                 data-i18n="author">by XEGARE</a
            >
        </div>
    </main>
    <script>
        
        const translations = {
        "en": {
                "language": "Language",
                "author": "by XEGARE",
                "github": "View project on GitHub (opens in a new tab)",
                "pageTitle": "BME280 Meteostation",
                "heading": "Meteostation",
                "subtitle": "Temperature and humidity",
                "settings": "Settings",
                "temperature": "Temperature",
                "humidity": "Humidity",
                "checking": "Checking...",
                "sensorError": "BME280 is not responding",
                "connectionError": "Device is unavailable",
                "settingsTitle": "Settings | BME280 Meteostation",
                "settingsHeading": "Settings",
                "settingsSubtitle": "Wi-Fi and sea-level pressure",
                "ssid": "Wi-Fi network",
                "password": "Wi-Fi password",
                "pressure": "Sea-level pressure (hPa)",
                "show": "Show",
                "hide": "Hide",
                "save": "Save and restart",
                "back": "Back to readings",
                "saving": "Saving...",
                "saved": "Saved. Restarting...",
                "reconnect": "If connection is lost, reconnect to your Wi-Fi and open the device IP shown by your router or Serial Monitor.",
                "saveError": "Failed to save settings",
                "languageError": "Failed to save language",
                "loadError": "Failed to load settings"
        },
        "ru": {
                "language": "Язык",
                "author": "от XEGARE",
                "github": "Проект на GitHub (откроется в новой вкладке)",
                "pageTitle": "Метеостанция BME280",
                "heading": "Метеостанция",
                "subtitle": "Температура и влажность",
                "settings": "Настройки",
                "temperature": "Температура",
                "humidity": "Влажность",
                "checking": "Проверка...",
                "sensorError": "Датчик BME280 не отвечает",
                "connectionError": "Устройство недоступно",
                "settingsTitle": "Настройки | Метеостанция BME280",
                "settingsHeading": "Настройки",
                "settingsSubtitle": "Wi-Fi и давление на уровне моря",
                "ssid": "Сеть Wi-Fi",
                "password": "Пароль Wi-Fi",
                "pressure": "Давление на уровне моря (гПа)",
                "show": "Показать",
                "hide": "Скрыть",
                "save": "Сохранить и перезапустить",
                "back": "К показаниям",
                "saving": "Сохранение...",
                "saved": "Сохранено. Перезапуск...",
                "reconnect": "Если связь пропала, подключитесь к своей сети Wi-Fi и откройте IP устройства из роутера или монитора порта.",
                "saveError": "Не удалось сохранить настройки",
                "languageError": "Не удалось сохранить язык",
                "loadError": "Не удалось загрузить настройки"
        }
}
        let language = 'en'
        function Translate(key) {
            return translations[language][key] || translations.en[key] || key
        }
        function SetText(element, key) {
            element.dataset.i18n = key
            element.textContent = Translate(key)
        }
        function ApplyLanguage() {
            document.documentElement.lang = language
            document.querySelectorAll('[data-i18n]').forEach(element => {
                element.textContent = Translate(element.dataset.i18n)
            })
            document.querySelectorAll('[data-i18n-aria]').forEach(element => {
                element.setAttribute('aria-label', Translate(element.dataset.i18nAria))
            })
            document.querySelectorAll('[data-language]').forEach(button => {
                button.setAttribute('aria-pressed', String(button.dataset.language === language))
            })
        }
        async function Request(url, options = {}) {
            const response = await fetch(new URL(url, window.location.origin), {
                cache: 'no-store', ...options, signal: AbortSignal.timeout(5000)
            })
            if (!response.ok) throw new Error('HTTP ' + response.status)
            return response.json()
        }
        document.querySelectorAll('[data-language]').forEach(button => {
            button.addEventListener('click', async () => {
                const buttons = document.querySelectorAll('[data-language]')
                buttons.forEach(item => { item.disabled = true })
                try {
                    const data = await Request('/set_language', {
                        method: 'POST', body: new URLSearchParams({language: button.dataset.language})
                    })
                    language = data.language
                    ApplyLanguage()
                    if (typeof RefreshReadings === 'function') await RefreshReadings()
                } catch (error) {
                    alert(Translate('languageError'))
                } finally {
                    buttons.forEach(item => { item.disabled = false })
                }
            })
        })
        ApplyLanguage()

        function TogglePassword() {
            const input = document.getElementById('password')
            const visible = input.type === 'text'
            input.type = visible ? 'password' : 'text'
            SetText(document.querySelector('.toggle'), visible ? 'show' : 'hide')
        }
        Request('/get_config').then(data => {
            language = data.language
            ApplyLanguage()
            document.getElementById('ssid').value = data.ssid || ''
            document.getElementById('password').value = data.password || ''
            document.getElementById('sea_level_pressure').value = data.sea_level_pressure
            document.getElementById('backToReadings').hidden = data.ApMode
            document.getElementById('settingsFields').disabled = false
        }).catch(() => {
            const notice = document.getElementById('saveNotice')
            notice.hidden = false
            SetText(notice, 'loadError')
        })
        document.getElementById('configForm').addEventListener('submit', async event => {
            event.preventDefault()
            const fields = document.getElementById('settingsFields')
            const button = document.getElementById('saveButton')
            const body = new URLSearchParams(new FormData(event.target))
            body.set('language', language)
            fields.disabled = true
            document.querySelectorAll('[data-language]').forEach(item => { item.disabled = true })
            SetText(button, 'saving')
            try {
                await Request('/save_config', {method: 'POST', body})
                SetText(button, 'saved')
                const notice = document.getElementById('saveNotice')
                notice.hidden = false
                SetText(notice, 'reconnect')
                setTimeout(async () => {
                    for (let attempt = 0; attempt < 30; attempt++) {
                        try {
                            await Request('/get_config')
                            window.location.assign(new URL('/', window.location.origin))
                            return
                        } catch (error) {}
                        await new Promise(resolve => setTimeout(resolve, 2000))
                    }
                }, 4000)
            } catch (error) {
                fields.disabled = false
                document.querySelectorAll('[data-language]').forEach(item => { item.disabled = false })
                SetText(button, 'save')
                alert(Translate('saveError'))
            }
        })
    </script>
</body>
</html>
)rawliteral";

bool IsRussian()
{
  return strcmp(config.language, "ru") == 0;
}

bool IsValidLanguage(const String &language)
{
  return language == "en" || language == "ru";
}

bool SaveConfig()
{
  if(!EepromReady) return false;
  EEPROM.put(0, config);
  return EEPROM.commit();
}

void ResetConfig()
{
  memset(&config, 0, sizeof(config));
  config.magic = CONFIG_MAGIC;
  config.sea_level_pressure = DEFAULT_SEA_LEVEL_PRESSURE_HPA;
  strlcpy(config.language, "en", sizeof(config.language));
}

void LoadConfig()
{
  if(!EepromReady)
  {
    ResetConfig();
    return;
  }
  EEPROM.get(0, config);
  const bool valid = config.magic == CONFIG_MAGIC &&
    memchr(config.wifi_ssid, '\0', sizeof(config.wifi_ssid)) != nullptr &&
    memchr(config.wifi_password, '\0', sizeof(config.wifi_password)) != nullptr &&
    config.language[2] == '\0' && IsValidLanguage(String(config.language)) &&
    isfinite(config.sea_level_pressure) &&
    config.sea_level_pressure >= 800.0f && config.sea_level_pressure <= 1200.0f;
  if(!valid)
  {
    ResetConfig();
    if(!SaveConfig()) Serial.println("Failed to initialize saved settings");
  }
  // An empty SSID always means setup mode.
  config.configured = config.wifi_ssid[0] != '\0';
}

String JsonString(const String &value)
{
  String result = "\"";
  for(size_t i = 0; i < value.length(); i++)
  {
    const uint8_t ch = static_cast<uint8_t>(value[i]);
    if(ch == '"' || ch == '\\')
    {
      result += '\\';
      result += static_cast<char>(ch);
    }
    else if(ch < 0x20)
    {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
      result += escaped;
    }
    else result += static_cast<char>(ch);
  }
  return result + "\"";
}

void SendJson(int code, const String &json)
{
  WebServerInstance.sendHeader("Cache-Control", "no-store");
  WebServerInstance.send(code, "application/json; charset=utf-8", json);
}

void SendError(int code, const char *message)
{
  SendJson(code, "{\"status\":\"error\",\"error\":" + JsonString(message) + "}");
}

bool SensorResponds(uint8_t address)
{
  // Probe chip ID as well as ACK so unplugged sensors cannot report stale data.
  Wire.beginTransmission(address);
  Wire.write(0xD0);
  if(Wire.endTransmission(false) != 0) return false;
  if(Wire.requestFrom(address, static_cast<uint8_t>(1)) != 1) return false;
  return Wire.read() == 0x60;
}

void InvalidateSensor()
{
  SensorStatus = false;
  SensorInitialized = false;
  Temperature = Humidity = PressureHpa = NAN;
  LastSensorRetry = millis();
}

void ReadSensor()
{
  LastSensorRead = millis();
  if(!SensorInitialized || !SensorResponds(SensorAddress))
  {
    InvalidateSensor();
    return;
  }
  const float temperature = bme.readTemperature();
  const float humidity = bme.readHumidity();
  const float pressure = bme.readPressure() / 100.0f;
  if(!SensorResponds(SensorAddress) || !isfinite(temperature) || !isfinite(humidity) ||
     !isfinite(pressure) || temperature < -40.0f || temperature > 85.0f ||
     humidity < 0.0f || humidity > 100.0f || pressure < 300.0f || pressure > 1100.0f)
  {
    InvalidateSensor();
    return;
  }
  Temperature = temperature;
  Humidity = humidity;
  PressureHpa = pressure;
  SensorStatus = true;
}

void InitializeSensor()
{
  LastSensorRetry = millis();
  const uint8_t addresses[] = {0x76, 0x77};
  for(uint8_t address : addresses)
  {
    if(SensorResponds(address) && bme.begin(address, &Wire))
    {
      SensorAddress = address;
      SensorInitialized = true;
      // Allow first conversion to finish before reading; no wait in setup.
      LastSensorRead = millis();
      Serial.printf("BME280 detected at 0x%02X\n", address);
      return;
    }
  }
  InvalidateSensor();
  Serial.println("BME280 not responding; retrying in 5 seconds");
}

void HandleSensor()
{
  const unsigned long now = millis();
  if(!SensorInitialized)
  {
    if(now - LastSensorRetry >= SENSOR_RETRY_INTERVAL_MS) InitializeSensor();
  }
  else if(now - LastSensorRead >= SENSOR_READ_INTERVAL_MS) ReadSensor();
}

void StartAPMode()
{
  if(ApMode) return;
  WiFi.mode(WIFI_AP_STA);
  if(!WiFi.softAP(AP_SSID, AP_PASSWORD))
  {
    Serial.println("Failed to start setup AP; will retry");
    return;
  }
  DnsServerInstance.start(DNS_PORT, "*", WiFi.softAPIP());
  ApMode = true;
  Serial.printf("Setup AP: %s | http://%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void HandleWiFi()
{
  const unsigned long now = millis();
  if(WiFi.status() == WL_CONNECTED)
  {
    if(!WiFiWasConnected)
      Serial.printf("Wi-Fi connected | http://%s:%d\n", WiFi.localIP().toString().c_str(), WEB_PORT);
    WiFiWasConnected = true;
    WiFiEverConnected = true;
    if(ApMode)
    {
      DnsServerInstance.stop();
      WiFi.softAPdisconnect(false);
      WiFi.mode(WIFI_STA);
      ApMode = false;
    }
    return;
  }
  if(WiFiWasConnected)
  {
    WiFiWasConnected = false;
    WiFiDisconnectedAt = now;
    LastReconnectAttempt = now;
    Serial.println("Wi-Fi lost");
  }
  // First boot gets the controller's 3-minute window; later outages get 30 sec.
  const unsigned long timeout = WiFiEverConnected ? WIFI_AP_FALLBACK_MS : WIFI_CONNECT_TIMEOUT_MS;
  if(!ApMode && (!config.configured || now - WiFiDisconnectedAt >= timeout)) StartAPMode();
  if(config.configured && now - LastReconnectAttempt >= WIFI_RECONNECT_INTERVAL_MS)
  {
    WiFi.reconnect();
    LastReconnectAttempt = now;
  }
}

void HandleStatusLED()
{
  static unsigned long cycleStarted = 0;
  static int previousMode = -1;
  const unsigned long now = millis();
  // Connecting: quick blink. AP: steady. Connected: sensor determines pattern.
  const int mode = WiFi.status() == WL_CONNECTED ? (SensorStatus ? 1 : 2) : (ApMode ? 0 : 3);
  if(mode != previousMode)
  {
    cycleStarted = now;
    previousMode = mode;
  }
  const unsigned long elapsed = now - cycleStarted;
  bool ledOn = true;
  if(mode == 1) ledOn = elapsed % 2000UL < 1000UL;
  else if(mode == 2)
  {
    const unsigned long burstDuration = (LED_BLINK_COUNT * 2UL - 1UL) * LED_FAST_BLINK_MS;
    const unsigned long phase = elapsed % (burstDuration + LED_PAUSE_MS);
    ledOn = phase < burstDuration && (phase / LED_FAST_BLINK_MS) % 2UL == 0;
  }
  else if(mode == 3) ledOn = elapsed % 500UL < 250UL;
  digitalWrite(STATUS_LED_PIN, ledOn ? STATUS_LED_ON : STATUS_LED_OFF);
}

String FormatSensorValues()
{
  String json = "\"temperature\":" + (SensorStatus ? String(Temperature, 1) : String("null"));
  json += ",\"humidity\":" + (SensorStatus ? String(Humidity, 1) : String("null"));
  json += ",\"pressure_hpa\":" + (SensorStatus ? String(PressureHpa, 2) : String("null"));
  // Sea-level pressure is a reference for altitude, not a replacement for measured pressure.
  const float altitude = SensorStatus ? 44330.0f * (1.0f - powf(PressureHpa / config.sea_level_pressure, 0.1903f)) : NAN;
  json += ",\"altitude_m\":" + (SensorStatus ? String(altitude, 1) : String("null"));
  return json;
}

String FormatSensorData()
{
  String message;
  if(SensorStatus)
  {
    // Keep integer rounding in the localized message for Alice.
    message = String(IsRussian() ? "Температура: " : "Temperature: ") + String(Temperature, 0);
    message += String(IsRussian() ? ", Влажность: " : ", Humidity: ") + String(Humidity, 0);
  }
  else message = IsRussian() ? "Датчик BME280 не отвечает!" : "BME280 is not responding!";
  return "{\"status\":\"ok\",\"message\":" + JsonString(message) + "," + FormatSensorValues() + "}";
}

void SendSensorStatus()
{
  String json = "{\"sensor_ok\":" + String(SensorStatus ? "true" : "false");
  json += ",\"language\":" + JsonString(config.language);
  json += "," + FormatSensorValues();
  json += ",\"ApMode\":" + String(ApMode ? "true" : "false") + "}";
  SendJson(200, json);
}

void SendStartPage()
{
  WebServerInstance.sendHeader("Cache-Control", "no-store");
  WebServerInstance.send_P(200, "text/html; charset=utf-8", ApMode ? html_config : html_index);
}

void RequestRestart()
{
  RestartRequestedAt = millis();
  RestartPending = true;
}

bool ParsePressure(const String &text, float &pressure)
{
  char *end = nullptr;
  pressure = strtof(text.c_str(), &end);
  return text.length() > 0 && text.length() == strlen(text.c_str()) &&
    end != text.c_str() && *end == '\0' &&
    isfinite(pressure) && pressure >= 800.0f && pressure <= 1200.0f;
}

bool ValidPassword(const String &password)
{
  if(password.length() == 0) return true; // Open network.
  if(password.length() >= 8 && password.length() <= 63) return true;
  if(password.length() != 64) return false;
  for(size_t i = 0; i < password.length(); i++)
    if(!isxdigit(static_cast<unsigned char>(password[i]))) return false;
  return true; // 64-character hexadecimal WPA key.
}

void HandleSaveConfig()
{
  if(RestartPending) return SendError(409, "Restart pending");
  if(!WebServerInstance.hasArg("ssid") || !WebServerInstance.hasArg("password") ||
     !WebServerInstance.hasArg("sea_level_pressure")) return SendError(400, "Missing settings");
  const String ssid = WebServerInstance.arg("ssid");
  const String password = WebServerInstance.arg("password");
  const String language = WebServerInstance.hasArg("language") ? WebServerInstance.arg("language") : String(config.language);
  float pressure;
  if(ssid.length() == 0 || ssid.length() > 32 || ssid.length() != strlen(ssid.c_str()))
    return SendError(400, "SSID must contain 1-32 bytes");
  if(!ValidPassword(password) || password.length() != strlen(password.c_str()))
    return SendError(400, "Invalid Wi-Fi password");
  if(!IsValidLanguage(language)) return SendError(400, "Language must be en or ru");
  if(!ParsePressure(WebServerInstance.arg("sea_level_pressure"), pressure))
    return SendError(400, "Sea-level pressure must be 800-1200 hPa");
  const Config previous = config;
  strlcpy(config.wifi_ssid, ssid.c_str(), sizeof(config.wifi_ssid));
  strlcpy(config.wifi_password, password.c_str(), sizeof(config.wifi_password));
  strlcpy(config.language, language.c_str(), sizeof(config.language));
  config.sea_level_pressure = pressure;
  config.configured = true;
  if(!SaveConfig())
  {
    config = previous;
    return SendError(500, "Failed to save settings");
  }
  SendJson(200, "{\"status\":\"ok\"}");
  RequestRestart();
}

void SetupWebServer()
{
  WebServerInstance.on("/", HTTP_GET, SendStartPage);
  WebServerInstance.on("/settings", HTTP_GET, []() {
    WebServerInstance.sendHeader("Cache-Control", "no-store");
    WebServerInstance.send_P(200, "text/html; charset=utf-8", html_config);
  });
  WebServerInstance.on("/get", HTTP_GET, []() { SendJson(200, FormatSensorData()); });
  WebServerInstance.on("/status", HTTP_GET, SendSensorStatus);
  WebServerInstance.on("/get_config", HTTP_GET, []() {
    String json = "{\"ssid\":" + JsonString(config.wifi_ssid);
    json += ",\"password\":" + JsonString(config.wifi_password);
    json += ",\"sea_level_pressure\":" + String(config.sea_level_pressure, 2);
    json += ",\"language\":" + JsonString(config.language);
    json += ",\"ApMode\":" + String(ApMode ? "true" : "false") + "}";
    SendJson(200, json);
  });
  WebServerInstance.on("/save_config", HTTP_POST, HandleSaveConfig);
  WebServerInstance.on("/set_language", HTTP_POST, []() {
    if(RestartPending) return SendError(409, "Restart pending");
    const String language = WebServerInstance.arg("language");
    if(!IsValidLanguage(language)) return SendError(400, "Language must be en or ru");
    if(language != config.language)
    {
      const Config previous = config;
      strlcpy(config.language, language.c_str(), sizeof(config.language));
      if(!SaveConfig())
      {
        config = previous;
        return SendError(500, "Failed to save language");
      }
    }
    SendJson(200, "{\"status\":\"ok\",\"language\":" + JsonString(config.language) + "}");
  });
  WebServerInstance.on("/clear_config", HTTP_POST, []() {
    if(RestartPending) return SendError(409, "Restart pending");
    const Config previous = config;
    ResetConfig();
    if(!SaveConfig())
    {
      config = previous;
      return SendError(500, "Failed to clear settings");
    }
    SendJson(200, "{\"status\":\"ok\"}");
    RequestRestart();
  });
  // Android, Apple and Windows captive-portal detection.
  const char *portalPaths[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
    "/library/test/success.html", "/success.html", "/connecttest.txt", "/ncsi.txt", "/fwlink"};
  for(const char *route : portalPaths) WebServerInstance.on(route, HTTP_GET, SendStartPage);
  WebServerInstance.onNotFound([]() {
    if(ApMode && WebServerInstance.method() == HTTP_GET) return SendStartPage();
    SendError(404, "Not found");
  });
  WebServerInstance.begin();
}

void setup()
{
  Serial.begin(115200);
  Serial.println("ESP32 BME280 Meteostation starting");
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, STATUS_LED_OFF);
  EepromReady = EEPROM.begin(EEPROM_SIZE);
  if(!EepromReady) Serial.println("EEPROM unavailable; settings cannot be saved");
  LoadConfig();
  Wire.begin(BME_SDA_PIN, BME_SCL_PIN);
  Wire.setTimeOut(50);
  InitializeSensor();
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  if(config.configured)
  {
    WiFi.begin(config.wifi_ssid, config.wifi_password);
    WiFiDisconnectedAt = millis();
    LastReconnectAttempt = millis();
  }
  else StartAPMode();
  SetupWebServer();
}

void loop()
{
  if(RestartPending)
  {
    if(ApMode) DnsServerInstance.processNextRequest();
    WebServerInstance.handleClient();
    HandleStatusLED();
    if(millis() - RestartRequestedAt >= 1000UL) ESP.restart();
    delay(5);
    return;
  }
  HandleWiFi();
  if(ApMode) DnsServerInstance.processNextRequest();
  HandleSensor();
  WebServerInstance.handleClient();
  HandleStatusLED();
  delay(5);
}
