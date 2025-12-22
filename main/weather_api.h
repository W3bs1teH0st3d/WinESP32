/**
 * Weather API - Open-Meteo Integration
 * Free weather API without API key
 */

#ifndef WEATHER_API_H
#define WEATHER_API_H

#include <stdint.h>
#include <stdbool.h>

// Weather codes from Open-Meteo (WMO codes)
typedef enum {
    WEATHER_CLEAR = 0,
    WEATHER_MAINLY_CLEAR = 1,
    WEATHER_PARTLY_CLOUDY = 2,
    WEATHER_OVERCAST = 3,
    WEATHER_FOG = 45,
    WEATHER_DEPOSITING_RIME_FOG = 48,
    WEATHER_DRIZZLE_LIGHT = 51,
    WEATHER_DRIZZLE_MODERATE = 53,
    WEATHER_DRIZZLE_DENSE = 55,
    WEATHER_FREEZING_DRIZZLE_LIGHT = 56,
    WEATHER_FREEZING_DRIZZLE_DENSE = 57,
    WEATHER_RAIN_SLIGHT = 61,
    WEATHER_RAIN_MODERATE = 63,
    WEATHER_RAIN_HEAVY = 65,
    WEATHER_FREEZING_RAIN_LIGHT = 66,
    WEATHER_FREEZING_RAIN_HEAVY = 67,
    WEATHER_SNOW_SLIGHT = 71,
    WEATHER_SNOW_MODERATE = 73,
    WEATHER_SNOW_HEAVY = 75,
    WEATHER_SNOW_GRAINS = 77,
    WEATHER_RAIN_SHOWERS_SLIGHT = 80,
    WEATHER_RAIN_SHOWERS_MODERATE = 81,
    WEATHER_RAIN_SHOWERS_VIOLENT = 82,
    WEATHER_SNOW_SHOWERS_SLIGHT = 85,
    WEATHER_SNOW_SHOWERS_HEAVY = 86,
    WEATHER_THUNDERSTORM = 95,
    WEATHER_THUNDERSTORM_HAIL_SLIGHT = 96,
    WEATHER_THUNDERSTORM_HAIL_HEAVY = 99
} weather_code_t;

// Current weather data
typedef struct {
    float temperature;          // °C
    float apparent_temperature; // Feels like °C
    float humidity;             // %
    float wind_speed;           // km/h
    float pressure;             // hPa
    weather_code_t weather_code;
    int64_t timestamp;          // Unix timestamp
} current_weather_t;

// Daily forecast data
typedef struct {
    float temp_max;             // °C
    float temp_min;             // °C
    weather_code_t weather_code;
    char day_name[4];           // "Mon", "Tue", etc.
} daily_forecast_t;

// Complete weather data
typedef struct {
    current_weather_t current;
    daily_forecast_t daily[7];  // 7-day forecast
    int daily_count;
    char city_name[64];
    bool valid;
    int64_t fetch_time;         // When data was fetched
} weather_data_t;

// Initialize weather API (call once)
void weather_api_init(void);

// Fetch weather data (blocking, call from task)
// Returns ESP_OK on success
int weather_api_fetch(float latitude, float longitude, weather_data_t *data);

// Get cached weather data (non-blocking)
weather_data_t* weather_api_get_cached(void);

// Check if cached data is valid (not older than 30 min)
bool weather_api_cache_valid(void);

// Get weather description string from code
const char* weather_code_to_string(weather_code_t code);

// Get weather icon (LVGL symbol) from code
const char* weather_code_to_icon(weather_code_t code);

// Get day name from index (0 = today)
const char* weather_get_day_name(int day_offset);

#endif // WEATHER_API_H
