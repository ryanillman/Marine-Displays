#include "signalk_config.h"
#include "signalk_host.h"
#include <new>
#include "network_setup.h"
#include "screen_config_c_api.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "esp_task_wdt.h"
#include <esp_heap_caps.h>
#include <map>
#include <set>
#include <vector>

extern "C" int ui_get_current_screen(void);

// STL allocator that places all nodes in PSRAM instead of iRAM.
template <typename T>
struct PsramStlAllocator {
    using value_type = T;
    PsramStlAllocator() = default;
    template <class U> PsramStlAllocator(const PsramStlAllocator<U>&) noexcept {}
    T* allocate(std::size_t n) {
        void* p = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!p) throw std::bad_alloc();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept { heap_caps_free(p); }
};
template <class T, class U>
bool operator==(const PsramStlAllocator<T>&, const PsramStlAllocator<U>&) { return true; }
template <class T, class U>
bool operator!=(const PsramStlAllocator<T>&, const PsramStlAllocator<U>&) { return false; }

template <typename K, typename V>
using PsramMap = std::map<K, V, std::less<K>,
    PsramStlAllocator<std::pair<const K, V>>>;

// Custom ArduinoJson allocator that uses PSRAM instead of internal RAM.
// Saves ~4 KB of iRAM on every SK WebSocket message parse.
struct PsramAllocator {
    void* allocate(size_t size) {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    void deallocate(void* ptr) {
        heap_caps_free(ptr);
    }
    void* reallocate(void* ptr, size_t new_size) {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
};

// Empty until a finite sample arrives. A seeded 0 RPM or 40°C is a reading.
float g_sensor_values[TOTAL_PARAMS] = {
    NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN
};

enum { SLOT_EMPTY = 0, SLOT_GOOD = 1, SLOT_BAD = 2 };

static uint8_t g_slot_state[TOTAL_PARAMS] = {0};
static uint32_t g_slot_ms[TOTAL_PARAMS] = {0};

struct PathSample {
    float value;
    uint8_t state;
    uint32_t ms;
};

// Mutex for thread-safe access to sensor variables
SemaphoreHandle_t sensor_mutex = NULL;

// Metadata storage for each parameter
String g_sensor_units[TOTAL_PARAMS];
String g_sensor_descriptions[TOTAL_PARAMS];

// Navigation globals for POSITION/COMPASS display types
volatile float g_nav_latitude  = NAN;
volatile float g_nav_longitude = NAN;
char g_nav_datetime[32]        = {0};
char g_sk_datetime[32]         = {0};  // SK writes here; RTC sync reads it

// Extended storage for paths beyond the gauge array (number displays, dual displays)
// Uses PSRAM allocator to keep map nodes out of iRAM.
static PsramMap<String, PathSample> extended_sensor_values;
static PsramMap<String, String> extended_sensor_units;
static PsramMap<String, String> extended_sensor_descriptions;

// WiFi and HTTP client (static to this file)
static WebSocketsClient ws_client;
static String server_ip_str = "";
static uint16_t server_port_num = 0;
static String signalk_paths[TOTAL_PARAMS];  // Array of 10 paths
static TaskHandle_t signalk_task_handle = NULL;
static bool signalk_enabled = false;

// Set by HTTP handler (Core 1) before building/sending the config page.
// signalk_task (Core 0) sees this, disconnects the WS, and suspends reconnects
// until the flag is cleared on save — freeing the ~22KB WS receive buffer.
static volatile bool g_signalk_ws_paused = false;
// Set from the socket task when the stream is up. A quiet period:0 value
// stays good while this is true. Disconnect, pause, and Wi-Fi loss clear it.
static volatile bool g_link_up = false;
static volatile uint8_t g_nav_state = SLOT_EMPTY;
static volatile uint32_t g_nav_ms = 0;

// Set by resume_signalk_ws() to tell signalk_task to reconnect once iRAM > 18KB.
// signalk_task clears both this and g_signalk_ws_paused when the threshold is met.
static volatile bool g_signalk_ws_resume_when_ready = false;

// Connection health and reconnection/backoff state
static unsigned long last_message_time = 0;
static unsigned long last_reconnect_attempt = 0;
static unsigned long next_reconnect_at = 0;
static unsigned long current_backoff_ms = 2000; // start 2s
static const unsigned long RECONNECT_BASE_MS = 2000;
static const unsigned long RECONNECT_MAX_MS = 60000;
static const unsigned long MESSAGE_TIMEOUT_MS = 30000; // 30s without traffic we sent or received
static const unsigned long PING_INTERVAL_MS = 15000; // send periodic ping

// Forward declaration for active-screen path collection
static std::vector<String> get_active_screen_paths(int screen_1based);

// Outgoing message queue (simple ring buffer)
static SemaphoreHandle_t ws_queue_mutex = NULL;
static const int OUTGOING_QUEUE_SIZE = 8;
static String outgoing_queue[OUTGOING_QUEUE_SIZE];
static int queue_head = 0;
static int queue_tail = 0;
static int queue_count = 0;

static bool enqueue_outgoing(const String &msg) {
    if (ws_queue_mutex == NULL) return false;
    if (xSemaphoreTake(ws_queue_mutex, pdMS_TO_TICKS(100))) {
        if (queue_count >= OUTGOING_QUEUE_SIZE) {
            // Drop oldest to make room
            queue_head = (queue_head + 1) % OUTGOING_QUEUE_SIZE;
            queue_count--;
        }
        outgoing_queue[queue_tail] = msg;
        queue_tail = (queue_tail + 1) % OUTGOING_QUEUE_SIZE;
        queue_count++;
        xSemaphoreGive(ws_queue_mutex);
        return true;
    }
    return false;
}

static void flush_outgoing() {
    if (ws_queue_mutex == NULL) return;
    if (!ws_client.isConnected()) return;
    if (!xSemaphoreTake(ws_queue_mutex, pdMS_TO_TICKS(100))) return;
    while (queue_count > 0 && ws_client.isConnected()) {
        String &m = outgoing_queue[queue_head];
        ws_client.sendTXT(m);
        queue_head = (queue_head + 1) % OUTGOING_QUEUE_SIZE;
        queue_count--;
    }
    xSemaphoreGive(ws_queue_mutex);
}

// Public enqueue wrapper (declared in header)
void enqueue_signalk_message(const String &msg) {
    if (ws_queue_mutex == NULL) return;
    enqueue_outgoing(msg);
}

// Convert dot-delimited Signal K path to REST URL form
static String build_signalk_url(const String &path) {
    String cleaned = path;
    cleaned.trim();
    cleaned.replace(".", "/");
    return String("/signalk/v1/api/vessels/self/") + cleaned;
}

static bool signalk_source_live_unlocked() {
    if (!signalk_enabled) return false;
    if (g_signalk_ws_paused) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    return g_link_up;
}

static Sample sample_from_locked(float value, uint8_t state, uint32_t stamp, bool locked) {
    Sample sample;
    sample.value = NAN;
    sample.age_ms = 0;
    sample.valid = false;
    if (!locked) return sample;
    if (!signalk_source_live_unlocked()) return sample;
    if (state != SLOT_GOOD || !isfinite(value)) return sample;
    sample.value = value;
    sample.age_ms = millis() - stamp;
    sample.valid = true;
    return sample;
}

// Thread-safe getter for any sensor value. NAN when that slot has no live sample.
float get_sensor_value(int index) {
    Sample sample = get_sensor_sample(index);
    return sample.valid ? sample.value : NAN;
}

// Thread-safe setter for any sensor value
static void store_slot_locked(int index, float value, bool good);

void set_sensor_value(int index, float value) {
    if (index < 0 || index >= TOTAL_PARAMS) return;

    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        store_slot_locked(index, value, isfinite(value));
        xSemaphoreGive(sensor_mutex);
    }
}

// Metadata getters (thread-safe)
String get_sensor_unit(int index) {
    if (index < 0 || index >= TOTAL_PARAMS) return "";
    String unit = "";
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        unit = g_sensor_units[index];
        xSemaphoreGive(sensor_mutex);
    }
    return unit;
}

