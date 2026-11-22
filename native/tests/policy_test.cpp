#include "iroha/draft_policy.h"
#include <cstdio>

int main() {
    iroha::DraftPolicy policy;
    const size_t shortInput = policy.BucketFor(8,true);
    const size_t longInput = policy.BucketFor(32,true);
    iroha::DraftMeasure good{2,4,4,0,2,15,20};
    iroha::DraftMeasure bad{2,4,2,2,2,50,20};
    if (policy.Width(shortInput,true) || policy.Width(shortInput,false)!=2) return 1;
    policy.Observe(shortInput,good);
    policy.Observe(shortInput,good);
    if (policy.Width(shortInput,false)!=4 || policy.Width(longInput,false)!=2) return 2;
    policy.Observe(shortInput,bad);
    policy.Observe(shortInput,bad);
    if (policy.Width(shortInput,false) || policy.Width(shortInput,false) ||
        policy.Width(shortInput,false)!=2) return 3;
    policy.Observe(shortInput,good);
    policy.Observe(shortInput,good);
    if (policy.Width(shortInput,false)!=4) return 4;
    std::puts("PASS pressure bypass, wider blocks, independent buckets, cooldown, reprobe and recovery");
}
