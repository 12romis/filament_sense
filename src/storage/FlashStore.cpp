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

  const size_t written = preferences_.putFloat(kBaselineKey, baselineWeight);
  return written == sizeof(float);
}

bool FlashStore::loadBaselineWeight(float& baselineWeight) {
  if (!initialized_) {
    return false;
  }

  if (!preferences_.isKey(kBaselineKey)) {
    return false;
  }

  baselineWeight = preferences_.getFloat(kBaselineKey, 0.0F);
  return true;
}

}  // namespace storage
