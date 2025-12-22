/**
 * Weather API - Open-Meteo Integration
 * Free weather API without API key
 */

#include "weather_api.h"
#include "system_settings.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "WEATHER_API";

// Cached weather data
static weather_data_t cached_weather = {0};
static bool initialized = false;

// HTTP response buffer
#define HTTP_BUFFER_SIZE 4096
static char *http_buffer = NULL;
static int http_buffer_len = 0;

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            // Handle both chunked and non-chunked responses
            if (http_buffer && (http_buffer_len + evt->data_len < HTTP_BUFFER_SIZE - 1)) {
                memcpy(http_buffer + http_buffer_len, evt->data, evt->data_len);
                http_buffer_len += evt->data_len;
                http_buffer[http_buffer_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

void weather_api_init(void)
{
    if (initialized) return;
    
    memset(&cached_weather, 0, sizeof(cached_weather));
    cached_weather.valid = false;
    initialized = true;
    
    ESP_LOGI(TAG, "Weather API initialized");
}

int weather_api_fetch(float latitude, float longitude, weather_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    
    // Allocate HTTP buffer
    http_buffer = (char*)malloc(HTTP_BUFFER_SIZE);
    if (!http_buffer) {
        ESP_LOGE(TAG, "Failed to allocate HTTP buffer");
        return ESP_ERR_NO_MEM;
    }
    http_buffer_len = 0;
    http_buffer[0] = '\0';
    
    // Build URL
    char url[512];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,surface_pressure"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min"
        "&timezone=auto&forecast_days=7",
        latitude, longitude);
    
    ESP_LOGI(TAG, "Fetching weather from: %s", url);
    
    // Configure HTTP client with SSL - skip certificate verification for Open-Meteo
    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.timeout_ms = 15000;
    config.buffer_size = 2048;
    // Disable SSL certificate verification completely
    config.cert_pem = NULL;
    config.use_global_ca_store = false;
    config.skip_cert_common_name_check = true;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    // Tell mbedtls to skip server verification
    config.crt_bundle_attach = NULL;
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(http_buffer);
        http_buffer = NULL;
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return ESP_FAIL;
    }
    
    // Perform request
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    
    if (err != ESP_OK || status_code != 200) {
        ESP_LOGE(TAG, "HTTP request failed: err=%d, status=%d", err, status_code);
        free(http_buffer);
        http_buffer = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Received %d bytes", http_buffer_len);
    
    // Parse JSON
    cJSON *root = cJSON_Parse(http_buffer);
    free(http_buffer);
    http_buffer = NULL;
    
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }
    
    // Parse current weather
    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current) {
        cJSON *temp = cJSON_GetObjectItem(current, "temperature_2m");
        cJSON *apparent = cJSON_GetObjectItem(current, "apparent_temperature");
        cJSON *humidity = cJSON_GetObjectItem(current, "relative_humidity_2m");
        cJSON *wind = cJSON_GetObjectItem(current, "wind_speed_10m");
        cJSON *pressure = cJSON_GetObjectItem(current, "surface_pressure");
        cJSON *code = cJSON_GetObjectItem(current, "weather_code");
        
        if (temp) data->current.temperature = (float)temp->valuedouble;
        if (apparent) data->current.apparent_temperature = (float)apparent->valuedouble;
        if (humidity) data->current.humidity = (float)humidity->valuedouble;
        if (wind) data->current.wind_speed = (float)wind->valuedouble;
        if (pressure) data->current.pressure = (float)pressure->valuedouble;
        if (code) data->current.weather_code = (weather_code_t)code->valueint;
    }

    // Parse daily forecast
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON *codes = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *temps_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *temps_min = cJSON_GetObjectItem(daily, "temperature_2m_min");
        
        if (codes && temps_max && temps_min) {
            int count = cJSON_GetArraySize(codes);
            if (count > 7) count = 7;
            data->daily_count = count;
            
            for (int i = 0; i < count; i++) {
                cJSON *code = cJSON_GetArrayItem(codes, i);
                cJSON *tmax = cJSON_GetArrayItem(temps_max, i);
                cJSON *tmin = cJSON_GetArrayItem(temps_min, i);
                
                if (code) data->daily[i].weather_code = (weather_code_t)code->valueint;
                if (tmax) data->daily[i].temp_max = (float)tmax->valuedouble;
                if (tmin) data->daily[i].temp_min = (float)tmin->valuedouble;
                
                // Set day name
                strncpy(data->daily[i].day_name, weather_get_day_name(i), 3);
                data->daily[i].day_name[3] = '\0';
            }
        }
    }
    
    cJSON_Delete(root);
    
    // Set metadata
    data->valid = true;
    data->fetch_time = time(NULL);
    
    // Copy city name from settings
    location_settings_t *loc = settings_get_location();
    if (loc && loc->valid) {
        snprintf(data->city_name, sizeof(data->city_name), "%s", loc->city_name);
    } else {
        snprintf(data->city_name, sizeof(data->city_name), "Unknown");
    }
    
    // Update cache
    memcpy(&cached_weather, data, sizeof(weather_data_t));
    
    ESP_LOGI(TAG, "Weather fetched: %.1f°C, code=%d, city=%s", 
             data->current.temperature, data->current.weather_code, data->city_name);
    
    return ESP_OK;
}

