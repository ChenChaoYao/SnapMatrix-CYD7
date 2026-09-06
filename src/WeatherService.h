#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct WeatherData {
    bool valid = false;
    float temp = 0.0f;
    int humidity = 0;
    int code = 0;
    String city = "高雄";
    String city_en = "KAOHSIUNG";
    String desc_cn = "晴天";
    String desc_en = "CLEAR";
    float lat = 22.6148f;
    float lon = 120.3139f;
    unsigned long last_fetch = 0;
};

class WeatherService {
public:
    WeatherData data;

    void parseWmoCode(int code, String& cn, String& en) {
        switch (code) {
            case 0:  cn = "晴朗"; en = "CLEAR"; break;
            case 1:  cn = "晴時多雲"; en = "MAINLY CLEAR"; break;
            case 2:  cn = "多雲"; en = "PARTLY CLOUDY"; break;
            case 3:  cn = "陰天"; en = "OVERCAST"; break;
            case 45: 
            case 48: cn = "有霧"; en = "FOGGY"; break;
            case 51:
            case 53:
            case 55: cn = "毛毛雨"; en = "DRIZZLE"; break;
            case 61:
            case 63:
            case 65: cn = "下雨"; en = "RAIN"; break;
            case 71:
            case 73:
            case 75: cn = "下雪"; en = "SNOW"; break;
            case 80:
            case 81:
            case 82: cn = "陣雨"; en = "SHOWERS"; break;
            case 95:
            case 96:
            case 99: cn = "雷雨"; en = "THUNDERSTORM"; break;
            default: cn = "多雲"; en = "CLOUDY"; break;
        }
    }

    void fetch() {
        // 1. IP Geolocation (if not yet fetched)
        static bool geo_fetched = false;
        if (!geo_fetched) {
            HTTPClient http;
            http.setTimeout(3000);
            http.begin("http://ip-api.com/json");
            int code = http.GET();
            if (code == HTTP_CODE_OK) {
                DynamicJsonDocument doc(1024);
                if (!deserializeJson(doc, http.getString())) {
                    if (doc["status"] == "success") {
                        data.lat = doc["lat"].as<float>();
                        data.lon = doc["lon"].as<float>();
                        String city = doc["city"].as<String>();
                        if (city.length() > 0) {
                            data.city_en = city;
                            data.city_en.toUpperCase();
                            if (city == "Kaohsiung") data.city = "高雄";
                            else if (city == "Taipei") data.city = "台北";
                            else if (city == "New Taipei") data.city = "新北";
                            else if (city == "Taichung") data.city = "台中";
                            else if (city == "Tainan") data.city = "台南";
                            else if (city == "Taoyuan") data.city = "桃園";
                            else if (city == "Hsinchu") data.city = "新竹";
                            else data.city = city;
                            geo_fetched = true;
                        }
                    }
                }
            }
            http.end();
        }

        // 2. Open-Meteo current weather
        HTTPClient http;
        http.setTimeout(3000);
        String url = "http://api.open-meteo.com/v1/forecast?latitude=" + String(data.lat, 4) +
                     "&longitude=" + String(data.lon, 4) +
                     "&current=temperature_2m,relative_humidity_2m,weather_code";
        http.begin(url);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            DynamicJsonDocument doc(2048);
            if (!deserializeJson(doc, http.getString())) {
                if (doc.containsKey("current")) {
                    JsonObject cur = doc["current"];
                    data.temp = cur["temperature_2m"].as<float>();
                    data.humidity = cur["relative_humidity_2m"].as<int>();
                    data.code = cur["weather_code"].as<int>();
                    parseWmoCode(data.code, data.desc_cn, data.desc_en);
                    data.valid = true;
                    data.last_fetch = millis();
                    Serial.printf("[Weather] %s: %.1f°C, %d%%, %s (%s)\n",
                                  data.city.c_str(), data.temp, data.humidity,
                                  data.desc_cn.c_str(), data.desc_en.c_str());
                }
            }
        }
        http.end();
    }

    static void taskEntry(void* param) {
        WeatherService* service = (WeatherService*)param;
        service->fetch();
        vTaskDelete(NULL);
    }

    void triggerUpdateAsync() {
        xTaskCreate(taskEntry, "weather_task", 4096, this, 1, NULL);
    }
};
