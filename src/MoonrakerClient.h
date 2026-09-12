#pragma once
#include <Arduino.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

struct PrinterState {
    String gcode_state = "IDLE";
    String raw_gcode_state = "IDLE";
    String gcode_file = "";
    int mc_percent = 0;
    int mc_remaining_time = -1;
    int layer_num = 0;
    int total_layer_num = 0;
    int print_duration = 0; // minutes
    int raw_print_duration = 0; // seconds
    int nozzle_temper = 0;
    int nozzle_target_temper = 0;
    int bed_temper = 0;
    int bed_target_temper = 0;
    int chamber_temper = 0;
    int bed_mesh_point = 0;
    int bed_mesh_total = 0;
    String bed_mesh_state = "";
    String message = "";
    
    // Filament & Extruder info
    String active_extruder = "extruder";
    int extruder_temper[4] = {0, 0, 0, 0};
    int extruder_target[4] = {0, 0, 0, 0};
    String t_state[4] = {"", "", "", ""};
    String t_action[4] = {"", "", "", ""};
    String t_error[4] = {"", "", "", ""};
    uint32_t filament_rgb[4] = {0x000000, 0x2D9E59, 0xF4C032, 0xEB1919}; // RGB24: T1=Black, T2=Green, T3=Yellow, T4=True Red
    bool filament_loaded[4] = {true, true, true, true};
    String filament_type[4] = {"", "", "", ""};
    
    String filament_state = ""; // LOADING, UNLOADING, ERROR
    String filament_extruder = "";
    String filament_message = "";
    
    String machine_model = "Snapmaker U1";
    String rom_version = "";
    String klipper_version = "";
    
    int metadata_estimated_time = 0;
    bool is_connected = false;

    // Ideaformer & Standard Klipper extras
    String filament_name = "";
    float filament_used_m = 0.0f;
    int host_temper = 0;
    int estimated_total_layers = 0;

    // Motion & Extrusion Factors
    float extrude_factor = 1.0f;
    float speed_factor = 1.0f;

    // Probing & Tilt status
    String probe_status = "";
    String tilt_probe_step = "";

    // Snapmaker Machine State Manager
    int action_code = 0;
    int main_state = 0;

    // Unified Progress Cache
    float display_progress = 0.0f;
    float v_sd_progress = 0.0f;
    bool has_display_progress = false;
};

class MoonrakerClient {
public:
    String printer_ip = "192.168.1.50";
    uint16_t printer_port = 7125;
    String origin_header = "";
    WebSocketsClient ws;
    PrinterState state;
    SemaphoreHandle_t mutex;
    unsigned long last_retry = 0;

    MoonrakerClient() {
        mutex = xSemaphoreCreateMutex();
    }

    void begin(const String& ip, uint16_t port = 7125) {
        printer_ip = ip;
        printer_port = port;
        origin_header = "Origin: http://" + printer_ip + ":" + String(printer_port);
        ws.setExtraHeaders(origin_header.c_str());
        ws.begin(printer_ip.c_str(), printer_port, "/websocket", "");
        ws.onEvent([this](WStype_t type, uint8_t * payload, size_t length) {
            this->onWebSocketEvent(type, payload, length);
        });
        ws.setReconnectInterval(5000);
        ws.enableHeartbeat(15000, 3000, 2);
    }

    String printer_profile = "snapmaker_u1"; // "snapmaker_u1" or "ideaformer_ir3"
    bool pending_metadata_fetch = false;
    unsigned long last_sysinfo_fetch = 0;
    unsigned long last_ptc_fetch = 0;