String get_sensor_description(int index) {
    if (index < 0 || index >= TOTAL_PARAMS) return "";
    String desc = "";
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        desc = g_sensor_descriptions[index];
        xSemaphoreGive(sensor_mutex);
    }
    return desc;
}

void set_sensor_metadata(int index, const char* unit, const char* description) {
    if (index < 0 || index >= TOTAL_PARAMS) return;
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        if (unit) g_sensor_units[index] = String(unit);
        if (description) g_sensor_descriptions[index] = String(description);
        xSemaphoreGive(sensor_mutex);
    }
}

// Get sensor value by path (for number and dual displays that may use non-gauge paths)
float get_sensor_value_by_path(const String& path) {
    Sample sample = get_sensor_sample_by_path(path);
    return sample.valid ? sample.value : NAN;
}

// Get sensor unit by path
String get_sensor_unit_by_path(const String& path) {
    if (path.length() == 0) return "";
    
    // Gauge-slot strings win on the 4". Rounds keep only the extended map,
    // including when the path is also a gauge slot.
    if (signalk_host().gauge_slot_units) {
        for (int i = 0; i < TOTAL_PARAMS; i++) {
            if (signalk_paths[i] == path) {
                return get_sensor_unit(i);
            }
        }
    }
    
    // Check extended storage
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        auto it = extended_sensor_units.find(path);
        String unit = (it != extended_sensor_units.end()) ? it->second : "";
        xSemaphoreGive(sensor_mutex);
        return unit;
    }
    
    return "";
}

