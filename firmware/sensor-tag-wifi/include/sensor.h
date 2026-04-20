#pragma once

#include "reading.h"

/// Compile-time sensor abstraction. Exactly one implementation is linked
/// based on `-DSENSOR_SHT31` or `-DSENSOR_DS18B20` PlatformIO env.
///
/// Semantics: populate `r.temp1`/`r.temp2` (and `r.humidity1`/`r.humidity2`
/// if supported). The caller fills `r.timestamp` and `r.battery_pct`.
namespace Sensor {

/// Power up and initialize the sensor bus. Returns false on hardware failure.
bool begin();

/// Perform a blocking read. Populates reading fields. Returns false on error.
bool read(Reading& r);

/// Power down / release peripherals before deep sleep.
void deinit();

}  // namespace Sensor
