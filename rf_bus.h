#pragma once
#include <Arduino.h>

namespace X360RfBus {
void beginPinsOriginal();
bool initializeModuleOriginal();
bool startBootAnimationOriginal();
bool finishBootLedInitOriginal();
bool startSyncOriginal();
bool setGreenMask(uint8_t mask);
bool sendCommand10(uint16_t command);
}
