#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

#include <curl/curl.h>
#include <cjson/cJSON.h>

struct WeatherReading {
    double temperature = 0.0;
    std::string temperature_unit;
    int weather_code = -1;
    double wind_speed = 0.0;
    std::string wind_speed_unit;
};

struct WeatherData {
    std::mutex mutex;
    WeatherReading reading;
    bool valid = false;
};

struct Memory {
    char *response;
    size_t size;
};


extern "C" const char *weather_description(int weather_code)
{
    switch (weather_code) {
        case 0: return "Helder";
        case 1: return "Overwegend helder";
        case 2: return "Gedeeltelijk bewolkt";
        case 3: return "Betrokken";
        case 45: return "Mist";
        case 48: return "Aangevroren mist";
        case 51: return "Lichte motregen";
        case 53: return "Motregen";
        case 55: return "Zware motregen";
        case 56: return "Lichte onderkoelde motregen";
        case 57: return "Zware onderkoelde motregen";
        case 61: return "Lichte regen";
        case 63: return "Regen";
        case 65: return "Zware regen";
        case 66: return "Lichte onderkoelde regen";
        case 67: return "Zware onderkoelde regen";
        case 71: return "Lichte sneeuw";
        case 73: return "Sneeuw";
        case 75: return "Zware sneeuw";
        case 77: return "Sneeuwkorrels";
        case 80: return "Lichte regenbuien";
        case 81: return "Regenbuien";
        case 82: return "Zware regenbuien";
        case 85: return "Lichte sneeuwbuien";
        case 86: return "Zware sneeuwbuien";
        case 95: return "Onweer";
        case 96: return "Onweer met hagel";
        case 99: return "Onweer met zware hagel";
        default: return "Onbekend";
    }
}

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    if (size != 0 && nmemb > std::numeric_limits<size_t>::max() / size) return 0;

    const size_t total_size = size * nmemb;
    Memory *mem = static_cast<Memory *>(userp);
    if (total_size > std::numeric_limits<size_t>::max() - mem->size - 1) return 0;

    char *ptr = static_cast<char *>(realloc(mem->response, mem->size + total_size + 1));
    if (ptr == NULL) return 0;

    mem->response = ptr;
    memcpy(mem->response + mem->size, contents, total_size);
    mem->size += total_size;
    mem->response[mem->size] = '\0';
    return total_size;
}

static bool fetch_weather(WeatherReading *reading)
{
    if (reading == NULL) return false;

    static const char *url =
        "https://api.open-meteo.com/v1/forecast?"
        "latitude=51.2217&longitude=2.9241&current=temperature_2m,weather_code,wind_speed_10m";

    Memory chunk{static_cast<char *>(malloc(1)), 0};
    if (chunk.response == NULL) {
        fprintf(stderr, "Weather request failed: out of memory\n");
        return false;
    }
    chunk.response[0] = '\0';

    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Weather request failed: could not initialize cURL\n");
        free(chunk.response);
        return false;
    }

    CURLcode res = curl_easy_setopt(curl, CURLOPT_URL, url);
    if (res == CURLE_OK) res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    if (res == CURLE_OK) res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
    if (res == CURLE_OK) res = curl_easy_setopt(curl, CURLOPT_USERAGENT, "homewiz/1.0");
    if (res == CURLE_OK) res = curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    if (res == CURLE_OK) res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    if (res == CURLE_OK) res = curl_easy_perform(curl);

    long http_status = 0;
    if (res == CURLE_OK)
        res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

    bool success = false;
    if (res != CURLE_OK) {
        fprintf(stderr, "Weather request failed: %s\n", curl_easy_strerror(res));
    } else if (http_status < 200 || http_status >= 300) {
        fprintf(stderr, "Weather request failed: HTTP status %ld\n", http_status);
    } else {
        cJSON *json = cJSON_Parse(chunk.response);
        if (json == NULL) {
            fprintf(stderr, "Weather request failed: JSON parsing error\n");
        } else if (!cJSON_IsObject(json)) {
            fprintf(stderr, "Weather request failed: unexpected JSON response\n");
            cJSON_Delete(json);
        } else {
            cJSON *current = cJSON_GetObjectItemCaseSensitive(json, "current");
            cJSON *units = cJSON_GetObjectItemCaseSensitive(json, "current_units");
            cJSON *temp = cJSON_IsObject(current) ? cJSON_GetObjectItemCaseSensitive(current, "temperature_2m")
                                                    : NULL;
            cJSON *code = cJSON_IsObject(current) ? cJSON_GetObjectItemCaseSensitive(current, "weather_code")
                                                    : NULL;
            cJSON *wind = cJSON_IsObject(current) ? cJSON_GetObjectItemCaseSensitive(current, "wind_speed_10m")
                                                    : NULL;
            cJSON *unit_item = cJSON_IsObject(units)  ? cJSON_GetObjectItemCaseSensitive(units, "temperature_2m")
                                                    : NULL;
            cJSON *wind_unit_item = cJSON_IsObject(units)
                                        ? cJSON_GetObjectItemCaseSensitive(units, "wind_speed_10m")
                                        : NULL;

            if (cJSON_IsNumber(temp) && std::isfinite(temp->valuedouble) &&
                cJSON_IsNumber(code) && std::isfinite(code->valuedouble) &&
                code->valuedouble >= 0 && code->valuedouble <= 99 &&
                std::floor(code->valuedouble) == code->valuedouble &&
                cJSON_IsNumber(wind) && std::isfinite(wind->valuedouble) &&
                wind->valuedouble >= 0 &&
                cJSON_IsString(unit_item) && unit_item->valuestring != NULL &&
                cJSON_IsString(wind_unit_item) && wind_unit_item->valuestring != NULL) {
                reading->temperature = temp->valuedouble;
                reading->temperature_unit = unit_item->valuestring;
                reading->weather_code = static_cast<int>(code->valuedouble);
                reading->wind_speed = wind->valuedouble;
                reading->wind_speed_unit = wind_unit_item->valuestring;
                success = true;
            } else {
                fprintf(stderr, "Weather request failed: invalid current conditions in JSON response\n");
            }
            cJSON_Delete(json);
        }
    }

    curl_easy_cleanup(curl);
    free(chunk.response);
    return success;
}
