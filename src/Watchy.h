#ifndef WATCHY_H
#define WATCHY_H

#include <Arduino.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Arduino_JSON.h>
#include <GxEPD2_BW.h>
#include <Wire.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include "DSEG7_Classic_Bold_53.h"
#include "WatchyRTC.h"
#include "BLE.h"
#include "bma.h"
#include "config.h"
#include "WeatherData.h"

struct watchySettings {
    int8_t updateInterval;
    //NTP Settings
    String ntpServer;
    int gmtOffset;
    int dstOffset;
};



class Watchy {
    public:
        static WatchyRTC RTC;
        static GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display;
        tmElements_t currentTime;
        watchySettings settings;
    public:
        explicit Watchy(const watchySettings& s) : settings(s) {};
        void init(String datetime = "");
        void deepSleep();
        static void displayBusyCallback(const void*);
        float getBatteryVoltage();
        void vibMotor(uint8_t intervalMs = 100, uint8_t length = 20);

        void runUI();
        uint64_t readButtonState();
        void showMenu(bool partialRefresh);
        void showBattery();
        void showBuzz();
        void showAccelerometer();
        void showUpdateFW();
        void setTime();
        void showSyncNTP();
        void setupWifi();
        bool connectWiFi();
        weatherData* getWeatherData();
        void updateFWBegin();
        bool networkUpdate();
        bool syncNTP(long gmt, int dst, String ntpServer);

        inline bool syncNTP() { //NTP sync - call after connecting to WiFi and remember to turn it back off
            return syncNTP(settings.gmtOffset, settings.dstOffset, settings.ntpServer.c_str());
        }


        void showWatchFace(bool partialRefresh);
        virtual void drawWatchFace(); //override this method for different watch faces
        void showAltFace(bool partialRefresh);
        virtual void drawAltFace();

    private:
        void _bmaConfig();
        static void _configModeCallback(WiFiManager *myWiFiManager);
        static uint16_t _readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len);
        static uint16_t _writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len);
};

extern RTC_DATA_ATTR int guiState;
extern RTC_DATA_ATTR int menuIndex;
extern RTC_DATA_ATTR int menuTopIndex;
extern RTC_DATA_ATTR BMA423 sensor;
extern RTC_DATA_ATTR bool WIFI_CONFIGURED;
extern RTC_DATA_ATTR bool BLE_CONFIGURED;

#endif