    void resetState() {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
            bool conn = state.is_connected;
            state = PrinterState();
            state.is_connected = conn;
            if (printer_profile == "ideaformer_ir3") {
                state.machine_model = "Ideaformer IR3 V2";
            } else {
                state.machine_model = "Snapmaker U1";
            }
            xSemaphoreGive(mutex);
        }
    }

    void setPrinterProfile(const String& profile) {
        printer_profile = profile;
        resetState();
        if (state.is_connected) {
            sendSubscriptions();
            fetchSystemInfo();
            if (state.gcode_file.length() > 0) {
                fetchMetadata(state.gcode_file);
            }
        }
    }

    void loop() {
        ws.loop();
        if (state.is_connected) {
            if (pending_metadata_fetch && state.gcode_file.length() > 0) {
                pending_metadata_fetch = false;
                fetchMetadata(state.gcode_file);
            }
            if (printer_profile == "snapmaker_u1") {
                if (last_ptc_fetch == 0 || millis() - last_ptc_fetch >= 30000UL) {
                    last_ptc_fetch = millis();
                    fetchPrintTaskConfig();
                }
            }
            unsigned long sysinfo_interval = (state.rom_version.length() == 0) ? 5000UL : 60000UL;
            if (last_sysinfo_fetch == 0 || millis() - last_sysinfo_fetch >= sysinfo_interval) {
                last_sysinfo_fetch = millis();
                fetchSystemInfo();
            }
        }
    }

    void setPrinterIP(const String& ip) {
        if (ip.length() > 0) {
            printer_ip = ip;
            last_sysinfo_fetch = 0;
            resetState();
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50))) {
                state.is_connected = false;
                xSemaphoreGive(mutex);
            }
            ws.disconnect();
            begin(printer_ip, printer_port);
        }
    }

    void getState(PrinterState& outState) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50))) {
            outState = state;
            xSemaphoreGive(mutex);
        }
    }

    void sendSubscriptions() {
        String subMsg;
        if (printer_profile == "ideaformer_ir3") {
            // Ideaformer IR3 V2: 訂閱標準保證存在的通用物件，杜絕未宣告感測器造成的 404 崩潰
            subMsg = "{\"jsonrpc\":\"2.0\",\"method\":\"printer.objects.subscribe\",\"params\":{\"objects\":{"
                     "\"print_stats\":null,\"display_status\":null,\"toolhead\":null,\"gcode_move\":[\"extrude_factor\",\"speed_factor\"],"
                     "\"extruder\":null,\"heater_bed\":null,\"virtual_sdcard\":null}},\"id\":2}";
        } else {
            // Standard Snapmaker U1: 專屬 4 噴頭、腔體、進退料、動作碼、調平訂閱
            subMsg = "{\"jsonrpc\":\"2.0\",\"method\":\"printer.objects.subscribe\",\"params\":{\"objects\":{"
                     "\"print_stats\":null,\"display_status\":null,\"toolhead\":null,\"gcode_move\":[\"extrude_factor\",\"speed_factor\"],"
                     "\"extruder\":null,\"extruder1\":null,\"extruder2\":null,\"extruder3\":null,"
                     "\"heater_bed\":null,\"temperature_sensor cavity\":null,\"virtual_sdcard\":null,"
                     "\"print_task_config\":null,\"probe\":[\"status\"],\"auto_screws_tilt_adjust\":[\"probe_step\",\"current_point\"],"
                     "\"machine_state_manager\":[\"action_code\",\"main_state\"],"
                     "\"filament_feed left\":null,\"filament_feed right\":null,\"bed_mesh\":[\"progress\"]}},\"id\":2}";
        }
        ws.sendTXT(subMsg);
        Serial.printf("[Moonraker] Sent isolated subscription to %s:%d (Profile: %s)\n", printer_ip.c_str(), printer_port, printer_profile.c_str());

        if (printer_profile == "ideaformer_ir3") {
            queryIdeaformerSensors();
        }
    }

    void queryIdeaformerSensors() {
        if (printer_ip.length() == 0) return;
        HTTPClient http;
        String url = "http://" + printer_ip + ":" + String(printer_port) + "/printer/objects/list";
        http.begin(url);
        http.setTimeout(2500);
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(4096);
            if (!deserializeJson(doc, payload) && doc.containsKey("result") && doc["result"].containsKey("objects")) {
                JsonArray objs = doc["result"]["objects"];
                String subSensors = "{\"jsonrpc\":\"2.0\",\"method\":\"printer.objects.subscribe\",\"params\":{\"objects\":{";
                bool hasExtra = false;
                for (size_t i = 0; i < objs.size(); i++) {
                    String objName = objs[i].as<String>();
                    if (objName.startsWith("temperature_sensor") || objName.startsWith("temperature_host")) {
                        if (hasExtra) subSensors += ",";
                        subSensors += "\"" + objName + "\":null";
                        hasExtra = true;
                    }
                }
                subSensors += "}},\"id\":3}";
                if (hasExtra) {
                    ws.sendTXT(subSensors);
                    Serial.printf("[Moonraker] Dynamically subscribed to host sensors for Ideaformer: %s\n", subSensors.c_str());
                }
            }
        }
        http.end();
    }

    void fetchPrintTaskConfig() {
        if (printer_ip.length() == 0) return;
        HTTPClient http;
        String url = "http://" + printer_ip + ":" + String(printer_port) + "/printer/objects/query?print_task_config";
        http.begin(url);
        http.setTimeout(2500);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(4096);
            if (!deserializeJson(doc, payload)) {
                if (doc.containsKey("result") && doc["result"].containsKey("status")) {
                    JsonObject status = doc["result"]["status"];
                    if (status.containsKey("print_task_config")) {
                        JsonObject ptc = status["print_task_config"];
                        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
                            if (ptc.containsKey("filament_color_rgba")) {
                                JsonArray arr = ptc["filament_color_rgba"];
                                for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                                    String rgba = arr[i].as<String>();
                                    if (rgba.length() >= 6) {
                                        long hexVal = strtol(rgba.substring(0, 6).c_str(), NULL, 16);
                                        uint8_t r = (hexVal >> 16) & 0xFF;
                                        uint8_t g = (hexVal >> 8) & 0xFF;
                                        uint8_t b = hexVal & 0xFF;
                                        // Snapmaker palette calibration: map pinkish red preset (e.g. D81B60) to true vivid red
                                        if (r > 180 && g < 70 && b > 40 && b < 130) {
                                            r = 235; g = 25; b = 25;
                                        }
                                        state.filament_rgb[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                                    }
                                }
                            }
                            if (ptc.containsKey("filament_exist")) {
                                JsonArray arr = ptc["filament_exist"];
                                for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                                    state.filament_loaded[i] = arr[i].as<bool>();
                                }
                            }
                            if (ptc.containsKey("filament_type")) {
                                JsonArray arr = ptc["filament_type"];
                                for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                                    state.filament_type[i] = arr[i].as<String>();
                                }
                            }
                            xSemaphoreGive(mutex);
                        }
                    }
                }
            }
        }
        http.end();
    }

    static String urlEncode(const String& str) {
        String encoded = "";
        char c;
        for (size_t i = 0; i < str.length(); i++) {
            c = str.charAt(i);
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char code0 = "0123456789ABCDEF"[((uint8_t)c >> 4) & 0x0F];
                char code1 = "0123456789ABCDEF"[(uint8_t)c & 0x0F];
                encoded += '%';
                encoded += code0;
                encoded += code1;
            }
        }
        return encoded;
    }

    void fetchSystemInfo() {
        if (printer_ip.length() == 0) return;

        if (printer_profile == "ideaformer_ir3") {
            // Ideaformer IR3 V2 (Standard Klipper / Moonraker endpoint)
            HTTPClient http;
            String url = "http://" + printer_ip + ":" + String(printer_port) + "/printer/info";
            http.begin(url);
            http.setTimeout(2500);
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                DynamicJsonDocument doc(4096);
                if (!deserializeJson(doc, payload)) {
                    if (doc.containsKey("result")) {
                        JsonObject res = doc["result"];
                        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
                            state.machine_model = "Ideaformer IR3 V2";
                            if (res.containsKey("software_version")) {
                                String ver = res["software_version"].as<String>();
                                int dash = ver.indexOf('-');
                                state.rom_version = (dash > 0) ? ver.substring(0, dash) : ver;
                            }
                            xSemaphoreGive(mutex);
                        }
                    }
                }
            }
            http.end();
            return;
        }

        // Snapmaker U1
        HTTPClient http;
        String url = "http://" + printer_ip + ":" + String(printer_port) + "/machine/system_info";
        http.begin(url);
        http.setTimeout(2500);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(4096);
            if (!deserializeJson(doc, payload)) {
                if (doc.containsKey("result") && doc["result"].containsKey("system_info")) {
                    JsonObject sys = doc["result"]["system_info"];
                    if (sys.containsKey("product_info")) {
                        JsonObject prod = sys["product_info"];
                        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
                            if (prod.containsKey("machine_type")) state.machine_model = prod["machine_type"].as<String>();
                            if (prod.containsKey("firmware_version")) state.rom_version = "v" + prod["firmware_version"].as<String>();
                            else if (prod.containsKey("software_version")) state.rom_version = "v" + prod["software_version"].as<String>();
                            xSemaphoreGive(mutex);
                        }
                    }
                }
            }
        }
        http.end();

        // Fallback for standard Klipper/Moonraker if rom_version is still empty
        if (state.rom_version.length() == 0) {
            HTTPClient httpFallback;
            url = "http://" + printer_ip + ":" + String(printer_port) + "/server/info";
            httpFallback.begin(url);
            httpFallback.setTimeout(2000);
            httpCode = httpFallback.GET();
            if (httpCode == HTTP_CODE_OK) {
                String payload = httpFallback.getString();
                DynamicJsonDocument doc(4096);
                if (!deserializeJson(doc, payload)) {
                    if (doc.containsKey("result")) {
                        JsonObject res = doc["result"];
                        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
                            if (res.containsKey("moonraker_version")) {
                                state.rom_version = "v" + res["moonraker_version"].as<String>();
                            }
                            xSemaphoreGive(mutex);
                        }
                    }
                }
            }
            httpFallback.end();
        }
    }
