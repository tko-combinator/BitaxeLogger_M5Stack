#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <time.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// NVS情報
Preferences preferences;

// WiFi接続情報
char ssid[50];
char password[100];

// NTP情報
#define GMT_OFFSET_SEC 3600 * 9 // JST
#define SUMMER_TIME_OFFSET 0
char NTP_url[100];

// InfluxDB接続情報
char InfluxDB_token[100];
char InfluxDB_org[50];
char InfluxDB_bucket[50];
char InfluxDB_url[100];

// Bitaxe API情報
char Bitaxe_url[100];

// BM18B20接続情報
#define ONE_WIRE_BUS 26
#define MAX_SENSORS 5
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DeviceAddress sensorAddresses[MAX_SENSORS];

// SDカード書き込み情報
#define SDCARD_CHIP_SELECT 4

struct TempData
{
    float Q1;
    float Q2;
    float L;
};

struct BitaxeInfo
{
    float power;
    float voltage;
    float current;
    float temp;
    float hashRate;
};

bool setConfig()
{
    File file = SD.open("/config.json", FILE_READ);

    if (!file)
    {
        Serial.println("Failed to open config.json. Initializing from NVM...");

        if (preferences.begin("config", true))
        { 
            strlcpy(ssid, preferences.getString("ssid", "").c_str(), sizeof(ssid));
            strlcpy(password, preferences.getString("password", "").c_str(), sizeof(password));
            strlcpy(NTP_url, preferences.getString("NTP_url", "").c_str(), sizeof(NTP_url));
            strlcpy(Bitaxe_url, preferences.getString("Bitaxe_url", "").c_str(), sizeof(Bitaxe_url));
            strlcpy(InfluxDB_url, preferences.getString("InfluxDB_url", "").c_str(), sizeof(InfluxDB_url));
            strlcpy(InfluxDB_org, preferences.getString("InfluxDB_org", "").c_str(), sizeof(InfluxDB_org));
            strlcpy(InfluxDB_bucket, preferences.getString("InfluxDB_bucket", "").c_str(), sizeof(InfluxDB_bucket));
            strlcpy(InfluxDB_token, preferences.getString("InfluxDB_token", "").c_str(), sizeof(InfluxDB_token));

            preferences.end(); 
            return true;

        } else {
            Serial.println("Failed to initialize NVM.");
            return false;
        }
    }
    else
    {
        Serial.println("config.json opened successfully");
        String jsonString;
        while (file.available())
        {
            jsonString += char(file.read());
        }

        file.close();
        // Serial.println(jsonString);
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonString);

        if (error)
        {
            Serial.println("Failed to parse JSON");
            Serial.println(error.c_str());
            return false;
        }

        Serial.println("JSON parsing succeeded!");
        strlcpy(ssid, doc["wifi"]["ssid"], sizeof(ssid));
        strlcpy(password, doc["wifi"]["password"], sizeof(password));
        strlcpy(NTP_url, doc["ntp"]["url"], sizeof(NTP_url));
        strlcpy(Bitaxe_url, doc["bitaxe"]["url"], sizeof(Bitaxe_url));
        strlcpy(InfluxDB_url, doc["influxdb"]["url"], sizeof(InfluxDB_url));
        strlcpy(InfluxDB_org, doc["influxdb"]["org"], sizeof(InfluxDB_org));
        strlcpy(InfluxDB_bucket, doc["influxdb"]["bucket"], sizeof(InfluxDB_bucket));
        strlcpy(InfluxDB_token, doc["influxdb"]["token"], sizeof(InfluxDB_token));

        preferences.begin("config", false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.putString("NTP_url", NTP_url);
        preferences.putString("Bitaxe_url", Bitaxe_url);
        preferences.putString("InfluxDB_url", InfluxDB_url);
        preferences.putString("InfluxDB_org", InfluxDB_org);
        preferences.putString("InfluxDB_bucket", InfluxDB_bucket);
        preferences.putString("InfluxDB_token", InfluxDB_token);
        preferences.end();

        Serial.println("Data written to NVM from config.json");
        return true;
    }
}

void printError(String message)
{
    message = "Error: " + message;
    Serial.println(message);
}

void LcdInit()
{
    M5.Display.clear();
    M5.Display.setTextSize(3);
    M5.Display.setCursor(10, 10);
    M5.Lcd.fillScreen(WHITE);
    M5.Lcd.setTextColor(BLACK, WHITE);
    M5.Display.println("LCD: OK");
}

void WiFiInit()
{
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected.");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void WiFiReconnect()
{
}

void WiFiError()
{
    printError("WiFi not connected");
    WiFiReconnect();
}

void initNTP()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        configTime(GMT_OFFSET_SEC, SUMMER_TIME_OFFSET, NTP_url);
        Serial.println("Time synchronized with NTP server.");
    }
    else
    {
        WiFiError();
    }
}

String getLocalTime_NTP()
{
    tm timeData;

    if (WiFi.status() == WL_CONNECTED)
    {
        if (!getLocalTime(&timeData))
        {
            Serial.println("Failed to obtain time");
            return "00-00-00 00:00:00";
        }

        Serial.println(&timeData, "%Y-%m-%d %H:%M:%S");
        char timeBuf[20];
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeData);
        return String(timeBuf);
    }
    else
    {
        WiFiError();
    }
}

