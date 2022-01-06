#include "WeatherData.h"

void loadForecast(forecast* dest, const char*& src) {
    dest->temperature = *((float*)src);
    src += sizeof(float);
    dest->humidity = *((float*)src);
    src += sizeof(float);
    dest->condition_code = *((unsigned short*)src);
    src += sizeof(unsigned short);
}

void loadDailyForecast(daily_forecast* dest, const char*& src) {
    dest->temperature = *((float*)src);
    src += sizeof(float);
    dest->humidity = *((float*)src);
    src += sizeof(float);
    dest->temp_min = *((float*)src);
    src += sizeof(float);
    dest->temp_max = *((float*)src);
    src += sizeof(float);
    dest->condition_code = *((unsigned short*)src);
    src += sizeof(unsigned short);
}

void loadWeatherData(weatherData* data, const char* src) {
    loadForecast(&data->current, src);
    for(int i = 0; i < 4; ++i)
        loadForecast(&data->hourly[i], src);
    for(int i = 0; i < 8; ++i)
        loadDailyForecast(&data->daily[i], src);
}
