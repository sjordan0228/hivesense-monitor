#pragma once

// =============================================================================
// Module Interface Convention
// =============================================================================
//
// Every sensor/subsystem module follows this pattern:
//
//   namespace ModuleName {
//       bool initialize();
//           — Power on hardware, configure, validate communication.
//           — Returns false if hardware not detected or init fails.
//
//       bool readMeasurements(HivePayload& payload);
//           — Read sensor data and populate relevant fields in payload.
//           — Returns false if read fails.
//
//       void enterSleep();
//           — Power off hardware, release resources, set pins low.
//   }
//
// Modules do not depend on each other. They interact only through
// the shared HivePayload struct passed by the state machine dispatcher.
// =============================================================================