BitaxeInfo getBitaxeInfo()
{
    BitaxeInfo info;

    if (WiFi.status() == WL_CONNECTED)
    {
        HTTPClient http;
        String url = String(Bitaxe_url) + "/system/info";
        http.begin(url);

        int httpResponseCode = http.GET();
        if (httpResponseCode > 0)
        {
            String response = http.getString();
            Serial.println("Bitaxe API Response:");
            Serial.println(response);
            StaticJsonDocument<1024> data;
            DeserializationError error = deserializeJson(data, response);

            if (error)
            {
            }

            info.power = data["power"];
            info.voltage = data["voltage"];
            info.current = data["current"];
            info.temp = data["temp"];
            info.hashRate = data["hashRate"];

            return info;
        }
        else
        {
            Serial.print("Error on HTTP request: ");
            Serial.println(httpResponseCode);
        }

        http.end();
    }
    else
    {
        WiFiError();
    }
}

void _storeInfluxdb(TempData tempData, BitaxeInfo bitaxeInfo)
{
    HTTPClient http;
    http.setTimeout(5000);

    String url = String(InfluxDB_url) + "/write" + "?org=" + InfluxDB_org + "&bucket=" + InfluxDB_bucket + "&precision=s";

    http.begin(url.c_str());
    http.addHeader("Authorization", String("Token ") + InfluxDB_token);
    http.addHeader("Content-Type", "text/plain");

    String payload = "BitaxeLogging,device=bitaxe401Supra001 ";
    payload += "Q1=" + String(tempData.Q1) + ",";
    payload += "Q2=" + String(tempData.Q2) + ",";
    payload += "L=" + String(tempData.L) + ",";
    payload += "power=" + String(bitaxeInfo.power) + ",";
    payload += "voltage=" + String(bitaxeInfo.voltage) + ",";
    payload += "current=" + String(bitaxeInfo.current) + ",";
    payload += "temp=" + String(bitaxeInfo.temp) + ",";
    payload += "hashRate=" + String(bitaxeInfo.hashRate);

    Serial.println("URL for InfluxDB:");
    Serial.println(url);

    Serial.println("Payload for InfluxDB:");
    Serial.println(payload);

    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0)
    {
        String response = http.getString();
        Serial.println("Response Code: " + String(httpResponseCode));
        Serial.println("Response Body: " + response);
    }
    else
    {
        Serial.print("Error on sending POST: ");
        Serial.println(httpResponseCode);
    }

    http.end();
}

void storeDB(TempData tempData, BitaxeInfo bitaxeInfo)
{
    if (WiFi.status() == WL_CONNECTED)
    {
        _storeInfluxdb(tempData, bitaxeInfo);
    }
    else
    {
        WiFiError();
    }
}

void storeSDCARD(TempData tempData, BitaxeInfo bitaxeInfo)
{
    File file = SD.open("/log.csv", FILE_APPEND);
    String localTime = getLocalTime_NTP();

    if (file)
    {
        file.print(localTime);
        file.print(",");
        file.print(bitaxeInfo.power);
        file.print(",");
        file.print(bitaxeInfo.voltage);
        file.print(",");
        file.print(bitaxeInfo.current);
        file.print(",");
        file.print(bitaxeInfo.temp);
        file.print(",");
        file.print(bitaxeInfo.hashRate);
        file.print(",");
        file.print(tempData.Q1);
        file.print(",");
        file.print(tempData.Q2);
        file.print(",");
        file.print(tempData.L);
        file.println();
        file.close();
        Serial.println("Data written to SD card.");
    }
    else
    {
        Serial.println("Error opening log.csv");
    }
}

void _printAddress_BM18B20(DeviceAddress deviceAddress)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        if (deviceAddress[i] < 16)
            Serial.print("0");
        Serial.print(deviceAddress[i], HEX);
    }
    Serial.println();
}

int _init_BM18B20()
{
    sensors.begin();
    int deviceCount = sensors.getDeviceCount();
    Serial.print("Found ");
    Serial.print(deviceCount);
    Serial.println(" BM18B20 sensors.");

    for (int i = 0; i < deviceCount; i++)
    {
        if (sensors.getAddress(sensorAddresses[i], i))
        {
            Serial.print("Number: ");
            Serial.println(i + 1);
            Serial.print("Address: ");
            _printAddress_BM18B20(sensorAddresses[i]);
        }
        else
        {
            Serial.print("Unable to find address for sensor ");
            Serial.println(i + 1);
        }
    }
    return deviceCount;
}

float _getTemp_BM18B20(DeviceAddress sensorAddress)
{
    sensors.requestTemperaturesByAddress(sensorAddress);
    float temp = sensors.getTempC(sensorAddress);
    if (temp == DEVICE_DISCONNECTED_C)
    {
        Serial.println("Error: Senser falt..");
        return NAN;
    }
    Serial.println(temp);
    return temp;
}

void initTempSensers()
{
    _init_BM18B20();
}

TempData getTempData()
{
    TempData data;
    data.Q1 = 25.3;
    data.Q2 = 26.1;
    data.L = 45.0;
    return data;
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    SD.begin(SDCARD_CHIP_SELECT);
    Serial.begin(115200);
    Serial.println("M5Stack nitialized.");
    setConfig();
    initTempSensers();
    LcdInit();
    WiFiInit();
    M5.Display.println("WiFi: OK");
    initNTP();
    M5.Display.println("NTP: OK");
}

void loop()
{
    M5.update();
    BitaxeInfo bitaxeInfo = getBitaxeInfo();
    TempData tempData = getTempData();
    storeDB(tempData, bitaxeInfo);
    storeSDCARD(tempData, bitaxeInfo);
    delay(30000);
}
