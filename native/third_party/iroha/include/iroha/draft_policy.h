#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace iroha {
struct DraftMeasure {
    int blocks = 0, proposed = 0, confirmed = 0, partial = 0, scalarTokens = 0;
    double blockUs = 0, scalarUs = 0;
};

// A router retains only timing and hit rates. Models can be freed between requests.
class DraftPolicy {
    struct Bucket {
        double benefit = 0, hitRate = 0;
        int observations = 0, misses = 0, skip = 0, backoff = 2;
    };
    std::array<Bucket, 4> buckets_{};
public:
    static size_t BucketFor(size_t length, bool dictionaryAgrees) {
        return (length >= 24 ? 2u : 0u) + unsigned(dictionaryAgrees);
    }
    int Width(size_t index, bool pressure) {
        if (pressure) return 0;
        auto& b = buckets_[index];
        if (b.skip) { --b.skip; return 0; }
        return b.observations >= 2 && b.benefit > .10 && b.hitRate > .80 ? 4 : 2;
    }
    void Observe(size_t index, const DraftMeasure& m) {
        if (!m.scalarTokens || m.scalarUs <= 0) return;
        auto& b = buckets_[index];
        const double hit = m.proposed ? double(m.confirmed) / m.proposed : 0;
        const double serial = m.scalarUs / m.scalarTokens * m.confirmed;
        const double benefit = serial > 0 ? std::clamp(1.0 - m.blockUs/serial, -1.0, 1.0) : -1;
        const double weight = b.observations ? .5 : 1;
        b.hitRate += weight * (hit - b.hitRate);
        b.benefit += weight * (benefit - b.benefit);
        b.observations = std::min(b.observations + 1, 16);
        if (benefit > .03 && hit >= .65) { b.misses = 0; b.backoff = 2; }
        else if (++b.misses >= 2) {
            b.skip = b.backoff;
            b.backoff = std::min(8, 2*b.backoff);
            b.misses = 0;
        }
    }
};
}
