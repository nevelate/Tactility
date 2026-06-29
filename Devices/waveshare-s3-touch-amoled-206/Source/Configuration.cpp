#include "devices/AudioDevice.h"
#include "devices/Display.h"
#include "devices/Touch.h"
#include "devices/Sdcard.h"
#include "devices/Power.h"

#include <Tactility/hal/Configuration.h>
#include <Tactility/settings/AudioSettings.h>
#include <Axp2101Power.h>

#include <tactility/device.h>
#include <tactility/log.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_descriptor.h>
#include <Tactility/app/App.h>
#include <Tactility/app/dialogbox/DialogBox.h>
#include <Tactility/hal/power/PowerDevice.h>
#include <Tactility/lvgl/LvglSync.h>
#include <driver/gpio.h>

#include <ButtonControl.h>

#ifdef ESP_PLATFORM
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#define TOP_BUTTON GPIO_NUM_0
#define BOTTOM_BUTTON GPIO_NUM_10

using namespace tt::hal;

static constexpr const char* TAG = "WaveshareAMOLED";

static ::Device* findI2C() {
    ::Device* i2c = device_find_by_name("i2c0");
    if (i2c) return i2c;
    i2c = device_find_by_name("i2c_internal");
    if (i2c) return i2c;
    i2c = device_find_by_name("esp32_i2c");
    if (i2c) return i2c;
    return nullptr;
}

static bool initBoot() {
    ESP_LOGI(TAG, "initBoot()");

    // setup_reset_button();

    ::Device* i2c0 = findI2C();
    if (!i2c0) {
        LOG_E(TAG, "I2C controller not found");
        return false;
    }
    ESP_LOGI(TAG, "I2C found: %s", i2c0->name);

    error_t err = i2c_controller_has_device_at_address(i2c0, 0x51, pdMS_TO_TICKS(100));
    if (err == ERROR_NONE) {
        LOG_I(TAG, "Found device at 0x51 (RTC PCF85063)");
    } else {
        LOG_W(TAG, "No RTC at 0x51");
    }

    err = i2c_controller_has_device_at_address(i2c0, 0x6B, pdMS_TO_TICKS(100));
    if (err == ERROR_NONE) {
        LOG_I(TAG, "Found device at 0x6B (IMU QMI8658)");
    } else {
        LOG_W(TAG, "No IMU at 0x6B");
    }

    err = i2c_controller_has_device_at_address(i2c0, 0x38, pdMS_TO_TICKS(100));
    if (err == ERROR_NONE) {
        LOG_I(TAG, "Found device at 0x38 (Touch FT3168)");
    } else {
        LOG_W(TAG, "No Touch at 0x38");
    }

    err = i2c_controller_has_device_at_address(i2c0, 0x34, pdMS_TO_TICKS(100));
    if (err == ERROR_NONE) {
        LOG_I(TAG, "Found device at 0x34 (AXP2101)");
        initAxp();
    } else {
        LOG_W(TAG, "No AXP2101 at 0x34");
    }

    err = i2c_controller_has_device_at_address(i2c0, 0x30, pdMS_TO_TICKS(100));
    if (err == ERROR_NONE) {
        LOG_I(TAG, "Found device at 0x30 (Audio ES8311)");
    } else {
        LOG_W(TAG, "No ES8311 at 0x30");
    }

    err = i2c_controller_has_device_at_address(i2c0, 0x80, pdMS_TO_TICKS(100));
    if (err == ERROR_NONE) {
        LOG_I(TAG, "Found device at 0x80 (Audio ES7210)");
    } else {
        LOG_W(TAG, "No ES7210 at 0x80");
    }

    return true;
}

static DeviceVector createDevices() {
    auto devices = DeviceVector();

    std::shared_ptr<Axp2101> axp = std::static_pointer_cast<Axp2101>(getAxp2101());
    if (axp) {
        devices.push_back(axp);
        devices.push_back(std::make_shared<Axp2101Power>(axp));
    }

    auto buttons = std::make_shared<ButtonControl>(std::vector {
        ButtonControl::PinConfiguration {
            .pin = TOP_BUTTON,
            .event = ButtonControl::Event::ShortPress,
            .action = ButtonControl::Action::UiPressSelected
        },
        ButtonControl::PinConfiguration {
            .pin = TOP_BUTTON,
            .event = ButtonControl::Event::LongPress,
            .action = ButtonControl::Action::AppClose
        }
    });

    devices.push_back(buttons);

    auto touch = createTouch();
    auto display = createDisplay(touch);
    if (display) {
        // Touch is auto-registered via display->getTouchDevice() in Hal::registerDevices()
        devices.push_back(display);
    }

    // SD card on SPI1 (bypassing device tree to avoid conflict)
    auto sdcard = createSdCard();
    if (sdcard) {
        devices.push_back(sdcard);
    }

    // Audio codecs - create both first, then open together
    auto audioSettings = tt::settings::audio::loadOrGetDefault();

    auto speaker = waveshare::audio::createSpeakerDevice();
    auto microphone = waveshare::audio::createMicrophoneDevice();

    if (speaker || microphone) {
        waveshare::audio::initAudioCodecs(speaker, microphone);
    }

    if (speaker && speaker->isValid()) {
        speaker->setVolume(audioSettings.volume);
        speaker->setMuted(audioSettings.muted);
        devices.push_back(speaker);
    }

    if (microphone && microphone->isValid()) {
        devices.push_back(microphone);
    }

    return devices;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};