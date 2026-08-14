// Wrap-safe comparisons for monotonically increasing 32-bit network counters.
//
// The comparison is unambiguous only while values being compared are less than
// half the sequence space apart. Network windows in this project are far
// smaller than 2^31, so that invariant is explicit and easy to preserve.

#pragma once

#include <cstdint>

namespace mega {
namespace net {

inline bool SequenceMoreRecent(uint32_t value, uint32_t reference) {
    return value != reference &&
           static_cast<uint32_t>(value - reference) < 0x80000000u;
}

inline bool SequenceAtOrBefore(uint32_t value, uint32_t acknowledged) {
    return value == acknowledged || SequenceMoreRecent(acknowledged, value);
}

}  // namespace net
}  // namespace mega
