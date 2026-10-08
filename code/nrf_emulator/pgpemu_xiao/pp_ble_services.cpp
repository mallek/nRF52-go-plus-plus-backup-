#include <Arduino.h>
#include <string.h>

#include "pp_ble_services.h"

void ppSetupReadable(BLECharacteristic& chr,
                     const uint8_t* value,
                     uint16_t len)
{
  chr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr.setFixedLen(len);
  chr.write(value, len);
  chr.begin();
}

void ppSetupReadableWithTrace(BLECharacteristic& chr,
                              const uint8_t* value,
                              uint16_t len,
                              const BleSetupCallbacks& callbacks)
{
  ppSetupReadable(chr, value, len);
  chr.setReadAuthorizeCallback(callbacks.readAuthorizeCallback);
}

void ppSetupReadableVariableWithTrace(BLECharacteristic& chr,
                                      const uint8_t* value,
                                      uint16_t initialLen,
                                      uint16_t maxLen,
                                      const BleSetupCallbacks& callbacks)
{
  chr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr.setMaxLen(maxLen);
  chr.write(value, initialLen);
  chr.begin();
  chr.setReadAuthorizeCallback(callbacks.readAuthorizeCallback);
}

void ppSetupWritable(BLECharacteristic& chr,
                     uint8_t props,
                     uint16_t maxLen,
                     const BleSetupCallbacks& callbacks)
{
  chr.setProperties(props);
  chr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr.setMaxLen(maxLen);
  chr.setWriteCallback(callbacks.serviceWriteCallback);
  chr.begin();
}

void ppSetupNotifyReadable(BLECharacteristic& chr,
                           uint8_t props,
                           const uint8_t* value,
                           uint16_t len,
                           const BleSetupCallbacks& callbacks)
{
  chr.setProperties(props);
  chr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr.setFixedLen(len);
  chr.write(value, len);
  chr.begin();
  chr.setCccdWriteCallback(callbacks.cccdWriteCallback);
  chr.setReadAuthorizeCallback(callbacks.readAuthorizeCallback);
}

void ppSetupNotifyVariable(BLECharacteristic& chr,
                           uint8_t props,
                           const uint8_t* value,
                           uint16_t initialLen,
                           uint16_t maxLen,
                           const BleSetupCallbacks& callbacks)
{
  chr.setProperties(props);
  chr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  chr.setMaxLen(maxLen);
  chr.write(value, initialLen);
  chr.begin();
  chr.setCccdWriteCallback(callbacks.cccdWriteCallback);
  chr.setReadAuthorizeCallback(callbacks.readAuthorizeCallback);
}

void ppConfigureIdentity(const BleIdentityConfig& config)
{
  ble_gap_addr_t targetAddr = {};
  targetAddr.addr_id_peer = 0;
  targetAddr.addr_type = BLE_GAP_ADDR_TYPE_PUBLIC;
  memcpy(targetAddr.addr, config.realAddrLe, config.realAddrLen);

  Bluefruit.setAddr(&targetAddr);

  ble_gap_conn_sec_mode_t nameWritePerm = BLE_SECMODE_NO_ACCESS;
  sd_ble_gap_device_name_set(&nameWritePerm,
                             reinterpret_cast<uint8_t const*>(config.deviceName),
                             strlen(config.deviceName));
  Bluefruit.setAppearance(config.appearance);
}

void ppConfigureGapPreferences(const BleGapPreferenceConfig& config)
{
  Bluefruit.Periph.setConnInterval(config.connIntervalMinUnits,
                                   config.connIntervalMaxUnits);
  Bluefruit.Periph.setConnSupervisionTimeout(config.connTimeoutUnits);
}

// Experiment 10-01: advertise as LE Limited Discoverable (flags 0x05, "pairing mode").
// The spec caps limited-discoverable advertising at 180 s, so restart it when it times out.
#ifndef PP_ADV_LIMITED
#define PP_ADV_LIMITED 0
#endif
#if PP_ADV_LIMITED
static void ppLimitedAdvStopped()
{
  Serial.println("Limited-discoverable advertising timed out -> restarting");
  Bluefruit.Advertising.start(180);
}
#endif

void ppStartAdvertising(const BleAdvertisingConfig& config)
{
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();

  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
#if PP_ADV_LIMITED
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE);
#else
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
#endif
  Bluefruit.Advertising.addManufacturerData(config.manufacturerData, config.manufacturerDataLen);
  Bluefruit.Advertising.addData(0x20, config.serviceData32, config.serviceData32Len);
  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(config.advIntervalUnits, config.advIntervalUnits);
#if PP_ADV_LIMITED
  // Bluefruit starts in its "fast" phase first; unlimited (0) is rejected for limited discoverable.
  Bluefruit.Advertising.setFastTimeout(180);
#else
  Bluefruit.Advertising.setFastTimeout(0);
#endif
#if PP_ADV_LIMITED
  Bluefruit.Advertising.setStopCallback(ppLimitedAdvStopped);
  bool ok = Bluefruit.Advertising.start(180);
  Serial.print("Advertising start (limited discoverable, 180 s) ok=");
  Serial.println(ok ? "true" : "false");
#else
  Bluefruit.Advertising.start(0);
#endif
}
