#include <Arduino.h>
#include "config.h"
#include "types.h"

void setup() {
    Serial.begin(115200);
    Serial.println("[MAIN] HiveSense Collector — starting");
}

void loop() {
    delay(1000);
}
