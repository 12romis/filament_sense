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

}  // namespace storage