// Get sensor description by path
String get_sensor_description_by_path(const String& path) {
    if (path.length() == 0) return "";
    
    if (signalk_host().gauge_slot_units) {
        for (int i = 0; i < TOTAL_PARAMS; i++) {
            if (signalk_paths[i] == path) {
                return get_sensor_description(i);
            }
        }
    }
    
    // Check extended storage
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        auto it = extended_sensor_descriptions.find(path);
        String desc = (it != extended_sensor_descriptions.end()) ? it->second : "";
        xSemaphoreGive(sensor_mutex);
        return desc;
    }
    
    return "";
}


Sample get_sensor_sample(int index) {
    if (index < 0 || index >= TOTAL_PARAMS) {
        Sample sample;
        sample.value = NAN;
        sample.age_ms = 0;
        sample.valid = false;
        return sample;
    }
    float value = NAN;
    uint8_t state = SLOT_EMPTY;
    uint32_t stamp = 0;
    bool locked = false;
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        value = g_sensor_values[index];
        state = g_slot_state[index];
        stamp = g_slot_ms[index];
        locked = true;
        xSemaphoreGive(sensor_mutex);
    }
    return sample_from_locked(value, state, stamp, locked);
}

Sample get_sensor_sample_by_path(const String& path) {
    Sample missing;
    missing.value = NAN;
    missing.age_ms = 0;
    missing.valid = false;
    if (path.length() == 0) return missing;

    for (int i = 0; i < TOTAL_PARAMS; i++) {
        if (signalk_paths[i] == path) return get_sensor_sample(i);
    }

    float value = NAN;
    uint8_t state = SLOT_EMPTY;
    uint32_t stamp = 0;
    bool locked = false;
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        auto it = extended_sensor_values.find(path);
        if (it != extended_sensor_values.end()) {
            value = it->second.value;
            state = it->second.state;
            stamp = it->second.ms;
        }
        locked = true;
        xSemaphoreGive(sensor_mutex);
    }
    return sample_from_locked(value, state, stamp, locked);
}

bool nav_position_valid() {
    if (!signalk_source_live_unlocked()) return false;
    uint8_t state = g_nav_state;
    float lat = g_nav_latitude;
    float lon = g_nav_longitude;
    return state == SLOT_GOOD && isfinite(lat) && isfinite(lon);
}

static void store_slot_locked(int index, float value, bool good) {
    if (good) {
        g_sensor_values[index] = value;
        g_slot_state[index] = SLOT_GOOD;
        g_slot_ms[index] = millis();
    } else {
        g_slot_state[index] = SLOT_BAD;
    }
}

static void store_path_number(const char *path, float value, bool good) {
    if (path == NULL || path[0] == '\0') return;
    bool found = false;
    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
        for (int i = 0; i < TOTAL_PARAMS; i++) {
            if (signalk_paths[i].length() > 0 && signalk_paths[i].equals(path)) {
                store_slot_locked(i, value, good);
                found = true;
            }
        }
        if (!found) {
            PathSample stored;
            stored.value = good ? value : NAN;
            stored.state = good ? SLOT_GOOD : SLOT_BAD;
            stored.ms = millis();
            extended_sensor_values[String(path)] = stored;
        }
        xSemaphoreGive(sensor_mutex);
    }
}

