#include "ime_engine.h"
#include <cstdio>
#include <string>

using namespace ime;

static void Dump(const char* name, const std::string& raw, const char* phase, bool punct) {
  DecodeInput in;
  in.raw_text = raw;
  in.phase = phase;
  in.punctuation_completion = punct;
  in.field = "prose";
  in.candidate_limit = 16;
  auto cands = Decode(in);
  std::printf("=== %s (n=%zu) ===\n", name, cands.size());
  for (size_t i = 0; i < cands.size(); ++i) {
    const auto& c = cands[i];
    std::printf("  %zu. total=%.4f en=%.3f jp=%.3f cov=%.3f edit=%.3f comp=%.3f raw=%d :: %s\n",
                i + 1, c.total, c.en_score, c.jp_score, c.coverage, c.edit_cost,
                c.completion_bonus, c.is_raw ? 1 : 0, c.output_text.c_str());
  }
  bool ok = false;
  std::string kana = ConvertRomaji(raw, &ok);
  std::printf("  romaji_ok=%d kana=%s\n", ok ? 1 : 0, kana.c_str());
}

int main() {
  Dump("U01", "Githubnoripojitoriwokousinnsitekudasai", "sentence_end", true);
  Dump("U01-ja", "noripojitoriwokousinnsitekudasai", "end_of_phrase", false);
  Dump("U02", "konnitiwa", "sentence_end", true);
  Dump("M03", "APIwokakuninshitekudasai.", "end_of_phrase", false);
  Dump("M03-ja", "wokakuninshitekudasai.", "end_of_phrase", false);
  Dump("M01", "READMEwoyondekudasai.", "end_of_phrase", false);
  Dump("USER-cold", "samukunaltutekimasitane", "end_of_phrase", false);
  Dump("USER-strong", "saikilyou", "end_of_phrase", false);
  Dump("USER-arrow", "yajirushi", "end_of_phrase", false);
  Dump("USER-period", ".", "end_of_phrase", false);
  Dump("USER-sentence-period", "samukunaltutekimasitane.", "end_of_phrase", false);
  Dump("USER-readme", "ri-domi-", "end_of_phrase", false);
  Dump("USER-long", "ri-domiewokousinnsitekudasaiGithubde", "end_of_phrase", false);
  Dump("USER-long-alt", "ri-domi-wokousinnsitekudasaiGithubde", "end_of_phrase", false);
  Dump("USER-long-ascii", "READMEwokousinnsitekudasaiGithubde", "end_of_phrase", false);
  Dump("USER-long-suffix", "wokousinnsitekudasaiGithubde", "end_of_phrase", false);
  Dump("USER-sankai", "sannkai", "end_of_phrase", false);
  Dump("USER-birds", "toritatigasannkaisuru", "end_of_phrase", false);
  auto sankai = AzookeyConvert(u8"さんかい", 32);
  std::printf("=== AZOO-SANKAI (n=%zu) ===\n", sankai.size());
  for (size_t i = 0; i < sankai.size(); ++i) std::printf("  %zu. %s\n", i + 1, sankai[i].c_str());
  return 0;
}
