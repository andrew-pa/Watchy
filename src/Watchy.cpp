#include "Watchy.h"
#include "secrets.h"

WatchyRTC Watchy::RTC;
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> Watchy::display(GxEPD2_154_D67(CS, DC, RESET, BUSY));

RTC_DATA_ATTR int guiState = WATCHFACE_STATE;
RTC_DATA_ATTR int menuIndex = 0;
RTC_DATA_ATTR int menuTopIndex = 0;
RTC_DATA_ATTR BMA423 sensor;
RTC_DATA_ATTR bool WIFI_CONFIGURED;
RTC_DATA_ATTR bool BLE_CONFIGURED;
RTC_DATA_ATTR weatherData currentWeather;
RTC_DATA_ATTR int updateCounter = 999;
RTC_DATA_ATTR bool displayFullInit = true;


void Watchy::init(String datetime) {
    esp_sleep_wakeup_cause_t wakeup_reason;
    wakeup_reason = esp_sleep_get_wakeup_cause(); //get wake up reason
    Wire.begin(SDA, SCL); //init i2c
    RTC.init();

    // Init the display here for all cases, if unused, it will do nothing
    display.init(0, displayFullInit, 10, true); // 10ms by spec, and fast pulldown reset
    display.epd2.setBusyCallback(displayBusyCallback);

    switch (wakeup_reason)
    {
        case ESP_SLEEP_WAKEUP_EXT0: //RTC Alarm
            if(guiState == WATCHFACE_STATE){
                showWatchFace(true); //partial updates on tick
            }
            break;
        case ESP_SLEEP_WAKEUP_EXT1: //button Press
            runUI();
            break;
        default: //reset
            RTC.config(datetime);
            _bmaConfig();
            showWatchFace(false); //full update on reset
            break;
    }
    deepSleep();
}

