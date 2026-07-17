#include "RecentSightings.h"

#include <Arduino.h>

#include <cstring>

RecentSightings recentSightings;

void RecentSightings::push(const Entry& entry) {
  entries[head] = entry;
  head = (head + 1) % kCapacity;
  if (entryCount < kCapacity) entryCount++;
}

void RecentSightings::recordWifi(const WifiObservation& obs, LogRecordType type) {
  Entry entry;
  entry.seenAtMs = millis();
  entry.type = type;
  entry.mac = (type == LogRecordType::WifiClient) ? obs.transmitter : obs.bssid;
  entry.rssi = obs.rssi;
  const uint8_t len = obs.ssidLen > sizeof(entry.label) - 1 ? sizeof(entry.label) - 1 : obs.ssidLen;
  memcpy(entry.label, obs.ssid, len);
  entry.label[len] = '\0';
  push(entry);
}

void RecentSightings::recordBle(const BleObservation& obs) {
  Entry entry;
  entry.seenAtMs = millis();
  entry.type = LogRecordType::BleDevice;
  entry.mac = obs.address;
  entry.rssi = obs.rssi;
  const uint8_t len = obs.nameLen > sizeof(entry.label) - 1 ? sizeof(entry.label) - 1 : obs.nameLen;
  memcpy(entry.label, obs.name, len);
  entry.label[len] = '\0';
  push(entry);
}

const RecentSightings::Entry& RecentSightings::at(size_t indexFromNewest) const {
  const size_t idx = (head + kCapacity - 1 - indexFromNewest) % kCapacity;
  return entries[idx];
}
