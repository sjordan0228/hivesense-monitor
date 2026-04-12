#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "state_machine.h"
#include "power_manager.h"
#include "storage.h"

RTC_DATA_ATTR static uint32_t bootCount = 0;

void setup() {
    Serial.begin(115200);
    bootCount++;

    Serial.printf("\n[MAIN] HiveSense Node — Phase 1 | Boot #%u\n", bootCount);

    PowerManager::initialize();
    Storage::initialize();
}

void loop() {
    static NodeState currentState = StateMachine::determineInitialState();
    static HivePayload payload;

    currentState = StateMachine::executeState(currentState, payload);
}