void Watchy::displayBusyCallback(const void*) {
    gpio_wakeup_enable((gpio_num_t)BUSY, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_light_sleep_start();
}

void Watchy::deepSleep() {
    display.hibernate();
    displayFullInit = false; // Notify not to init it again
    RTC.clearAlarm(); //resets the alarm flag in the RTC
     // Set pins 0-39 to input to avoid power leaking out
    for(int i=0; i<40; i++) {
        pinMode(i, INPUT);
    }
    esp_sleep_enable_ext0_wakeup(RTC_PIN, 0); //enable deep sleep wake on RTC interrupt
    esp_sleep_enable_ext1_wakeup(BTN_PIN_MASK, ESP_EXT1_WAKEUP_ANY_HIGH); //enable deep sleep wake on button press
    esp_deep_sleep_start();
}

uint64_t Watchy::readButtonState() {
    return
        (uint64_t)digitalRead(MENU_BTN_PIN) << MENU_BTN_PIN |
        (uint64_t)digitalRead(BACK_BTN_PIN) << BACK_BTN_PIN |
        (uint64_t)digitalRead(UP_BTN_PIN) << UP_BTN_PIN |
        (uint64_t)digitalRead(DOWN_BTN_PIN) << DOWN_BTN_PIN;
}

void Watchy::runUI() {
    pinMode(MENU_BTN_PIN, INPUT);
    pinMode(BACK_BTN_PIN, INPUT);
    pinMode(UP_BTN_PIN, INPUT);
    pinMode(DOWN_BTN_PIN, INPUT);
    long timeoutStart = millis();
    uint64_t systemState = esp_sleep_get_ext1_wakeup_status();
    while(true) {
        switch(guiState) {
            case WATCHFACE_STATE:
                if(systemState & MENU_BTN_MASK) {
                    showMenu(false);
                } else if(systemState & BACK_BTN_MASK) {
                    showAltFace(false);
                } else {
                    return;
                }
                break;
            case ALTFACE_STATE:
                if(systemState & MENU_BTN_MASK) {
                    showMenu(false);
                } else if(systemState & BACK_BTN_MASK) {
                    showWatchFace(false);
                } else {
                    return;
                }
                break;
            case MAIN_MENU_STATE:
                if(systemState & MENU_BTN_MASK) {
                    switch(menuIndex) {
                        case 0:
                            updateCounter = settings.updateInterval - 1;
                            showWatchFace(false);
                            break;
                        case 1: showBattery(); break;
                        case 2: showAccelerometer(); break;
                        case 3: setTime(); break;
                        case 4: showSyncNTP(); break;
                        case 5: setupWifi(); break;
                        case 6: showBuzz(); break;
                        case 7: showUpdateFW(); break;
                        default: break;
                    }
                } else if(systemState & BACK_BTN_MASK) {
                    showWatchFace(false);
                } else {
                    bool partialRefresh = true;
                    if(systemState & UP_BTN_MASK) {
                        menuIndex--;
                        if(menuIndex < 0) menuIndex = 0;
                        if(menuIndex < menuTopIndex) {
                            menuTopIndex -= MENU_PAGE_LENGTH;
                            if(menuTopIndex < 0) menuTopIndex = 0;
                            partialRefresh = false;
                        }
                    } else if(systemState & DOWN_BTN_MASK) {
                        menuIndex++;
                        if(menuIndex > MENU_LENGTH) menuIndex = MENU_LENGTH;
                        if(menuIndex >= menuTopIndex+MENU_PAGE_LENGTH) {
                            menuTopIndex += MENU_PAGE_LENGTH;
                            partialRefresh = false;
                        }
                    }
                    showMenu(partialRefresh);
                }
                break;
            case APP_STATE:
                if(systemState & BACK_BTN_MASK) {
                    showMenu(false);
                }
                break;
            case FW_UPDATE_STATE:
                if(systemState & MENU_BTN_MASK) {
                    updateFWBegin();
                } else if(systemState & BACK_BTN_MASK) {
                    showMenu(false);
                }
                break;
        }

        // go back to sleep after 3 seconds of inactivity
        if(systemState != 0) timeoutStart = millis();
        else if(millis() - timeoutStart > 3000) return;

        systemState = readButtonState();
    }
}

void Watchy::showMenu(bool partialRefresh) {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);

    int16_t  x1, y1;
    uint16_t w, h;
    int16_t yPos;

    const char *menuItems[] = {
        "Sync Now",
        "Battery Voltage",
        "Show Accelerometer",
        "Set Time",
        "Sync NTP",
        "Setup WiFi",

        "Vibrate Motor",
        "Update Firmware",
        ".",
        ".",
        ".",
        "."
    };
    for(int i = 0; i < MENU_PAGE_LENGTH; i++){
        int j = menuTopIndex + i;
        if(j > MENU_LENGTH) break;
        yPos = 30+(MENU_HEIGHT*i);
        display.setCursor(0, yPos);
        if(j == menuIndex) {
            display.getTextBounds(menuItems[j], 0, yPos, &x1, &y1, &w, &h);
            display.fillRect(x1-1, y1-10, 200, h+15, GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.println(menuItems[j]);
        } else {
            display.setTextColor(GxEPD_WHITE);
            display.println(menuItems[j]);
        }
    }

    // draw scroll bar
    display.setCursor(0, 0);
    display.fillRect(190,
            16.f + ((float)menuIndex/(float)MENU_LENGTH)*168.f,
            6, 168.f / (float)MENU_LENGTH, GxEPD_WHITE);

    display.display(partialRefresh);

    guiState = MAIN_MENU_STATE;
}

void Watchy::showBattery(){
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(20, 30);
    display.println("Battery Voltage:");
    float voltage = getBatteryVoltage();
    display.setCursor(70, 80);
    display.print(voltage);
    display.println("V");
    display.display(false); //full refresh

    guiState = APP_STATE;
}

void Watchy::showBuzz(){
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(70, 80);
    display.println("Buzz!");
    display.display(false); //full refresh
    vibMotor();
    showMenu(false);
}

void Watchy::vibMotor(uint8_t intervalMs, uint8_t length){
    pinMode(VIB_MOTOR_PIN, OUTPUT);
    bool motorOn = false;
    for(int i=0; i<length; i++){
        motorOn = !motorOn;
        digitalWrite(VIB_MOTOR_PIN, motorOn);
        delay(intervalMs);
    }
}

void Watchy::setTime(){
    guiState = APP_STATE;

    RTC.read(currentTime);

    int8_t minute = currentTime.Minute;
    int8_t hour = currentTime.Hour;
    int8_t day = currentTime.Day;
    int8_t month = currentTime.Month;
    int8_t year = tmYearToY2k(currentTime.Year);

    int8_t setIndex = SET_HOUR;

    int8_t blink = 0;

    pinMode(DOWN_BTN_PIN, INPUT);
    pinMode(UP_BTN_PIN, INPUT);
    pinMode(MENU_BTN_PIN, INPUT);
    pinMode(BACK_BTN_PIN, INPUT);

    display.setFullWindow();

    while(1){

    if(digitalRead(MENU_BTN_PIN) == 1){
        setIndex++;
        if(setIndex > SET_DAY){
        break;
        }
    }
    if(digitalRead(BACK_BTN_PIN) == 1){
        if(setIndex != SET_HOUR){
        setIndex--;
        }
    }

    blink = 1 - blink;

    if(digitalRead(DOWN_BTN_PIN) == 1){
        blink = 1;
        switch(setIndex){
        case SET_HOUR:
            hour == 23 ? (hour = 0) : hour++;
            break;
        case SET_MINUTE:
            minute == 59 ? (minute = 0) : minute++;
            break;
        case SET_YEAR:
            year == 99 ? (year = 0) : year++;
            break;
        case SET_MONTH:
            month == 12 ? (month = 1) : month++;
            break;
        case SET_DAY:
            day == 31 ? (day = 1) : day++;
            break;
        default:
            break;
        }
    }

    if(digitalRead(UP_BTN_PIN) == 1){
        blink = 1;
        switch(setIndex){
        case SET_HOUR:
            hour == 0 ? (hour = 23) : hour--;
            break;
        case SET_MINUTE:
            minute == 0 ? (minute = 59) : minute--;
            break;
        case SET_YEAR:
            year == 0 ? (year = 99) : year--;
            break;
        case SET_MONTH:
            month == 1 ? (month = 12) : month--;
            break;
        case SET_DAY:
            day == 1 ? (day = 31) : day--;
            break;
        default:
            break;
        }
    }

    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
    display.setFont(&DSEG7_Classic_Bold_53);

    display.setCursor(5, 80);
    if(setIndex == SET_HOUR){//blink hour digits
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if(hour < 10){
        display.print("0");
    }
    display.print(hour);

    display.setTextColor(GxEPD_WHITE);
    display.print(":");

    display.setCursor(108, 80);
    if(setIndex == SET_MINUTE){//blink minute digits
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if(minute < 10){
        display.print("0");
    }
    display.print(minute);

    display.setTextColor(GxEPD_WHITE);

    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(45, 150);
    if(setIndex == SET_YEAR){//blink minute digits
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    display.print(2000+year);

    display.setTextColor(GxEPD_WHITE);
    display.print("/");

    if(setIndex == SET_MONTH){//blink minute digits
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if(month < 10){
        display.print("0");
    }
    display.print(month);

    display.setTextColor(GxEPD_WHITE);
    display.print("/");

    if(setIndex == SET_DAY){//blink minute digits
        display.setTextColor(blink ? GxEPD_WHITE : GxEPD_BLACK);
    }
    if(day < 10){
        display.print("0");
    }
    display.print(day);
    display.display(true); //partial refresh
    }

    tmElements_t tm;
    tm.Month = month;
    tm.Day = day;
    tm.Year = y2kYearToTm(year);
    tm.Hour = hour;
    tm.Minute = minute;
    tm.Second = 0;

    RTC.set(tm);

    showMenu(false);

}

void Watchy::showAccelerometer(){
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);

    Accel acc;

    long previousMillis = 0;
    long interval = 200;

    guiState = APP_STATE;

    pinMode(BACK_BTN_PIN, INPUT);

    while(1){

    unsigned long currentMillis = millis();

    if(digitalRead(BACK_BTN_PIN) == 1){
        break;
    }

    if(currentMillis - previousMillis > interval){
        previousMillis = currentMillis;
        // Get acceleration data
        bool res = sensor.getAccel(acc);
        uint8_t direction = sensor.getDirection();
        display.fillScreen(GxEPD_BLACK);
        display.setCursor(0, 30);
        if(res == false) {
            display.println("getAccel FAIL");
        }else{
        display.print("  X:"); display.println(acc.x);
        display.print("  Y:"); display.println(acc.y);
        display.print("  Z:"); display.println(acc.z);

        display.setCursor(30, 130);
        switch(direction){
            case DIRECTION_DISP_DOWN:
                display.println("FACE DOWN");
                break;
            case DIRECTION_DISP_UP:
                display.println("FACE UP");
                break;
            case DIRECTION_BOTTOM_EDGE:
                display.println("BOTTOM EDGE");
                break;
            case DIRECTION_TOP_EDGE:
                display.println("TOP EDGE");
                break;
            case DIRECTION_RIGHT_EDGE:
                display.println("RIGHT EDGE");
                break;
            case DIRECTION_LEFT_EDGE:
                display.println("LEFT EDGE");
                break;
            default:
                display.println("ERROR!!!");
                break;
        }

        }
        display.display(true); //full refresh
    }
    }

    showMenu(false);
}

bool Watchy::networkUpdate() {
  if(updateCounter >= settings.updateInterval) {
      updateCounter = 0;
      if(connectWiFi()) {
          syncNTP();
          HTTPClient http; //Use Weather API for live data if WiFi is connected
          http.setConnectTimeout(3000);//3 second max timeout
          http.begin(NETWORK_UPDATE_URL);
          int httpResponseCode = http.GET();
          if(httpResponseCode == 200) {
              String payload = http.getString();
              loadWeatherData(&currentWeather, payload.c_str());
          } else {
              //http error
          }
          http.end();

          WiFi.mode(WIFI_OFF);
          btStop();
      } else {
          // updateCounter = -60;
      }
  } else {
      updateCounter++;
  }
  RTC.read(currentTime);
  return false;
}

void Watchy::showWatchFace(bool partialRefresh){
    networkUpdate();
    display.setFullWindow();
    drawWatchFace();
    display.display(partialRefresh);
    guiState = WATCHFACE_STATE;
}

void Watchy::drawWatchFace(){
    display.setFont(&DSEG7_Classic_Bold_53);
    display.setCursor(5, 53+60);
    if(currentTime.Hour < 10){
        display.print("0");
    }
    display.print(currentTime.Hour);
    display.print(":");
    if(currentTime.Minute < 10){
        display.print("0");
    }
    display.println(currentTime.Minute);
}

void Watchy::showAltFace(bool partialRefresh) {
    networkUpdate();
    display.setFullWindow();
    drawAltFace();
    display.display(partialRefresh);
    guiState = ALTFACE_STATE;
}

void Watchy::drawAltFace() {
    display.setFont(&DSEG7_Classic_Bold_53);
    display.setCursor(5, 53+60);
    display.println(updateCounter);
}

weatherData* Watchy::getWeatherData(){
    return &currentWeather;
}

float Watchy::getBatteryVoltage(){
    if(RTC.rtcType == DS3231){
        return analogReadMilliVolts(V10_ADC_PIN) / 1000.0f * 2.0f; // Battery voltage goes through a 1/2 divider.
    }else{
        return analogReadMilliVolts(V15_ADC_PIN) / 1000.0f * 2.0f;
    }
}

uint16_t Watchy::_readRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)address, (uint8_t)len);
    uint8_t i = 0;
    while (Wire.available()) {
        data[i++] = Wire.read();
    }
    return 0;
}

uint16_t Watchy::_writeRegister(uint8_t address, uint8_t reg, uint8_t *data, uint16_t len) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(data, len);
    return (0 !=  Wire.endTransmission());
}

