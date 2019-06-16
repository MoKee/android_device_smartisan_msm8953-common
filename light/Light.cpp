/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright (C) 2018-2019 The MoKee Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "light"

#include "Light.h"

#include <android-base/logging.h>

namespace {
using android::hardware::light::V2_0::LightState;

static constexpr int RAMP_SIZE = 8;
static constexpr int RAMP_STEP_DURATION = 50;

static constexpr int BRIGHTNESS_RAMP[RAMP_SIZE] = {0, 12, 25, 37, 50, 72, 85, 100};
static constexpr int DEFAULT_MAX_BRIGHTNESS = 255;

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static bool isLit(const LightState& state) {
    return (state.color & 0x00ffffff);
}

static std::string getScaledDutyPcts(int brightness) {
    std::string buf, pad;

    for (auto i : BRIGHTNESS_RAMP) {
        buf += pad;
        buf += std::to_string(i * brightness / 255);
        pad = ",";
    }

    return buf;
}
}  // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

Light::Light(std::pair<std::ofstream, uint32_t>&& lcd_backlight,
             std::ofstream&& red_led, std::ofstream&& green_led, std::ofstream&& blue_led,
             std::ofstream&& blue_duty_pcts,
             std::ofstream&& blue_start_idx,
             std::ofstream&& blue_pause_lo,
             std::ofstream&& blue_pause_hi,
             std::ofstream&& blue_ramp_step_ms,
             std::ofstream&& red_blink, std::ofstream&& blue_blink)
    : mLcdBacklight(std::move(lcd_backlight)),
      mRedLed(std::move(red_led)),
      mGreenLed(std::move(green_led)),
      mBlueLed(std::move(blue_led)),
      mBlueDutyPcts(std::move(blue_duty_pcts)),
      mBlueStartIdx(std::move(blue_start_idx)),
      mBluePauseLo(std::move(blue_pause_lo)),
      mBluePauseHi(std::move(blue_pause_hi)),
      mBlueRampStepMs(std::move(blue_ramp_step_ms)),
      mRedBlink(std::move(red_blink)),
      mBlueBlink(std::move(blue_blink)) {
    auto backlightFn(std::bind(&Light::setLcdBacklight, this, std::placeholders::_1));
    auto attentionFn(std::bind(&Light::setAttentionLight, this, std::placeholders::_1));
    auto batteryFn(std::bind(&Light::setBatteryLight, this, std::placeholders::_1));
    auto notificationFn(std::bind(&Light::setNotificationLight, this, std::placeholders::_1));
    mLights.emplace(std::make_pair(Type::BACKLIGHT, backlightFn));
    mLights.emplace(std::make_pair(Type::ATTENTION, attentionFn));
    mLights.emplace(std::make_pair(Type::BATTERY, batteryFn));
    mLights.emplace(std::make_pair(Type::NOTIFICATIONS, notificationFn));
}

// Methods from ::android::hardware::light::V2_0::ILight follow.
Return<Status> Light::setLight(Type type, const LightState& state) {
    auto it = mLights.find(type);

    if (it == mLights.end()) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    it->second(state);

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    for (auto const& light : mLights) {
        types.push_back(light.first);
    }

    _hidl_cb(types);

    return Void();
}

void Light::setLcdBacklight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);

    uint32_t brightness = rgbToBrightness(state);

    // If max panel brightness is not the default (255),
    // apply linear scaling across the accepted range.
    if (mLcdBacklight.second != DEFAULT_MAX_BRIGHTNESS) {
        int old_brightness = brightness;
        brightness = brightness * mLcdBacklight.second / DEFAULT_MAX_BRIGHTNESS;
        LOG(VERBOSE) << "scaling brightness " << old_brightness << " => " << brightness;
    }

    mLcdBacklight.first << brightness << std::endl;
}

void Light::setAttentionLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mAttentionState = state;
    handleSpeakerBatteryLightLocked();
}

void Light::setBatteryLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mBatteryState = state;
    handleSpeakerBatteryLightLocked();
}

void Light::setNotificationLight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    mNotificationState = state;
    handleSpeakerBatteryLightLocked();
}

void Light::handleSpeakerBatteryLightLocked() {
    mRedLed << 0 << std::endl;
    mRedBlink << 0 << std::endl;
    mGreenLed << 0 << std::endl;
    mBlueLed << 0 << std::endl;
    mBlueBlink << 0 << std::endl;

    if (isLit(mNotificationState)) {
        setNotificationLightLocked(mNotificationState);
    } else if (isLit(mAttentionState)) {
        setNotificationLightLocked(mAttentionState);
    } else if (isLit(mBatteryState)) {
        setBatteryLightLocked(mBatteryState);
    }
}

void Light::setNotificationLightLocked(const LightState& state) {
    int brightness;
    int blink;
    int onMs, offMs;
    int stepDuration, pauseHi;

    switch (state.flashMode) {
        case Flash::TIMED:
            onMs = state.flashOnMs;
            offMs = state.flashOffMs;
            break;
        case Flash::NONE:
        default:
            onMs = 0;
            offMs = 0;
            break;
    }

    brightness = (state.color & 0xff000000) >> 24;
    blink = onMs > 0 && offMs > 0;

    if (blink) {
        stepDuration = RAMP_STEP_DURATION;
        pauseHi = onMs - (stepDuration * RAMP_SIZE * 2);

        if (stepDuration * RAMP_SIZE * 2 > onMs) {
            stepDuration = onMs / (RAMP_SIZE * 2);
            pauseHi = 0;
        }

        mBlueStartIdx << 0 << std::endl;
        mBlueDutyPcts << getScaledDutyPcts(brightness) << std::endl;
        mBluePauseLo << offMs << std::endl;
        mBluePauseHi << pauseHi << std::endl;
        mBlueRampStepMs << stepDuration << std::endl;

        // Start the party
        mBlueBlink << 1 << std::endl;
    } else {
        mBlueLed << brightness << std::endl;
    }
}

void Light::setBatteryLightLocked(const LightState& state) {
    int level = (state.color & 0xff000000) >> 24;
    if (level > 10) {
        mGreenLed << 1 << std::endl;
    } else {
        mRedLed << 1 << std::endl;
    }
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
