#include "ime_engine.h"
#include <cstdio>
using namespace ime;
int main() {
  auto show = [](const char* name, const std::string& raw, const char* phase = "end_of_phrase", bool punct = false) {
    DecodeInput in;
    in.raw_text = raw;
    in.phase = phase;
    in.punctuation_completion = punct;
    in.candidate_limit = 8;
    auto c = Decode(in);
    std::printf("== %s [%s] ==\n", name, raw.c_str());
    for (size_t i = 0; i < c.size() && i < 5; ++i)
      std::printf("  [%zu] total=%.3f jp=%.2f en=%.2f raw=%d | %s\n", i, c[i].total, c[i].jp_score,
                  c[i].en_score, c[i].is_raw ? 1 : 0, c[i].output_text.c_str());
  };
  show("E01", "Please update the Github repository.");
  show("E02", "This is a nice day.");
  show("space", "hello");
  show("A01", "no");
  show("U01", "Githubnoripojitoriwokousinnsitekudasai", "sentence_end", true);
  show("J01", "konnitiwa");
  std::printf("ready=%d\n", AzookeyAvailable() ? 1 : 0);
  return 0;
}