private:
    void fetchMetadata(const String& filename) {
        if (filename.length() == 0) return;
        HTTPClient http;
        String url = "http://" + printer_ip + ":" + String(printer_port) + "/server/files/metadata?filename=" + urlEncode(filename);
        http.begin(url);
        http.setTimeout(2500);
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(8192);
            if (!deserializeJson(doc, payload)) {
                if (doc.containsKey("result")) {
                    JsonObject res = doc["result"];
                    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) {
                        if (res.containsKey("estimated_time")) {
                            state.metadata_estimated_time = res["estimated_time"].as<int>();
                        }
                        if (res.containsKey("filament_name")) {
                            state.filament_name = res["filament_name"].as<String>();
                        }
                        // Layer count calculation
                        if (res.containsKey("layer_count") && !res["layer_count"].isNull()) {
                            state.estimated_total_layers = res["layer_count"].as<int>();
                        } else if (res.containsKey("object_height") && res.containsKey("layer_height")) {
                            float objH = res["object_height"].as<float>();
                            float layerH = res["layer_height"].as<float>();
                            float firstLayerH = res.containsKey("first_layer_height") ? res["first_layer_height"].as<float>() : layerH;
                            if (objH > 0 && layerH > 0) {
                                state.estimated_total_layers = (int)round((objH - firstLayerH) / layerH) + 1;
                            }
                        }
                        if (state.total_layer_num == 0 && state.estimated_total_layers > 0) {
                            state.total_layer_num = state.estimated_total_layers;
                        }
                        xSemaphoreGive(mutex);
                        Serial.printf("[Moonraker] Metadata parsed: EstTime=%ds, Filament=%s, EstLayers=%d\n",
                                      state.metadata_estimated_time, state.filament_name.c_str(), state.estimated_total_layers);
                    }
                }
            }
        }
        http.end();
    }

    void onWebSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
        switch(type) {
            case WStype_DISCONNECTED:
                {
                    String reason = (payload && length > 0) ? String((const char*)payload, length) : "None";
                    Serial.printf("[Moonraker] Disconnected from %s:%d (Reason: %s)\n", printer_ip.c_str(), printer_port, reason.c_str());
                }
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50))) {
                    state.is_connected = false;
                    state.gcode_state = "IDLE";
                    xSemaphoreGive(mutex);
                }
                break;
            case WStype_CONNECTED:
                Serial.printf("[Moonraker] Connected to %s:%d!\n", printer_ip.c_str(), printer_port);
                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(50))) {
                    state.is_connected = true;
                    xSemaphoreGive(mutex);
                }
                sendSubscriptions();
                if (printer_profile == "snapmaker_u1") fetchPrintTaskConfig();
                fetchSystemInfo();
                if (state.gcode_file.length() > 0) fetchMetadata(state.gcode_file);
                break;
            case WStype_TEXT:
                {
                    DynamicJsonDocument doc(16384);
                    DeserializationError err = deserializeJson(doc, payload, length);
                    if (!err) {
                        if (doc.containsKey("result") && doc["result"].containsKey("status")) {
                            handleStatus(doc["result"]["status"].as<JsonObject>());
                        } else if (doc.containsKey("method") && doc["method"] == "notify_status_update") {
                            handleStatus(doc["params"][0].as<JsonObject>());
                        }
                    } else {
                        Serial.printf("[Moonraker] JSON parse error: %s (len: %u)\n", err.c_str(), (unsigned int)length);
                    }
                }
                break;
            case WStype_ERROR:
                Serial.printf("[Moonraker] WebSocket Error!\n");
                break;
            default:
                break;
        }
    }

    void handleStatus(JsonObject status) {
        if (!xSemaphoreTake(mutex, pdMS_TO_TICKS(100))) return;

        // 1. Common Klipper print_stats parsing
        if (status.containsKey("print_stats")) {
            JsonObject ps = status["print_stats"];
            if (ps.containsKey("filename")) {
                String newFile = ps["filename"].as<String>();
                if (newFile != state.gcode_file) {
                    state.gcode_file = newFile;
                    state.display_progress = 0.0f;
                    state.v_sd_progress = 0.0f;
                    state.has_display_progress = false;
                    state.mc_percent = 0;
                    state.metadata_estimated_time = 0;
                    state.estimated_total_layers = 0;
                    pending_metadata_fetch = true;
                } else if (state.metadata_estimated_time == 0 && state.gcode_file.length() > 0) {
                    pending_metadata_fetch = true;
                }
            }
            if (ps.containsKey("filament_used") && !ps["filament_used"].isNull()) {
                state.filament_used_m = ps["filament_used"].as<float>() / 1000.0f;
            }
            if (ps.containsKey("print_duration")) {
                state.raw_print_duration = ps["print_duration"].as<int>();
                state.print_duration = state.raw_print_duration / 60;
            }
            if (ps.containsKey("state")) {
                String s = ps["state"].as<String>();
                String newState = "IDLE";
                if (s == "standby") newState = "IDLE";
                else if (s == "printing") newState = "RUNNING";
                else if (s == "paused") newState = "PAUSE";
                else if (s == "complete") newState = "FINISH";
                else if (s == "cancelled") newState = "CANCELLED";
                else if (s == "error") newState = "FAILED";
                else if (s == "preparing") newState = "PREPARE";
                else { newState = s; newState.toUpperCase(); }

                if (newState == "RUNNING" && state.raw_gcode_state != "RUNNING") {
                    state.bed_mesh_point = 0;
                    state.bed_mesh_total = 0;
                    state.bed_mesh_state = "";
                    if (state.raw_print_duration < 10) {
                        state.display_progress = 0.0f;
                        state.v_sd_progress = 0.0f;
                        state.has_display_progress = false;
                        state.mc_percent = 0;
                    }
                }
                state.raw_gcode_state = newState;
            }
            if (ps.containsKey("info")) {
                JsonObject info = ps["info"];
                if (info.containsKey("current_layer") && !info["current_layer"].isNull()) {
                    state.layer_num = info["current_layer"].as<int>();
                }
                if (info.containsKey("total_layer") && !info["total_layer"].isNull()) {
                    state.total_layer_num = info["total_layer"].as<int>();
                }
            }
            // Fallback for estimated layers if total_layer is 0 or null
            if (state.total_layer_num == 0 && state.estimated_total_layers > 0) {
                state.total_layer_num = state.estimated_total_layers;
            }
            if (state.layer_num == 0 && state.total_layer_num > 0 && (state.raw_gcode_state == "RUNNING" || state.raw_gcode_state == "PAUSE")) {
                state.layer_num = (int)round((state.mc_percent / 100.0f) * state.total_layer_num);
                if (state.layer_num < 1 && state.mc_percent > 0) state.layer_num = 1;
            }
            if (ps.containsKey("message")) {
                String msg = ps["message"].as<String>();
                if (msg.indexOf("dirty bed") >= 0) msg = "FOREIGN OBJECT DETECTED ON BED";
                state.message = msg;
            }
        }

        if (status.containsKey("display_status")) {
            JsonObject ds = status["display_status"];
            if (ds.containsKey("progress")) {
                state.display_progress = ds["progress"].as<float>();
                if (state.display_progress > 0.001f) {
                    state.has_display_progress = true;
                }
            }
            if (ds.containsKey("message") && !ds["message"].isNull()) {
                String msg = ds["message"].as<String>();
                if (msg.indexOf("dirty bed") >= 0) msg = "FOREIGN OBJECT DETECTED ON BED";
                state.message = msg;
            }
        }

        if (status.containsKey("virtual_sdcard")) {
            JsonObject vsd = status["virtual_sdcard"];
            if (vsd.containsKey("progress")) {
                state.v_sd_progress = vsd["progress"].as<float>();
            }
        }

        // Unified progress calculation
        int new_percent = 0;
        if (state.has_display_progress) {
            new_percent = (int)round(state.display_progress * 100.0f);
        } else {
            new_percent = (int)round(state.v_sd_progress * 100.0f);
        }
        if (new_percent < 0) new_percent = 0;
        if (new_percent > 100) new_percent = 100;

        // Monotonic increase protection during active printing
        if (state.raw_gcode_state == "RUNNING" || state.raw_gcode_state == "PAUSE") {
            if (new_percent >= state.mc_percent || state.raw_print_duration < 10) {
                state.mc_percent = new_percent;
            }
        } else {
            state.mc_percent = new_percent;
        }

        // GCode Move factors (Extrude & Speed factors)
        if (status.containsKey("gcode_move")) {
            JsonObject gm = status["gcode_move"];
            if (gm.containsKey("extrude_factor")) state.extrude_factor = gm["extrude_factor"].as<float>();
            if (gm.containsKey("speed_factor")) state.speed_factor = gm["speed_factor"].as<float>();
        }

        // Bed Temperature (Common to both)
        if (status.containsKey("heater_bed")) {
            JsonObject hb = status["heater_bed"];
            if (hb.containsKey("temperature")) state.bed_temper = round(hb["temperature"].as<float>());
            if (hb.containsKey("target")) state.bed_target_temper = round(hb["target"].as<float>());
        }

        // =========================================================================
        // ISOLATED PROFILE LOGIC (Ideaformer IR3 vs Snapmaker U1)
        // =========================================================================
        if (printer_profile == "ideaformer_ir3") {
            // --- IDEA-FORMER IR3 V2: Single Extruder, Belt Klipper, Dynamic Host Sensors ---
            if (status.containsKey("extruder")) {
                JsonObject ex = status["extruder"];
                if (ex.containsKey("temperature")) {
                    state.extruder_temper[0] = round(ex["temperature"].as<float>());
                    state.nozzle_temper = state.extruder_temper[0];
                }
                if (ex.containsKey("target")) {
                    state.extruder_target[0] = round(ex["target"].as<float>());
                    state.nozzle_target_temper = state.extruder_target[0];
                }
            }
            // Clear secondary extruders
            for (int i = 1; i < 4; i++) {
                state.extruder_temper[i] = 0;
                state.extruder_target[i] = 0;
            }
            state.active_extruder = "extruder";

            // No cavity/chamber on IR3
            state.chamber_temper = 0;

            // Dynamically scan any temperature_sensor or temperature_host
            for (JsonPair kv : status) {
                String k = kv.key().c_str();
                if (k.startsWith("temperature_sensor") || k.startsWith("temperature_host")) {
                    JsonObject sens = kv.value().as<JsonObject>();
                    if (sens.containsKey("temperature")) {
                        state.host_temper = round(sens["temperature"].as<float>());
                    }
                }
            }

            // Clear Snapmaker specific AMS / leveling data to prevent pollution
            state.filament_state = "";
            state.filament_message = "";
            state.filament_extruder = "";
            for (int i = 0; i < 4; i++) {
                state.t_state[i] = "";
                state.t_action[i] = "";
                state.t_error[i] = "";
                state.filament_loaded[i] = false;
            }
            state.action_code = 0;
            state.main_state = 0;
            state.probe_status = "";
            state.tilt_probe_step = "";
            state.bed_mesh_state = "";
            state.bed_mesh_point = 0;
            state.bed_mesh_total = 0;

            // State evaluation for Ideaformer
            state.gcode_state = state.raw_gcode_state;
            if (state.gcode_state == "RUNNING") {
                if (state.bed_target_temper > 0 && state.bed_temper < state.bed_target_temper - 2) {
                    state.gcode_state = "HEATING_BED";
                } else if (state.nozzle_target_temper > 0 && state.nozzle_temper < state.nozzle_target_temper - 2) {
                    state.gcode_state = "HEATING_NOZZLE";
                } else if (state.raw_print_duration == 0) {
                    state.gcode_state = "PREPARE";
                }

                // Remaining time estimation
                if (state.metadata_estimated_time > 0) {
                    float prog = state.mc_percent / 100.0f;
                    float remSec = state.metadata_estimated_time * (1.0f - prog);
                    state.mc_remaining_time = (int)((remSec + 59.0f) / 60.0f);
                } else if (state.mc_percent > 0 && state.raw_print_duration > 5) {
                    float totalSec = (float)state.raw_print_duration / (state.mc_percent / 100.0f);
                    float remSec = totalSec - state.raw_print_duration;
                    state.mc_remaining_time = (int)((remSec + 59.0f) / 60.0f);
                } else {
                    state.mc_remaining_time = -1;
                }
            }
        } else {
            // --- SNAPMAKER U1: 4 Extruders, Cavity, Feeders, Leveling ---
            if (status.containsKey("toolhead")) {
                JsonObject th = status["toolhead"];
                if (th.containsKey("extruder")) {
                    state.active_extruder = th["extruder"].as<String>();
                }
            }

            const char* extKeys[4] = {"extruder", "extruder1", "extruder2", "extruder3"};
            for (int i = 0; i < 4; i++) {
                if (status.containsKey(extKeys[i])) {
                    JsonObject ex = status[extKeys[i]];
                    if (ex.containsKey("temperature")) state.extruder_temper[i] = round(ex["temperature"].as<float>());
                    if (ex.containsKey("target")) state.extruder_target[i] = round(ex["target"].as<float>());
                }
            }

            int activeIdx = 0;
            if (state.active_extruder == "extruder1") activeIdx = 1;
            else if (state.active_extruder == "extruder2") activeIdx = 2;
            else if (state.active_extruder == "extruder3") activeIdx = 3;

            state.nozzle_temper = state.extruder_temper[activeIdx];
            state.nozzle_target_temper = state.extruder_target[activeIdx];
            if (state.nozzle_target_temper == 0) {
                for (int i = 0; i < 4; i++) {
                    if (state.extruder_target[i] > 0) {
                        state.nozzle_temper = state.extruder_temper[i];
                        state.nozzle_target_temper = state.extruder_target[i];
                        break;
                    }
                }
            }

            if (status.containsKey("temperature_sensor cavity")) {
                JsonObject ct = status["temperature_sensor cavity"];
                if (ct.containsKey("temperature")) state.chamber_temper = round(ct["temperature"].as<float>());
            }

            if (status.containsKey("probe")) {
                JsonObject pb = status["probe"];
                if (pb.containsKey("status")) state.probe_status = pb["status"].as<String>();
            }

            if (status.containsKey("machine_state_manager")) {
                JsonObject msm = status["machine_state_manager"];
                if (msm.containsKey("action_code")) state.action_code = msm["action_code"].as<int>();
                if (msm.containsKey("main_state")) state.main_state = msm["main_state"].as<int>();
            }

            if (status.containsKey("auto_screws_tilt_adjust")) {
                JsonObject asta = status["auto_screws_tilt_adjust"];
                if (asta.containsKey("probe_step")) state.tilt_probe_step = asta["probe_step"].as<String>();
                if (asta.containsKey("current_point") && state.bed_mesh_point == 0) state.bed_mesh_point = asta["current_point"].as<int>();
            }

            if (status.containsKey("bed_mesh")) {
                JsonObject bm = status["bed_mesh"];
                if (bm.containsKey("progress")) {
                    JsonObject prog = bm["progress"];
                    if (prog.containsKey("probe_state")) state.bed_mesh_state = prog["probe_state"].as<String>();
                    if (prog.containsKey("current_point")) state.bed_mesh_point = prog["current_point"].as<int>();
                    if (prog.containsKey("total_points")) state.bed_mesh_total = prog["total_points"].as<int>();
                }
            }

            state.gcode_state = state.raw_gcode_state;

            // Probing / Leveling detection
            String bState = state.bed_mesh_state;
            bState.toLowerCase();
            String pStatus = state.probe_status;
            pStatus.toLowerCase();
            String tStep = state.tilt_probe_step;
            tStep.toLowerCase();
            String msgLower = state.message;
            msgLower.toLowerCase();

            bool is_probing = false;
            if ((state.action_code >= 310 && state.action_code <= 319) ||
                bState == "probing" || bState == "start" || bState == "measuring" || bState == "calibrating" || bState == "preheat") {
                is_probing = true;
            } else if (state.bed_mesh_total > 0 && state.bed_mesh_point > 0 && state.bed_mesh_point < state.bed_mesh_total && 
                       bState != "aborted" && bState != "finish" && bState != "ready" && bState != "idle") {
                is_probing = true;
            } else if (pStatus == "probing" || (tStep.length() > 0 && tStep != "adjust_idle" && tStep != "idle" && tStep != "ready")) {
                is_probing = true;
            } else if (msgLower.indexOf("leveling") >= 0 || msgLower.indexOf("bed_mesh") >= 0 || msgLower.indexOf("probe") >= 0 || msgLower.indexOf("調平") >= 0) {
                is_probing = true;
            }

            // Flow calibration detection
            bool is_calib_flow = false;
            if (state.action_code == 322 || (state.action_code >= 320 && state.action_code <= 329) ||
                msgLower.indexOf("flow") >= 0 || (msgLower.indexOf("calibrat") >= 0 && msgLower.indexOf("extru") >= 0) || msgLower.indexOf("流量") >= 0) {
                is_calib_flow = true;
            }

            if (is_probing && state.raw_gcode_state != "FAILED" && state.raw_gcode_state != "CANCELLED") {
                state.gcode_state = "LEVELING";
            } else if (is_calib_flow && state.raw_gcode_state != "FAILED" && state.raw_gcode_state != "CANCELLED") {
                state.gcode_state = "CALIBRATING_FLOW";
            } else if (state.gcode_state == "RUNNING") {
                if (state.bed_target_temper > 0 && state.bed_temper < state.bed_target_temper - 2) {
                    state.gcode_state = "HEATING_BED";
                } else if (state.nozzle_target_temper > 0 && state.nozzle_temper < state.nozzle_target_temper - 2) {
                    state.gcode_state = "HEATING_NOZZLE";
                } else if (state.raw_print_duration == 0) {
                    state.gcode_state = "PREPARE";
                }

                // Estimate remaining time
                if (state.metadata_estimated_time > 0) {
                    float prog = state.mc_percent / 100.0f;
                    float remSec = state.metadata_estimated_time * (1.0f - prog);
                    state.mc_remaining_time = (int)((remSec + 59.0f) / 60.0f);
                } else if (state.mc_percent > 0 && state.raw_print_duration > 5) {
                    float totalSec = (float)state.raw_print_duration / (state.mc_percent / 100.0f);
                    float remSec = totalSec - state.raw_print_duration;
                    state.mc_remaining_time = (int)((remSec + 59.0f) / 60.0f);
                } else {
                    state.mc_remaining_time = -1;
                }
            }

            // Filament feed status (T1~T4)
            if (status.containsKey("filament_feed left")) {
                JsonObject ffl = status["filament_feed left"];
                if (ffl.containsKey("extruder0")) {
                    if (ffl["extruder0"].containsKey("channel_state")) state.t_state[0] = ffl["extruder0"]["channel_state"].as<String>();
                    if (ffl["extruder0"].containsKey("channel_action_state")) state.t_action[0] = ffl["extruder0"]["channel_action_state"].as<String>();
                    if (ffl["extruder0"].containsKey("channel_error")) state.t_error[0] = ffl["extruder0"]["channel_error"].as<String>();
                }
                if (ffl.containsKey("extruder1")) {
                    if (ffl["extruder1"].containsKey("channel_state")) state.t_state[1] = ffl["extruder1"]["channel_state"].as<String>();
                    if (ffl["extruder1"].containsKey("channel_action_state")) state.t_action[1] = ffl["extruder1"]["channel_action_state"].as<String>();
                    if (ffl["extruder1"].containsKey("channel_error")) state.t_error[1] = ffl["extruder1"]["channel_error"].as<String>();
                }
            }

            if (status.containsKey("filament_feed right")) {
                JsonObject ffr = status["filament_feed right"];
                if (ffr.containsKey("extruder2")) {
                    if (ffr["extruder2"].containsKey("channel_state")) state.t_state[2] = ffr["extruder2"]["channel_state"].as<String>();
                    if (ffr["extruder2"].containsKey("channel_action_state")) state.t_action[2] = ffr["extruder2"]["channel_action_state"].as<String>();
                    if (ffr["extruder2"].containsKey("channel_error")) state.t_error[2] = ffr["extruder2"]["channel_error"].as<String>();
                }
                if (ffr.containsKey("extruder3")) {
                    if (ffr["extruder3"].containsKey("channel_state")) state.t_state[3] = ffr["extruder3"]["channel_state"].as<String>();
                    if (ffr["extruder3"].containsKey("channel_action_state")) state.t_action[3] = ffr["extruder3"]["channel_action_state"].as<String>();
                    if (ffr["extruder3"].containsKey("channel_error")) state.t_error[3] = ffr["extruder3"]["channel_error"].as<String>();
                }
            }

            // Filament Colors & Task config
            if (status.containsKey("print_task_config")) {
                JsonObject ptc = status["print_task_config"];
                if (ptc.containsKey("filament_color_rgba")) {
                    JsonArray arr = ptc["filament_color_rgba"];
                    for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                        String rgba = arr[i].as<String>();
                        if (rgba.length() >= 6) {
                            long hexVal = strtol(rgba.substring(0, 6).c_str(), NULL, 16);
                            uint8_t r = (hexVal >> 16) & 0xFF;
                            uint8_t g = (hexVal >> 8) & 0xFF;
                            uint8_t b = hexVal & 0xFF;
                            if (r > 180 && g < 70 && b > 40 && b < 130) {
                                r = 235; g = 25; b = 25;
                            }
                            state.filament_rgb[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                        }
                    }
                } else if (ptc.containsKey("filament_color")) {
                    JsonArray arr = ptc["filament_color"];
                    for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                        uint32_t c = arr[i].as<uint32_t>();
                        uint8_t r = (c >> 16) & 0xFF;
                        uint8_t g = (c >> 8) & 0xFF;
                        uint8_t b = c & 0xFF;
                        if (r > 180 && g < 70 && b > 40 && b < 130) {
                            r = 235; g = 25; b = 25;
                        }
                        state.filament_rgb[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                    }
                }
                if (ptc.containsKey("filament_exist")) {
                    JsonArray arr = ptc["filament_exist"];
                    for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                        state.filament_loaded[i] = arr[i].as<bool>();
                    }
                }
                if (ptc.containsKey("filament_type")) {
                    JsonArray arr = ptc["filament_type"];
                    for (size_t i = 0; i < 4 && i < arr.size(); i++) {
                        state.filament_type[i] = arr[i].as<String>();
                    }
                }
            }

            // Determine if any active loading/unloading
            state.filament_state = "";
            for (int i = 0; i < 4; i++) {
                String s = state.t_state[i];
                String a = state.t_action[i];
                String extName = "T" + String(i + 1);

                if ((s == "load_fail" || s == "unload_fail" || a == "load_fail" || a == "unload_fail") &&
                    state.t_error[i] != "ok" && state.t_error[i] != "none" && state.t_error[i].length() > 0) {
                    state.filament_state = "ERROR";
                    state.filament_extruder = extName;
                    state.filament_message = extName + " - " + state.t_error[i];
                    state.filament_message.toUpperCase();
                    break;
                }

                bool is_unloading = (s == "unloading" || s == "pre_unload" || a == "unloading" || a == "pre_unload" || 
                                    ((s.indexOf("unload") >= 0) && (s.indexOf("finish") < 0)) || 
                                    ((a.indexOf("unload") >= 0) && (a.indexOf("finish") < 0)));
                bool is_loading = !is_unloading && (s == "loading" || s == "pre_load" || a == "loading" || a == "pre_load" || 
                                    ((s.indexOf("load") >= 0) && (s.indexOf("finish") < 0)) || 
                                    ((a.indexOf("load") >= 0) && (a.indexOf("finish") < 0)));

                if (is_unloading) {
                    state.filament_state = "UNLOADING";
                    state.filament_extruder = extName;
                    state.filament_message = extName + " - UNLOADING";
                    break;
                } else if (is_loading) {
                    state.filament_state = "LOADING";
                    state.filament_extruder = extName;
                    state.filament_message = extName + " - LOADING";
                    break;
                }
            }

            if (state.gcode_state == "RUNNING") {
                state.filament_state = "";
            }
        }

        xSemaphoreGive(mutex);
    }
};