static bool read_finite_number(JsonVariant value, float *out) {
    if (value.isNull() || value.is<bool>() || value.is<JsonObject>() || value.is<JsonArray>() || value.is<const char*>()) {
        return false;
    }
    if (!value.is<float>()) return false;
    float number = value.as<float>();
    if (!isfinite(number)) return false;
    *out = number;
    return true;
}

// Fetch metadata from SignalK REST API for a specific path
static void fetch_metadata_for_path(int index, const String &path) {
    if (path.length() == 0) return;
    
    // Convert dots to slashes for REST API path
    String rest_path = path;
    rest_path.replace(".", "/");
    
    HTTPClient http;
    String url = "http://" + server_ip_str + ":" + String(server_port_num) + "/signalk/v1/api/vessels/self/" + rest_path;
    
    esp_task_wdt_reset(); // prevent WDT during HTTP fetch
    http.begin(url);
    http.setTimeout(1500); // 1.5s — fast LAN; long enough for SK, short enough to avoid WDT
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        BasicJsonDocument<PsramAllocator> doc(2048);
        DeserializationError err = deserializeJson(doc, payload);
        
        if (!err) {
            // Check for meta field
            if (doc.containsKey("meta")) {
                JsonObject meta = doc["meta"].as<JsonObject>();
                const char* unit = nullptr;
                const char* description = nullptr;
                
                if (meta.containsKey("units")) {
                    unit = meta["units"];
                }
                if (meta.containsKey("description")) {
                    description = meta["description"];
                }
                
                if (unit || description) {
                    set_sensor_metadata(index, unit, description);
                }
            } else {
            }
        } else {
            Serial.printf("[SIGNALK] JSON parse error for %s: %s\n", path.c_str(), err.c_str());
        }
    } else {
        Serial.printf("[SIGNALK] HTTP GET failed for %s: code %d\n", path.c_str(), httpCode);
    }
    
    http.end();
}