void Watchy::_bmaConfig(){

    if (sensor.begin(_readRegister, _writeRegister, delay) == false) {
        //fail to init BMA
        return;
    }

    // Accel parameter structure
    Acfg cfg;
    /*!
        Output data rate in Hz, Optional parameters:
            - BMA4_OUTPUT_DATA_RATE_0_78HZ
            - BMA4_OUTPUT_DATA_RATE_1_56HZ
            - BMA4_OUTPUT_DATA_RATE_3_12HZ
            - BMA4_OUTPUT_DATA_RATE_6_25HZ
            - BMA4_OUTPUT_DATA_RATE_12_5HZ
            - BMA4_OUTPUT_DATA_RATE_25HZ
            - BMA4_OUTPUT_DATA_RATE_50HZ
            - BMA4_OUTPUT_DATA_RATE_100HZ
            - BMA4_OUTPUT_DATA_RATE_200HZ
            - BMA4_OUTPUT_DATA_RATE_400HZ
            - BMA4_OUTPUT_DATA_RATE_800HZ
            - BMA4_OUTPUT_DATA_RATE_1600HZ
    */
    cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
    /*!
        G-range, Optional parameters:
            - BMA4_ACCEL_RANGE_2G
            - BMA4_ACCEL_RANGE_4G
            - BMA4_ACCEL_RANGE_8G
            - BMA4_ACCEL_RANGE_16G
    */
    cfg.range = BMA4_ACCEL_RANGE_2G;
    /*!
        Bandwidth parameter, determines filter configuration, Optional parameters:
            - BMA4_ACCEL_OSR4_AVG1
            - BMA4_ACCEL_OSR2_AVG2
            - BMA4_ACCEL_NORMAL_AVG4
            - BMA4_ACCEL_CIC_AVG8
            - BMA4_ACCEL_RES_AVG16
            - BMA4_ACCEL_RES_AVG32
            - BMA4_ACCEL_RES_AVG64
            - BMA4_ACCEL_RES_AVG128
    */
    cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;

    /*! Filter performance mode , Optional parameters:
        - BMA4_CIC_AVG_MODE
        - BMA4_CONTINUOUS_MODE
    */
    cfg.perf_mode = BMA4_CONTINUOUS_MODE;

    // Configure the BMA423 accelerometer
    sensor.setAccelConfig(cfg);

    // Enable BMA423 accelerometer
    // Warning : Need to use feature, you must first enable the accelerometer
    // Warning : Need to use feature, you must first enable the accelerometer
    sensor.enableAccel();

    struct bma4_int_pin_config config ;
    config.edge_ctrl = BMA4_LEVEL_TRIGGER;
    config.lvl = BMA4_ACTIVE_HIGH;
    config.od = BMA4_PUSH_PULL;
    config.output_en = BMA4_OUTPUT_ENABLE;
    config.input_en = BMA4_INPUT_DISABLE;
    // The correct trigger interrupt needs to be configured as needed
    sensor.setINTPinConfig(config, BMA4_INTR1_MAP);

    struct bma423_axes_remap remap_data;
    remap_data.x_axis = 1;
    remap_data.x_axis_sign = 0xFF;
    remap_data.y_axis = 0;
    remap_data.y_axis_sign = 0xFF;
    remap_data.z_axis = 2;
    remap_data.z_axis_sign = 0xFF;
    // Need to raise the wrist function, need to set the correct axis
    sensor.setRemapAxes(&remap_data);

    // Enable BMA423 isStepCounter feature
    sensor.enableFeature(BMA423_STEP_CNTR, true);
    // Enable BMA423 isTilt feature
    sensor.enableFeature(BMA423_TILT, true);
    // Enable BMA423 isDoubleClick feature
    sensor.enableFeature(BMA423_WAKEUP, true);

    // Reset steps
    sensor.resetStepCounter();

    // Turn on feature interrupt
    sensor.enableStepCountInterrupt();
    sensor.enableTiltInterrupt();
    // It corresponds to isDoubleClick interrupt
    sensor.enableWakeupInterrupt();
}

