#include "ime_engine.h"
#include <cstdio>
#include <string>
using namespace ime;

static void Dump(const char* id, const std::string& raw, const char* phase = "end_of_phrase",
                 bool punct = false, int k = 8) {
  DecodeInput in;
  in.raw_text = raw;
  in.phase = phase;
  in.punctuation_completion = punct;
  in.field = "prose";
  in.candidate_limit = k;
  auto c = Decode(in);
  std::printf("=== %s [%s] ===\n", id, raw.c_str());
  for (size_t i = 0; i < c.size(); ++i)
    std::printf("  [%zu] tot=%.3f jp=%.2f en=%.2f edit=%.2f cov=%.2f raw=%d | %s\n", i,
                c[i].total, c[i].jp_score, c[i].en_score, c[i].edit_cost, c[i].coverage,
                c[i].is_raw ? 1 : 0, c[i].output_text.c_str());
  bool ok = false;
  std::string kana = ConvertRomaji(raw, &ok);
  std::printf("  romaji_ok=%d kana=%s\n", ok ? 1 : 0, kana.c_str());
  if (AzookeyAvailable()) {
    auto azo = AzookeyConvert(kana.empty() ? raw : kana, 6);
    std::printf("  azo(n=%zu):\n", azo.size());
    for (size_t i = 0; i < azo.size(); ++i) std::printf("    %s\n", azo[i].c_str());
  }
}

int main() {
  AzookeyEnsureReady();
  Dump("L18", "honntowaniomosirokatta");
  Dump("T31", "tokyo");
  Dump("L03", "asanomadeisogasiidesita");
  Dump("T26", "daijoubudesu");
  Dump("L14", "sukodesu");
  Dump("L20", "korehokanjidesu");
  Dump("T07", "arigatougozaimas");
  Dump("L08", "mainitenomasiyouwokurikaesitekudasai");
  Dump("T21", "tyuo");
  Dump("T22", "chuo");
  Dump("T24", "sindai");
  Dump("T30", "sensei");
  return 0;
}
