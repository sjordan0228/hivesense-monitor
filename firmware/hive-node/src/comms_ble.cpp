#include "comms_ble.h"
#include "config.h"
#include "types.h"
#include "storage.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

namespace {

NimBLEServer*         bleServer        = nullptr;
NimBLECharacteristic* charSensorLog    = nullptr;
NimBLECharacteristic* charReadingCount = nullptr;
NimBLECharacteristic* charHiveId       = nullptr;
NimBLECharacteristic* charClearLog     = nullptr;

// Written from BLE callback context, read from main task
volatile bool deviceConnected = false;
volatile bool syncComplete    = false;
volatile bool sensorLogSubscribed = false;

// ---------------------------------------------------------------------------
// BLE Callbacks
// ---------------------------------------------------------------------------

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server) override {
        deviceConnected = true;
        syncComplete    = false;
        Serial.println("[BLE] Phone connected");
    }

    void onDisconnect(NimBLEServer* server) override {
        deviceConnected = false;
        syncComplete    = true;
        Serial.println("[BLE] Phone disconnected");
    }
};

/// Detects when phone subscribes to sensor log notifications.
class SensorLogCallback : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* characteristic, ble_gap_conn_desc* desc, uint16_t subValue) override {
        if (subValue == 1 || subValue == 2) {  // notify or indicate
            sensorLogSubscribed = true;
            Serial.println("[BLE] Phone subscribed to sensor log");
        }
    }
};

/// Write 0x01 to this characteristic to clear stored readings after sync.
class ClearLogCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.length() != 1 || value[0] != 0x01) {
            return;
        }

        Serial.println("[BLE] Clear command received — erasing stored readings");
        Storage::clearAllReadings();
        syncComplete = true;

        // Update reading count characteristic so phone sees 0
        uint16_t zero = 0;
        charReadingCount->setValue(reinterpret_cast<uint8_t*>(&zero), sizeof(uint16_t));
    }
};

/// Phone writes hive ID during initial pairing to associate this node.
class HiveIdCallback : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.empty() || value.length() >= 16) {
            return;
        }

        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);  // read-write
        prefs.putString(NVS_KEY_HIVE_ID, value.c_str());
        prefs.end();

        Serial.printf("[BLE] Hive ID updated to: %s\n", value.c_str());
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Send all stored readings to the connected phone via BLE notifications.
void sendStoredReadings() {
    uint16_t count = Storage::getReadingCount();
    Serial.printf("[BLE] Sending %u readings via notifications\n", count);

    HivePayload payload;
    for (uint16_t i = 0; i < count; i++) {
        if (!Storage::readReading(i, payload)) {
            Serial.printf("[BLE] ERROR: Failed to read index %u\n", i);
            continue;
        }

        charSensorLog->setValue(
            reinterpret_cast<uint8_t*>(&payload),
            sizeof(HivePayload)
        );
        charSensorLog->notify();

        // Small delay between notifications to avoid BLE congestion
        delay(20);
    }

    Serial.println("[BLE] All readings sent");
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace CommsBle {

bool initialize() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String hiveId = prefs.getString(NVS_KEY_HIVE_ID, "HIVE-001");
    prefs.end();

    String deviceName = "HiveSense-" + hiveId;

    NimBLEDevice::init(deviceName.c_str());
    NimBLEDevice::setMTU(512);

    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    NimBLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    // Sensor Log — bulk transfer of stored readings via notify
    charSensorLog = service->createCharacteristic(
        BLE_CHAR_SENSOR_LOG,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );
    charSensorLog->setCallbacks(new SensorLogCallback());

    // Reading Count — phone reads this to know how many readings to expect
    charReadingCount = service->createCharacteristic(
        BLE_CHAR_READING_COUNT,
        NIMBLE_PROPERTY::READ
    );
    uint16_t count = Storage::getReadingCount();
    charReadingCount->setValue(reinterpret_cast<uint8_t*>(&count), sizeof(uint16_t));

    // Hive ID — readable for identification, writable for pairing
    charHiveId = service->createCharacteristic(
        BLE_CHAR_HIVE_ID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );
    charHiveId->setCallbacks(new HiveIdCallback());
    charHiveId->setValue(hiveId.c_str());

    // Clear Log — phone writes 0x01 after confirming complete download
    charClearLog = service->createCharacteristic(
        BLE_CHAR_CLEAR_LOG,
        NIMBLE_PROPERTY::WRITE
    );
    charClearLog->setCallbacks(new ClearLogCallback());

    service->start();

    Serial.printf("[BLE] GATT server initialized — %s (%u readings available)\n",
                  deviceName.c_str(), count);
    return true;
}

bool advertiseAndWait(uint16_t timeoutMs) {
    deviceConnected      = false;
    syncComplete         = false;
    sensorLogSubscribed  = false;

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->start();

    Serial.printf("[BLE] Advertising for %u ms\n", timeoutMs);

    // Wait for connection
    uint32_t deadline = millis() + timeoutMs;
    while (!deviceConnected && millis() < deadline) {
        delay(100);
    }

    if (!deviceConnected) {
        advertising->stop();
        Serial.println("[BLE] No connection — advertising stopped");
        return false;
    }

    // Wait for phone to subscribe to sensor log notifications
    Serial.println("[BLE] Connected — waiting for sensor log subscription...");
    deadline = millis() + 10000;  // 10 second timeout for subscription
    while (!sensorLogSubscribed && deviceConnected && millis() < deadline) {
        delay(50);
    }

    if (!sensorLogSubscribed) {
        Serial.println("[BLE] Phone did not subscribe — skipping data send");
        return true;  // Still connected, just no data transfer
    }

    sendStoredReadings();
    return true;
}

void waitForSyncComplete() {
    Serial.println("[BLE] Waiting for sync to complete...");

    while (!syncComplete) {
        delay(100);
    }
}

void shutdown() {
    NimBLEDevice::deinit(true);  // true = release memory
    bleServer        = nullptr;
    charSensorLog    = nullptr;
    charReadingCount = nullptr;
    charHiveId       = nullptr;
    charClearLog     = nullptr;

    Serial.println("[BLE] Shutdown — stack deinitialized");
}

}  // namespace CommsBle