weather_data_t* weather_api_get_cached(void)
{
    return &cached_weather;
}

bool weather_api_cache_valid(void)
{
    if (!cached_weather.valid) return false;
    
    // Cache valid for 30 minutes
    int64_t now = time(NULL);
    return (now - cached_weather.fetch_time) < (30 * 60);
}

const char* weather_code_to_string(weather_code_t code)
{
    switch (code) {
        case WEATHER_CLEAR: return "Clear";
        case WEATHER_MAINLY_CLEAR: return "Mainly Clear";
        case WEATHER_PARTLY_CLOUDY: return "Partly Cloudy";
        case WEATHER_OVERCAST: return "Overcast";
        case WEATHER_FOG:
        case WEATHER_DEPOSITING_RIME_FOG: return "Foggy";
        case WEATHER_DRIZZLE_LIGHT:
        case WEATHER_DRIZZLE_MODERATE:
        case WEATHER_DRIZZLE_DENSE: return "Drizzle";
        case WEATHER_FREEZING_DRIZZLE_LIGHT:
        case WEATHER_FREEZING_DRIZZLE_DENSE: return "Freezing Drizzle";
        case WEATHER_RAIN_SLIGHT: return "Light Rain";
        case WEATHER_RAIN_MODERATE: return "Rain";
        case WEATHER_RAIN_HEAVY: return "Heavy Rain";
        case WEATHER_FREEZING_RAIN_LIGHT:
        case WEATHER_FREEZING_RAIN_HEAVY: return "Freezing Rain";
        case WEATHER_SNOW_SLIGHT: return "Light Snow";
        case WEATHER_SNOW_MODERATE: return "Snow";
        case WEATHER_SNOW_HEAVY: return "Heavy Snow";
        case WEATHER_SNOW_GRAINS: return "Snow Grains";
        case WEATHER_RAIN_SHOWERS_SLIGHT:
        case WEATHER_RAIN_SHOWERS_MODERATE:
        case WEATHER_RAIN_SHOWERS_VIOLENT: return "Rain Showers";
        case WEATHER_SNOW_SHOWERS_SLIGHT:
        case WEATHER_SNOW_SHOWERS_HEAVY: return "Snow Showers";
        case WEATHER_THUNDERSTORM:
        case WEATHER_THUNDERSTORM_HAIL_SLIGHT:
        case WEATHER_THUNDERSTORM_HAIL_HEAVY: return "Thunderstorm";
        default: return "Unknown";
    }
}

const char* weather_code_to_icon(weather_code_t code)
{
    // Using LVGL symbols
    switch (code) {
        case WEATHER_CLEAR:
        case WEATHER_MAINLY_CLEAR:
            return LV_SYMBOL_IMAGE;  // Sun
        case WEATHER_PARTLY_CLOUDY:
            return LV_SYMBOL_IMAGE;  // Partial cloud
        case WEATHER_OVERCAST:
            return LV_SYMBOL_IMAGE;  // Cloud
        case WEATHER_FOG:
        case WEATHER_DEPOSITING_RIME_FOG:
            return LV_SYMBOL_EYE_CLOSE;  // Fog
        case WEATHER_DRIZZLE_LIGHT:
        case WEATHER_DRIZZLE_MODERATE:
        case WEATHER_DRIZZLE_DENSE:
        case WEATHER_RAIN_SLIGHT:
        case WEATHER_RAIN_MODERATE:
        case WEATHER_RAIN_HEAVY:
        case WEATHER_RAIN_SHOWERS_SLIGHT:
        case WEATHER_RAIN_SHOWERS_MODERATE:
        case WEATHER_RAIN_SHOWERS_VIOLENT:
            return LV_SYMBOL_CHARGE;  // Rain
        case WEATHER_SNOW_SLIGHT:
        case WEATHER_SNOW_MODERATE:
        case WEATHER_SNOW_HEAVY:
        case WEATHER_SNOW_GRAINS:
        case WEATHER_SNOW_SHOWERS_SLIGHT:
        case WEATHER_SNOW_SHOWERS_HEAVY:
            return LV_SYMBOL_CHARGE;  // Snow
        case WEATHER_THUNDERSTORM:
        case WEATHER_THUNDERSTORM_HAIL_SLIGHT:
        case WEATHER_THUNDERSTORM_HAIL_HEAVY:
            return LV_SYMBOL_CHARGE;  // Thunder
        default:
            return LV_SYMBOL_IMAGE;
    }
}

const char* weather_get_day_name(int day_offset)
{
    static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int day = (tm_info->tm_wday + day_offset) % 7;
    
    if (day_offset == 0) return "Today";
    if (day_offset == 1) return "Tmrw";
    
    return days[day];
}
