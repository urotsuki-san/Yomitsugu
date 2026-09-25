#include "azookey_bridge.h"
#include "ime_engine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace ime;
using Clock = std::chrono::steady_clock;

static double ms(Clock::duration d) {
  return std::chrono::duration<double, std::milli>(d).count();
}

int main() {
  if (!AzookeyEnsureReady()) {
    std::printf("azookey not ready\n");
    return 1;
  }
  std::printf("zenzai: %s\n", AzookeyZenzaiStatus().c_str());

  const std::vector<std::string> samples = {
      u8"きょうはいいてんきですね。",
      u8"ありがとう",
      u8"がっこう",
      u8"にほんご",
      u8"わたしのなまえはたなかです",
  };

  // cold: unique keys, first convert may run Zenzai inference
  {
    std::vector<double> cold;
    for (size_t i = 0; i < samples.size(); ++i) {
      std::string key = samples[i] + "\x01" + std::to_string(i);
      (void)key;
      auto t0 = Clock::now();
      auto out = AzookeyConvert(samples[i], 4);
      auto t1 = Clock::now();
      cold.push_back(ms(t1 - t0));
      std::printf("cold[%zu] %.2f ms n=%zu top=%s\n", i, cold.back(), out.size(),
                  out.empty() ? "-" : out[0].c_str());
    }
  }

  // warm: same keys hit cache + zenzai cache
  {
    std::vector<double> warm;
    for (int r = 0; r < 20; ++r) {
      for (const auto& s : samples) {
        auto t0 = Clock::now();
        (void)AzookeyConvert(s, 4);
        auto t1 = Clock::now();
        warm.push_back(ms(t1 - t0));
      }
    }
    std::sort(warm.begin(), warm.end());
    auto pct = [&](double p) {
      size_t idx = static_cast<size_t>(p * (warm.size() - 1));
      return warm[idx];
    };
    std::printf("warm n=%zu p50=%.3f p95=%.3f p99=%.3f max=%.3f ms\n", warm.size(),
                pct(0.50), pct(0.95), pct(0.99), warm.back());
  }

  // Decode-level with mixed JA (includes azo alts)
  {
    DecodeInput in;
    in.raw_text = "kyouhaiitennkidesune.";
    in.phase = "sentence_end";
    in.punctuation_completion = true;
    in.candidate_limit = 8;
    std::vector<double> dec;
    for (int i = 0; i < 50; ++i) {
      auto t0 = Clock::now();
      auto c = Decode(in);
      auto t1 = Clock::now();
      if (i >= 10) dec.push_back(ms(t1 - t0));
      if (i == 0 && !c.empty())
        std::printf("decode top=%s\n", c[0].output_text.c_str());
    }
    std::sort(dec.begin(), dec.end());
    std::printf("decode warm p50=%.3f p95=%.3f p99=%.3f ms\n", dec[dec.size()/2],
                dec[static_cast<size_t>(0.95*(dec.size()-1))],
                dec[static_cast<size_t>(0.99*(dec.size()-1))]);
  }
  std::printf("zenzai: %s\n", AzookeyZenzaiStatus().c_str());
  return 0;
}