void Watchy::setupWifi(){
    display.epd2.setBusyCallback(0); //temporarily disable lightsleep on busy
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    wifiManager.setTimeout(WIFI_AP_TIMEOUT);
    wifiManager.setAPCallback(_configModeCallback);
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    if(!wifiManager.autoConnect(WIFI_AP_SSID)) {//WiFi setup failed
        display.println("Setup failed &");
        display.println("timed out!");
    }else{
        display.println("Connected to");
        display.println(WiFi.SSID());
    }
    display.display(false); //full refresh
    //turn off radios
    WiFi.mode(WIFI_OFF);
    btStop();
    display.epd2.setBusyCallback(displayBusyCallback); //enable lightsleep on busy
    guiState = APP_STATE;
}

void Watchy::_configModeCallback (WiFiManager *myWiFiManager) {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(0, 30);
    display.println("Connect to");
    display.print("SSID: ");
    display.println(WIFI_AP_SSID);
    display.print("IP: ");
    display.println(WiFi.softAPIP());
    display.display(false); //full refresh
}

bool Watchy::connectWiFi(){
    if(WL_CONNECT_FAILED == WiFi.begin(WIFI_SSID, WIFI_PASSWORD)){//WiFi not setup, you can also use hard coded credentials with WiFi.begin(SSID,PASS);
        WIFI_CONFIGURED = false;
    }else{
        if(WL_CONNECTED == WiFi.waitForConnectResult()){//attempt to connect for 10s
            WIFI_CONFIGURED = true;
        }else{//connection failed, time out
            WIFI_CONFIGURED = false;
            //turn off radios
            WiFi.mode(WIFI_OFF);
            btStop();
        }
    }
    return WIFI_CONFIGURED;
}