// Fetch metadata for all configured paths (gauges, number displays, dual displays)
void fetch_all_metadata() {
    // Fetch for gauge paths (stored by index)
    for (int i = 0; i < TOTAL_PARAMS; i++) {
        if (signalk_paths[i].length() > 0) {
            esp_task_wdt_reset(); // prevent WDT across multi-path loop
            fetch_metadata_for_path(i, signalk_paths[i]);
            vTaskDelay(pdMS_TO_TICKS(50)); // Small delay between requests
        }
    }
    
    // Fetch for additional paths (number and dual displays) not in gauge slots
    std::vector<String> all_paths = get_all_signalk_paths();
    for (const String& path : all_paths) {
        if (path.length() == 0) continue;
        
        // Skip if already in gauge paths
        bool in_gauge = false;
        for (int i = 0; i < TOTAL_PARAMS; i++) {
            if (signalk_paths[i] == path) {
                in_gauge = true;
                break;
            }
        }
        
        if (!in_gauge) {
            // Fetch metadata and store in extended map
            String api_path = path;
            api_path.replace('.', '/');
            String url = "http://" + server_ip_str + ":" + String(server_port_num) + 
                         "/signalk/v1/api/vessels/self/" + api_path;
            
            esp_task_wdt_reset(); // prevent WDT across multi-path loop
            HTTPClient http;
            http.setTimeout(1500);
            http.begin(url);
            int httpCode = http.GET();
            
            if (httpCode == 200) {
                String payload = http.getString();
                BasicJsonDocument<PsramAllocator> doc(2048);
                DeserializationError err = deserializeJson(doc, payload);
                
                if (!err && doc.containsKey("meta")) {
                    JsonObject meta = doc["meta"].as<JsonObject>();
                    String unit = meta.containsKey("units") ? String(meta["units"].as<const char*>()) : String("");
                    String description = meta.containsKey("description") ? String(meta["description"].as<const char*>()) : String("");
                    
                    if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
                        if (unit.length() > 0) extended_sensor_units[path] = unit;
                        if (description.length() > 0) extended_sensor_descriptions[path] = description;
                        xSemaphoreGive(sensor_mutex);
                    }
                }
            }
            http.end();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// Initialize mutex
void init_sensor_mutex() {
    if (sensor_mutex == NULL) {
        sensor_mutex = xSemaphoreCreateMutex();
    }
}

// WebSocket event handler
static void wsEvent(WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_CONNECTED) {
        Serial.println("Signal K: WebSocket connected");
        g_link_up = true;
        last_message_time = millis();
        // reset backoff on successful connect
        current_backoff_ms = RECONNECT_BASE_MS;
        // Subscribe only to paths for the active screen (+ background graph screens)
        // Manual string build avoids DynamicJsonDocument's 2048B iRAM alloc in the WS connect handler.
        int active_scr = ui_get_current_screen();  // 1-based
        std::vector<String> all_conn_paths = get_active_screen_paths(active_scr);
        String out = "{\"context\":\"vessels.self\",\"subscribe\":[";
        bool first_conn = true;
        for (const String& p : all_conn_paths) {
            if (p.length() > 0) {
                if (!first_conn) out += ",";
                out += "{\"path\":\"";
                out += p;
                out += "\",\"period\":";
                out += String(signalk_host().subscribe_period_ms);
                out += "}";
                first_conn = false;
            }
        }
        out += "]}";
        ws_client.sendTXT(out);
        // flush any queued outgoing messages (resubscribe, etc)
        flush_outgoing();
        
        // 4" learns units over HTTP. Rounds take units from the socket meta.
        if (signalk_host().fetch_metadata_on_connect) {
            fetch_all_metadata();
        }
        
        return;
    }

    if (type == WStype_TEXT) {
        last_message_time = millis();
        String msg = String((char*)payload, length);
        
        // Parse incoming JSON in PSRAM to avoid ~4 KB iRAM allocation per message
        BasicJsonDocument<PsramAllocator> doc(4096);
        DeserializationError err = deserializeJson(doc, msg);
        if (err) {
            Serial.printf("[SIGNALK] JSON parse error: %s\n", err.c_str());
            return;
        }

        if (doc.containsKey("updates")) {
            JsonArray updates = doc["updates"].as<JsonArray>();
            for (JsonVariant update : updates) {
                if (!update.containsKey("values")) continue;
                JsonArray values = update["values"].as<JsonArray>();
                for (JsonVariant val : values) {
                    if (!val.containsKey("path") || !val.containsKey("value")) continue;
                    const char* path = val["path"];

                    // navigation.position arrives as a JSON object {latitude,longitude}.
                    // Rounds leave the object to as<float>(), which stores 0.
                    if (signalk_host().parse_nav_objects && strcmp(path, "navigation.position") == 0) {
                        float lat = NAN;
                        float lon = NAN;
                        bool good = false;
                        if (val["value"].is<JsonObject>()) {
                            JsonObject pos = val["value"].as<JsonObject>();
                            good = read_finite_number(pos["latitude"], &lat) && read_finite_number(pos["longitude"], &lon);
                        }
                        if (good) {
                            g_nav_latitude = lat;
                            g_nav_longitude = lon;
                            g_nav_state = SLOT_GOOD;
                            g_nav_ms = millis();
                        } else {
                            g_nav_state = SLOT_BAD;
                        }
                        continue;
                    }
                    // navigation.datetime arrives as an ISO-8601 string
                    if (signalk_host().parse_nav_objects && strcmp(path, "navigation.datetime") == 0) {
                        const char* dt = val["value"].as<const char*>();
                        if (dt) { strncpy(g_sk_datetime, dt, 31); g_sk_datetime[31] = '\0'; }
                        continue;
                    }

                    float value = NAN;
                    bool good = read_finite_number(val["value"], &value);
                    store_path_number(path, value, good);
                }
                // Units carried on the update. Updates with no values are skipped
                // above, which is the same filter both firmwares already used.
                if (signalk_host().capture_ws_meta && update.containsKey("meta")) {
                    JsonArray metas = update["meta"].as<JsonArray>();
                    for (JsonVariant m : metas) {
                        const char* mpath = m["path"] | "";
                        if (mpath == NULL || mpath[0] == '\0') continue;
                        if (sensor_mutex != NULL && xSemaphoreTake(sensor_mutex, pdMS_TO_TICKS(50))) {
                            if (m.containsKey("units")) {
                                const char* u = m["units"].as<const char*>();
                                if (u) extended_sensor_units[String(mpath)] = String(u);
                            }
                            if (m.containsKey("description")) {
                                const char* d = m["description"].as<const char*>();
                                if (d) extended_sensor_descriptions[String(mpath)] = String(d);
                            }
                            xSemaphoreGive(sensor_mutex);
                        }
                    }
                }
            }
        }
    }
    if (type == WStype_PONG) {
        last_message_time = millis();
    }

    if (type == WStype_DISCONNECTED) {
        g_link_up = false;
        if (payload != NULL && length > 0) {
            Serial.printf("[SK] WebSocket disconnected (%.*s) — will reconnect\n",
                          (int)length, (const char *)payload);
        } else {
            Serial.println("[SK] WebSocket disconnected — will reconnect");
        }
        current_backoff_ms = RECONNECT_BASE_MS;
        next_reconnect_at = millis() + RECONNECT_BASE_MS;
    }

    if (type == WStype_ERROR) {
        g_link_up = false;
        Serial.println("[SK] WebSocket error — will reconnect");
        current_backoff_ms = RECONNECT_BASE_MS;
        next_reconnect_at = millis() + RECONNECT_BASE_MS;
    }
}

// Helper: begin WS connection
static void ws_begin_connection() {
    // subscribe=none prevents server from firehosing all data on connect
    ws_client.begin(server_ip_str.c_str(), server_port_num, "/signalk/v1/stream?subscribe=none");
    ws_client.onEvent(wsEvent);
}

// FreeRTOS task for Signal K updates (runs on core 0)
// FreeRTOS task for Signal K WebSocket updates (runs on core 0)
static void signalk_task(void *parameter) {
    Serial.println("Signal K task started (WebSocket)");
    vTaskDelay(pdMS_TO_TICKS(500));

    while (signalk_enabled) {
        // Config UI pause
        if (g_signalk_ws_paused) {
            if (ws_client.isConnected()) {
                ws_client.disconnect();
                current_backoff_ms = RECONNECT_BASE_MS;
                Serial.println("[SK] Config UI active - WS disconnected to free iRAM");
            }
            if (g_signalk_ws_resume_when_ready) {
                g_signalk_ws_resume_when_ready = false;
                g_signalk_ws_paused = false;
                next_reconnect_at = millis() + 1000;
                Serial.println("[SK] WS unpaused, reconnecting in 1s");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // CRITICAL: ws_client.loop() may fire wsEvent() which sets
        // last_message_time = millis(). We MUST sample `now` AFTER loop()
        // so that `now - last_message_time` doesn't underflow to ~4 billion.
        ws_client.loop();
        flush_outgoing();

        unsigned long now = millis();

        if (ws_client.isConnected()) {
            if (now - last_message_time >= PING_INTERVAL_MS) {
                ws_client.sendPing();
                // A ping is traffic we sent. Count it so the idle check does not
                // fire again on the next 10 ms loop. A server that never pongs
                // is no longer dropped by this timer.
                last_message_time = now;
            }
            if (now - last_message_time >= MESSAGE_TIMEOUT_MS) {
                Serial.println("Signal K: connection idle timeout, forcing disconnect");
                ws_client.disconnect();
                unsigned int jitter = (esp_random() & 0x7FF) % 1000;
                next_reconnect_at = now + current_backoff_ms + jitter;
                last_reconnect_attempt = now;
                current_backoff_ms = min(current_backoff_ms * 2, RECONNECT_MAX_MS);
            }
        } else {
            if (next_reconnect_at == 0) {
                next_reconnect_at = now + current_backoff_ms;
            }
            if (now >= next_reconnect_at) {
                ws_begin_connection();
                last_reconnect_attempt = now;
                unsigned int jitter = (esp_random() & 0x7FF) % 1000;
                next_reconnect_at = now + current_backoff_ms + jitter;
                current_backoff_ms = min(current_backoff_ms * 2, RECONNECT_MAX_MS);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Serial.println("Signal K task ended");
    vTaskDelete(NULL);
}

// Enable Signal K with WiFi credentials
void enable_signalk(const char* ssid, const char* password, const char* server_ip, uint16_t server_port) {
    if (signalk_enabled) {
        Serial.println("Signal K already enabled");
        return;
    }
    
    signalk_enabled = true;
    server_ip_str = server_ip;
    server_port_num = server_port;
    
    // Get all paths from configuration including gauges, number displays, and dual displays
    std::vector<String> all_paths = get_all_signalk_paths();
    
    // First, load the traditional gauge paths into signalk_paths array
    for (int i = 0; i < TOTAL_PARAMS; i++) {
        signalk_paths[i] = get_signalk_path_by_index(i);
    }
    
    // Initialize mutex first
    init_sensor_mutex();
    // create ws queue mutex
    if (ws_queue_mutex == NULL) {
        ws_queue_mutex = xSemaphoreCreateMutex();
    }
    
    // WiFi should already be connected from setup_sensESP()
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Signal K: WiFi not connected, aborting");
        signalk_enabled = false;
        return;
    }

    Serial.println("Signal K: Starting WebSocket client...");
    ws_begin_connection();
    ws_client.setReconnectInterval(0);

    // Create task to pump connection loop
    xTaskCreatePinnedToCore(signalk_task, "SignalKWS", 8192, NULL, 3, &signalk_task_handle, 0);

    Serial.println("Signal K task created successfully");
    Serial.flush();
}

// Disable Signal K
void disable_signalk() {
    signalk_enabled = false;
    if (signalk_task_handle != NULL) {
        vTaskDelete(signalk_task_handle);
        signalk_task_handle = NULL;
    }
    ws_client.disconnect();
    Serial.println("Signal K disabled");
}

// Returns true if the WS is currently paused.
bool is_signalk_ws_paused() {
    return g_signalk_ws_paused;
}

// Pause the WebSocket connection while the config UI is open.
// Sets the pause flag and yields 300ms so signalk_task (Core 0) sees it,
// calls ws_client.disconnect(), and the ~22KB WS receive buffer is freed
// before the HTTP handler builds and sends the large config page.
void pause_signalk_ws() {
    if (!signalk_enabled) return;
    // Cancel any pending or in-flight resume so the signalk_task doesn't
    // unpause itself while we're serving config fragments.
    g_signalk_ws_resume_when_ready = false;
    g_signalk_ws_resume_pending = false;

    if (g_signalk_ws_paused) {
        // Already paused — WS is disconnected and buffers are freed.
        // Skip the 300ms wait; just print current iRAM for diagnostics.
        Serial.printf("[SK] WS already paused, iRAM now %u B\n",
                      heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return;
    }
    g_signalk_ws_paused = true;
    // 6 × 50ms = 300ms: task runs every 10ms so it sees the flag in <10ms;
    // remaining 290ms is for lwIP to actually free the TCP socket buffers.
    for (int i = 0; i < 6; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_task_wdt_reset();
    }
    Serial.printf("[SK] WS paused for config UI, iRAM now %u B\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

// Set when handle_save_gauges() wants WS resumed but LVGL apply is still pending.
// The main loop calls resume_signalk_ws() after apply_all_screen_visuals() completes.
volatile bool g_signalk_ws_resume_pending = false;


// Resume the WebSocket connection after the config save completes.
// Keeps g_signalk_ws_paused=true and sets g_signalk_ws_resume_when_ready so the
// signalk_task will reconnect once iRAM > 18KB (lwIP TIME_WAIT PCBs have cleared).
void resume_signalk_ws() {
    if (!signalk_enabled) return;
    g_signalk_ws_resume_pending = false;
    // Keep paused — signalk_task will clear the pause once iRAM recovers
    g_signalk_ws_resume_when_ready = true;
    Serial.println("[SK] WS resume requested - waiting for iRAM > 18KB before reconnecting");
}

// Schedule a WS resume to happen after the next apply_all_screen_visuals() completes.
// Call this from HTTP handlers instead of resume_signalk_ws() directly, so that
// LVGL image SD reads happen while iRAM is still free (WS still paused).
void schedule_signalk_ws_resume() {
    if (!signalk_enabled) return;
    g_signalk_ws_resume_pending = true;
    Serial.println("[SK] WS resume deferred until after screen rebuild");
}

// Helper: collect paths for active screen + background graph screens
static std::vector<String> get_active_screen_paths(int screen_1based) {
    std::set<String> seen_paths;
    std::vector<String> result;
    int active_idx = screen_1based - 1;
    if (active_idx < 0) active_idx = 0;

    auto merge = [&](const std::vector<String>& src) {
        for (const String& p : src) {
            if (p.length() > 0 && seen_paths.find(p) == seen_paths.end()) {
                seen_paths.insert(p);
                result.push_back(p);
            }
        }
    };

    // Active screen paths
    merge(get_signalk_paths_for_screen(active_idx));

    // Background graph screens still need data collection.
    // Armed thresholds and an anchor watch also need their paths while another
    // screen is in front, or the alarm reads a slot the socket is not sending.
    for (int s = 0; s < NUM_SCREENS; s++) {
        bool want = false;
        if (s != active_idx && screen_configs[s].display_type == DISPLAY_TYPE_GRAPH) want = true;
        if (screen_configs[s].display_type == DISPLAY_TYPE_ANCHOR) want = true;
        for (int g = 0; g < 2 && !want; g++) {
            for (int z = 0; z < 5; z++) {
                if (screen_configs[s].buzzer[g][z] != 0) want = true;
            }
        }
        if (want) merge(get_signalk_paths_for_screen(s));
    }
    return result;
}

// Subscribe to only the given screen's paths (+ background graph screens)
void subscribe_to_active_screen(int screen_1based) {
    std::vector<String> paths = get_active_screen_paths(screen_1based);

    // First unsubscribe from everything
    String unsub = "{\"context\":\"vessels.self\",\"unsubscribe\":[{\"path\":\"*\"}]}";
    enqueue_outgoing(unsub);

    // Then subscribe to only what we need
    String out = "{\"context\":\"vessels.self\",\"subscribe\":[";
    bool first = true;
    for (const String& p : paths) {
        if (!first) out += ",";
        out += "{\"path\":\"";
        out += p;
        out += "\",\"period\":";
        out += String(signalk_host().subscribe_period_ms);
        out += "}";
        first = false;
    }
    out += "]}";
    enqueue_outgoing(out);
    Serial.printf("[SK] Subscribed to %d paths for screen %d\n", (int)paths.size(), screen_1based);
}

// Rebuild the subscription list from current configuration and (re)send it
// over the active WebSocket connection if connected. If the WS is not
// connected, the updated paths will be used when connection is (re)established.
void refresh_signalk_subscriptions() {
    // Reload gauge paths from configuration
    for (int i = 0; i < TOTAL_PARAMS; i++) {
        signalk_paths[i] = get_signalk_path_by_index(i);
    }

    // Subscribe to active screen paths (not all paths)
    int active = ui_get_current_screen();
    std::vector<String> all_paths = get_active_screen_paths(active);

    // Build subscription JSON manually to avoid DynamicJsonDocument allocating
    // 2048 bytes from internal iRAM on every save. DynamicJsonDocument uses malloc()
    // which draws from the internal heap; on a device with ~10 KB iRAM headroom this
    // fragments the heap and leaves the SDMMC DMA layer without a contiguous block.
    // Manual string building uses PSRAM-backed Arduino String objects instead.
    // Unsubscribe first, then subscribe to active paths only.
    String unsub = "{\"context\":\"vessels.self\",\"unsubscribe\":[{\"path\":\"*\"}]}";
    enqueue_outgoing(unsub);

    String out = "{\"context\":\"vessels.self\",\"subscribe\":[";
    bool first = true;
    for (const String& path : all_paths) {
        if (path.length() > 0) {
            if (!first) out += ",";
            out += "{\"path\":\"";
            out += path;
            out += "\",\"period\":";
            out += String(signalk_host().subscribe_period_ms);
            out += "}";
            first = false;
        }
    }
    out += "]}";

    // Always queue — never call ws_client.sendTXT() directly here.
    // refresh_signalk_subscriptions() may be called from Core 1 (HTTP handler)
    // while signalk_task on Core 0 is inside ws_client.loop(). Calling sendTXT()
    // from two cores simultaneously is an unprotected race that crashes the device.
    // flush_outgoing() inside signalk_task will drain the queue safely from Core 0.
    enqueue_outgoing(out);
}

#include "alarm_monitor.cpp"


