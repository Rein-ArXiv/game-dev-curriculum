#include "measure/query_ring.h"

namespace vklab {

int QueryRing::Begin() {
    if (inFlight_ >= kRingSize) {
        recording_ = false;
        return -1;
    }
    recording_ = true;
    return writeIdx_;
}

int QueryRing::End() {
    if (!recording_) return -1;
    const int slot = writeIdx_;
    ++inFlight_;
    writeIdx_ = (writeIdx_ + 1) % kRingSize;
    recording_ = false;
    return slot;
}

int QueryRing::ReadIndex() const {
    if (inFlight_ == 0) return -1;
    return (writeIdx_ - inFlight_ + kRingSize) % kRingSize;
}

void QueryRing::Retire() {
    if (inFlight_ > 0) --inFlight_;
}

}  // namespace vklab
