#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

char ssid[] = "";
char pass[] = "";

WebServer server(80);

#define SEALEVELPRESSURE_HPA (1020.00)

Adafruit_BME280 bme; // I2C
bool SensorStatus = false;

void WebServerLoop(void *pvParameters) {
  while(true) {
    server.handleClient();
  }
}

void handle_GetData() {
  server.send(200, "text/html", FormatHTML());
}

void handle_GetDataAPI() {
  server.send(200, "application/json", FormatSensorData());
}

void handle_NotFound() {
  server.send(200, "text/plain", "Not found");
}

String FormatHTML() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta http-equiv="Content-type" content="text/html; charset=utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>BLE280 Meteostation</title>
    <style>
        html {
            font-family: Helvetica;
            display: inline-block;
            margin: 0px auto;
            text-align: center;
        }
        body {
            margin-top: 50px;
        }
        h1 {
            color: #444444;
            margin: 50px auto 30px;
        }
        h2 {
            color: #444444;
            margin-bottom: 30px;
        }
        p {
            font-size: 18px;
            color: #555;
            margin: 10px 0;
        }
    </style>
</head>
<body>
    <h1>Метеостанция</h1> )rawliteral";

    if(SensorStatus)
    {
      html += "<h2>Показания датчика</h2>";
      html += "<p><strong>Температура:</strong> ";
      html += String(bme.readTemperature(), 0);
      html += " °C</p><p><strong>Давление:</strong> ";
      html += String((bme.readPressure() / 100.0f) * 0.750062f, 0);
      html += " мм рт. ст.</p><p><strong>Влажность:</strong> ";
      html += String(bme.readHumidity(), 0);
      html += " %</p>";
    }
    else
    {
      html += "<h2>Датчик BME280 не обнаружен!</h2>";
    }

    html += R"rawliteral(
</body>
</html>
)rawliteral";

    return html;
}

String FormatSensorData() {
  String jsonResponse = "{ \"status\": \"ok\", ";
  jsonResponse += "\"data\": \"";
  if(SensorStatus)
  {
    jsonResponse += "Температура: " + String(bme.readTemperature(), 0) + ", ";
    jsonResponse += "Влажность: " + String(bme.readHumidity(), 0);
  }
  else
  {
    jsonResponse += "Датчик BME280 не обнаружен!";
  }
  jsonResponse += "\" }";
  return jsonResponse;
}


void setup() {
  Serial.begin(115200);
  Serial.println("Start ESP32 BME280 Meteostation\n");

  xTaskCreatePinnedToCore(WebServerLoop, "WebServerLoop", 4096, NULL, 1, NULL, 1);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Serial.println("\nWi-Fi connected.");
  Serial.println("Server address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handle_GetData);
  server.on("/get", handle_GetDataAPI);
  server.onNotFound(handle_NotFound);
  server.begin();

  SensorStatus = bme.begin(0x76);
  while (!SensorStatus)
  {
    SensorStatus = bme.begin(0x76);
  }
}

void loop() {}