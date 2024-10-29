#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_MCP9600.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define GROVE_ADDRESS 0x60

// NVS情報
Preferences preferences;

// WiFi接続情報
char ssid[50];
char password[100];
bool tryWiFiReconnect;
const int maxWifiReconnect = 5;

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

// MCP9600接続情報
Adafruit_MCP9600 mcp;
#define MCP9600_ADDRESS_Q1 0x60
#define MCP9600_ADDRESS_Q2 0x61
#define MCP9600_ADDRESS_L 0x62

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
    bool isValid;
};

void HLT()
{
    Serial.println("Entering HLT...");
    M5.Lcd.clear(WHITE);
    M5.Lcd.setCursor(10, 50);
    M5.Lcd.println("Entering HLT...");
    M5.Lcd.println("Please restart.");
    while (1)
    {
        delay(1000);
    }
}

void printError(const char *errorMessage, const char *errorType = "General Error")
{
    Serial.print(errorType);
    Serial.print(": ");
    Serial.println(errorMessage);
}

void setConfig()
{
    File file = SD.open("/config.json", FILE_READ);
    const char *errorType = "Config Error";

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
        }
        else
        {
            printError("Failed to initialize NVM.", errorType);
            HLT();
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
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, jsonString);

        if (error)
        {
            printError("Failed to parse JSON", errorType);
            Serial.println(error.c_str());
            HLT();
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
    }
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

void handleWifiReconection()
{
    if (!tryWiFiReconnect)
    {
        return;
    }

    int attemptCount = 0;

    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("Attempting WiFi reconnection...");
        WiFi.begin(ssid, password);

        while ((WiFi.status() != WL_CONNECTED) && attemptCount < maxWifiReconnect)
        {
            delay(1000);
            Serial.print("Reconnect attempt ");
            Serial.print(attemptCount + 1);
            Serial.println("...");
            attemptCount++;
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("WiFi reconnected successfully!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
        }
        else
        {
            printError("Failed to reconnect to WiFi.", "Wifi Error");
            Serial.println("Continuing with the next operation...");
            tryWiFiReconnect = false;
        }
    }
    else
    {
        Serial.println("WiFi is already connected.");
    }
    Serial.println("\nConnected to WiFi!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
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
        printError("Failed to initialize NTP: No network connection", "NTP Error");
        handleWifiReconection();
        HLT();
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
        printError("Cannot retrieve local time: No network connection", "NTP Error");
        handleWifiReconection();
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
                printError(error.c_str(), "JSON Deserialization Error");
                info.isValid = false;
                info.power = -1;
                info.voltage = -1;
                info.current = -1;
                info.temp = -1;
                info.hashRate = -1;

                return info;
            }

            info.power = data["power"];
            info.voltage = data["voltage"];
            info.current = data["current"];
            info.temp = data["temp"];
            info.hashRate = data["hashRate"];
            info.isValid = true;

            return info;
        }
        else
        {
            Serial.println(Serial.println(httpResponseCode));
        }

        http.end();
    }
    else
    {
        handleWifiReconection();
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
        printError("Error on sending POST", "HTTP Error");
        Serial.println("Response Code: " + String(httpResponseCode));
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
        handleWifiReconection();
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
        printError("Error opening log.csv", "File Open Error");
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
    return temp;
}

void _init_MCP9600()
{
    if (!mcp.begin(GROVE_ADDRESS))
    {
        printError("Failed to initialize MCP9600. Please check the connection.", "MCP9600 Initialization Error");
    }
}

float _getTemp_MCP9600()
{
    float temp = mcp.readThermocouple();
    if (isnan(temp))
    {
        printError("Failed to read temperature from MCP9600.", "Temperature Read Error");
        return NAN;
    }
    return temp;
}

void initTempSensers()
{
    _init_BM18B20();
    _init_MCP9600();
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
    tryWiFiReconnect = true;
    float temperature = _getTemp_MCP9600();
    if (!isnan(temperature))
    {
        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" C");
    }
    BitaxeInfo bitaxeInfo = getBitaxeInfo();
    TempData tempData = getTempData();
    if (bitaxeInfo.isValid)
    {
        storeDB(tempData, bitaxeInfo);
        storeSDCARD(tempData, bitaxeInfo);
    }
    else
    {
        printError("Failed to retrieve BitaxeInfo.");
    }
    delay(30000);
}