void Watchy::showUpdateFW(){
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(0, 30);
    display.println("Please visit");
    display.println("watchy.sqfmi.com");
    display.println("with a Bluetooth");
    display.println("enabled device");
    display.println(" ");
    display.println("Press menu button");
    display.println("again when ready");
    display.println(" ");
    display.println("Keep USB powered");
    display.display(false); //full refresh

    guiState = FW_UPDATE_STATE;
}

void Watchy::updateFWBegin(){
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(0, 30);
    display.println("Bluetooth Started");
    display.println(" ");
    display.println("Watchy BLE OTA");
    display.println(" ");
    display.println("Waiting for");
    display.println("connection...");
    display.display(false); //full refresh

    BLE BT;
    BT.begin("Watchy BLE OTA");
    int prevStatus = -1;
    int currentStatus;

    while(1){
    currentStatus = BT.updateStatus();
    if(prevStatus != currentStatus || prevStatus == 1){
        if(currentStatus == 0){
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("BLE Connected!");
        display.println(" ");
        display.println("Waiting for");
        display.println("upload...");
        display.display(false); //full refresh
        }
        if(currentStatus == 1){
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("Downloading");
        display.println("firmware:");
        display.println(" ");
        display.print(BT.howManyBytes());
        display.println(" bytes");
        display.display(true); //partial refresh
        }
        if(currentStatus == 2){
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("Download");
        display.println("completed!");
        display.println(" ");
        display.println("Rebooting...");
        display.display(false); //full refresh

        delay(2000);
        esp_restart();
        }
        if(currentStatus == 4){
        display.setFullWindow();
        display.fillScreen(GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(0, 30);
        display.println("BLE Disconnected!");
        display.println(" ");
        display.println("exiting...");
        display.display(false); //full refresh
        delay(1000);
        break;
        }
        prevStatus = currentStatus;
    }
    delay(100);
    }

    //turn off radios
    WiFi.mode(WIFI_OFF);
    btStop();
    showMenu(false);
}

void Watchy::showSyncNTP() {
    display.setFullWindow();
    display.fillScreen(GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(0, 30);
    display.println("Syncing NTP... ");
    display.display(false); //full refresh
    if(connectWiFi()) {
        if(syncNTP()) {
            display.println("NTP Sync Success\n");
            display.println("Current Time Is:");

            RTC.read(currentTime);

            display.print(tmYearToCalendar(currentTime.Year));
            display.print("/");
            display.print(currentTime.Month);
            display.print("/");
            display.print(currentTime.Day);
            display.print(" - ");

            if(currentTime.Hour < 10){
                display.print("0");
            }
            display.print(currentTime.Hour);
            display.print(":");
            if(currentTime.Minute < 10){
                display.print("0");
            }  
            display.println(currentTime.Minute);
        }else{
            display.println("NTP Sync Failed");
        }
    }else{
        display.println("WiFi Not Configured");
    }
    display.display(true); //full refresh
    //delay(3000);
    showMenu(false);
}

bool Watchy::syncNTP(long gmt, int dst, String ntpServer){ //NTP sync - call after connecting to WiFi and remember to turn it back off
    WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, ntpServer.c_str(), gmt);
    timeClient.begin();
    if(!timeClient.forceUpdate()){
        return false; //NTP sync failed
    }
    tmElements_t tm;
    breakTime((time_t)timeClient.getEpochTime(), tm);
    RTC.set(tm);
    return true;
}
