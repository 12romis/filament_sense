#include "storage/FlashStore.h"

namespace storage {

bool FlashStore::begin() {
  initialized_ = preferences_.begin(kNamespace, false);
  return initialized_;
}

bool FlashStore::saveBaselineWeight(float baselineWeight) {
  if (!initialized_) {
    return false;
  }

  const size_t written = preferences_.putFloat(kBaselineWeightKey, baselineWeight);
  return written == sizeof(float);
}

bool FlashStore::loadBaselineWeight(float& baselineWeight) {
  if (!initialized_) {
    return false;
  }

  if (!preferences_.isKey(kBaselineWeightKey)) {
    return false;
  }

  baselineWeight = preferences_.getFloat(kBaselineWeightKey, 0.0F);
  return true;
}

bool FlashStore::saveBaselineTimestamp(int64_t baselineTimestamp) {
  if (!initialized_) {
    return false;
  }

  const size_t written = preferences_.putLong64(kBaselineTimeKey, baselineTimestamp);
  return written == sizeof(int64_t);
}

bool FlashStore::loadBaselineTimestamp(int64_t& baselineTimestamp) {
  if (!initialized_) {
    return false;
  }

  if (!preferences_.isKey(kBaselineTimeKey)) {
    return false;
  }

  baselineTimestamp = preferences_.getLong64(kBaselineTimeKey, 0);
  return true;
}

bool FlashStore::loadThresholdAlertSent(bool& thresholdAlertSent, const char* key) {
  if (!initialized_) {
    return false;
  }

  thresholdAlertSent = preferences_.getBool(key, false);
  return true;
}

bool FlashStore::saveThresholdAlertSent(bool thresholdAlertSent, const char* key) {
  if (!initialized_) {
    return false;
  }

  const size_t written = preferences_.putBool(key, thresholdAlertSent);
  return written == sizeof(bool);
}

bool FlashStore::savekHx711RawUnitsPerGram(float hx711RawUnitsPerGram) {
  if (!initialized_) {
    return false;
  }

  const size_t written = preferences_.putFloat(kHx711RawUnitsPerGramKey, hx711RawUnitsPerGram);
  return written == sizeof(float);
}

bool FlashStore::loadkHx711RawUnitsPerGram(float& kHx711RawUnitsPerGram) {
  if (!initialized_) {
    return false;
  }

  if (!preferences_.isKey(kHx711RawUnitsPerGramKey)) {
    return false;
  }

  kHx711RawUnitsPerGram = preferences_.getFloat(kHx711RawUnitsPerGramKey, 0.0F);
  return true;
}

bool FlashStore::saveHx711TareOffset(long hx711TareOffset) {
  if (!initialized_) {
    return false;
  }

  const size_t written = preferences_.putLong(kHx711TareOffsetKey, hx711TareOffset);
  return written == sizeof(long);
}

bool FlashStore::loadHx711TareOffset(long& hx711TareOffset) {
  if (!initialized_) {
    return false;
  }

  if (!preferences_.isKey(kHx711TareOffsetKey)) {
    return false;
  }

  hx711TareOffset = preferences_.getLong(kHx711TareOffsetKey, 0L);
  return true;
}

}  // namespace storage
