/**
 * Win32 OS - Cities Database
 * Popular cities with coordinates for weather and timezone
 */

#ifndef CITIES_DATA_H
#define CITIES_DATA_H

#include <stdint.h>

// City structure
typedef struct {
    const char *name;       // City name (UTF-8)
    float lat;              // Latitude
    float lon;              // Longitude
    int8_t tz;              // Timezone offset from UTC
    const char *country;    // Country code
} city_info_t;

// Popular Russian cities (top 12)
static const city_info_t russian_cities[] = {
    {"Москва", 55.7558f, 37.6173f, 3, "RU"},
    {"Санкт-Петербург", 59.9343f, 30.3351f, 3, "RU"},
    {"Новосибирск", 55.0084f, 82.9357f, 7, "RU"},
    {"Екатеринбург", 56.8389f, 60.6057f, 5, "RU"},
    {"Казань", 55.8304f, 49.0661f, 3, "RU"},
    {"Краснодар", 45.0355f, 38.9753f, 3, "RU"},
    {"Сочи", 43.5855f, 39.7231f, 3, "RU"},
    {"Калининград", 54.7104f, 20.4522f, 2, "RU"},
    {"Владивосток", 43.1332f, 131.9113f, 10, "RU"},
    {"Симферополь", 44.9521f, 34.1024f, 3, "RU"},
    {"Якутск", 62.0355f, 129.6755f, 9, "RU"},
    {"Мирный", 62.5354f, 113.9610f, 9, "RU"},
};
#define RUSSIAN_CITIES_COUNT (sizeof(russian_cities) / sizeof(russian_cities[0]))

// International cities (top 10 only)
static const city_info_t world_cities[] = {
    {"London", 51.5074f, -0.1278f, 0, "GB"},
    {"Paris", 48.8566f, 2.3522f, 1, "FR"},
    {"Berlin", 52.5200f, 13.4050f, 1, "DE"},
    {"New York", 40.7128f, -74.0060f, -5, "US"},
    {"Los Angeles", 34.0522f, -118.2437f, -8, "US"},
    {"Tokyo", 35.6762f, 139.6503f, 9, "JP"},
    {"Beijing", 39.9042f, 116.4074f, 8, "CN"},
    {"Dubai", 25.2048f, 55.2708f, 4, "AE"},
    {"Sydney", -33.8688f, 151.2093f, 10, "AU"},
    {"Kyiv", 50.4501f, 30.5234f, 2, "UA"},
};
#define WORLD_CITIES_COUNT (sizeof(world_cities) / sizeof(world_cities[0]))

// Helper function to find city by name
static inline const city_info_t* find_city(const char *name) {
    // Search Russian cities first
    for (int i = 0; i < RUSSIAN_CITIES_COUNT; i++) {
        if (strcmp(russian_cities[i].name, name) == 0) {
            return &russian_cities[i];
        }
    }
    // Search world cities
    for (int i = 0; i < WORLD_CITIES_COUNT; i++) {
        if (strcmp(world_cities[i].name, name) == 0) {
            return &world_cities[i];
        }
    }
    return NULL;
}

#endif // CITIES_DATA_H
