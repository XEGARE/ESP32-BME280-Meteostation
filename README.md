Это исходный код для прошивки микроконтроллера ESP32, который предназначен для считывания погоды за окном с помощью датчика BME280.

Можно интегрировать с Алисой от Яндекса через навык "Домовёнок Кузя".

**Схемы для пайки и нужные пины для вашего варианта микроконтроллера смотрите в интернете.**

## Подготовка

### 1. Установка библиотек
Требуется установить две библиотеки:
1. Adafruit BME280 Library
2. Adafruit Unifed Sensor

### 2. Настройка Wi-Fi
Для подключения устройства к Wi-Fi сети измените эти строки:

```cpp
char ssid[] = "";  // Название вашей сети Wi-Fi (SSID)
char pass[] = "";  // Пароль от сети Wi-Fi
```

### 3. Изменение порта сервера
При необходимости изменить порт ищите эту строку:

```cpp
WebServer server(80);  // Порт 80 по умолчанию
```

### 4. Изменить давление над уровнем моря
Воспользуйтесь этим [сервисом](https://zoom.earth/maps/pressure/) и измените значение для вашей местности в этой строке:

```cpp
#define SEALEVELPRESSURE_HPA (1020.00)
```

## Использование

***{IP}*** - адрес устройства в локальной сети
***{PORT}*** - по умолчанию **80**

1. HTML страница: http://{IP}:{PORT}/  
![page](https://github.com/user-attachments/assets/b01ec3f2-9203-4d7a-b5c1-6f2ca6969b5f)
2. JSON ответ для Алисы: http://{IP}:{PORT}/get  
![json](https://github.com/user-attachments/assets/13058ea8-4044-41d7-8a72-0d1b4d268032)
