#pragma once

struct forecast {
    float temperature, humidity;
    unsigned short condition_code;
};

struct daily_forecast {
    float temperature, humidity, temp_min, temp_max;
    unsigned short condition_code;
};

struct weatherData {
    forecast current;
    forecast hourly[4];
    daily_forecast daily[8];
};

void loadWeatherData(weatherData* data, const char* src);
