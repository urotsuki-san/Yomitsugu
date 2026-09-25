#include "ime_engine.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <windows.h>
#include <chrono>
#include "user_dictionary.h"

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

static bool ReadmeDemo() {
  struct DemoCase { int id; const char* raw; const char* expected; };
  const DemoCase cases[] = {
      {1, "kyouhaiitennkidesune.", u8"今日はいい天気ですね。"},
      {2, "APIwokakuninshitekudasai.", u8"APIを確認してください。"},
  };
  if (!AzookeyEnsureReady()) {
    std::puts("DEMO FAIL: AzooKey runtime missing");
    return false;
  }
  bool all_ok = true;
  for (const auto& c : cases) {
    std::string final;
    const std::string raw = c.raw;
    for (size_t count = 1; count <= raw.size(); ++count) {
      DecodeInput in;
      in.raw_text = raw.substr(0, count);
      in.phase = "end_of_phrase";
      in.field = "prose";
      in.candidate_limit = 1;
      auto candidates = Decode(in);
      const std::string top = candidates.empty() ? "" : candidates[0].output_text;
      std::printf("DEMO_FRAME\t%d\ttyping\t%s\t%s\n", c.id, in.raw_text.c_str(), top.c_str());
      if (count == raw.size()) final = top;
    }
    const bool passed = final == c.expected;
    std::printf("DEMO %s %d\n", passed ? "PASS" : "FAIL", c.id);
    all_ok = all_ok && passed;
  }
  return all_ok;
}

int main(int argc, char** argv) {
  if (argc == 2 && std::strcmp(argv[1], "--runtime-json") == 0) {
    if (!AzookeyEnsureReady()) return 2;
    auto module = GetModuleHandleW(L"azookey-engine.dll");
    auto convert = reinterpret_cast<const char* (*)(const char*, int)>(GetProcAddress(module, "ConvertText"));
    auto freeString = reinterpret_cast<void (*)(const char*)>(GetProcAddress(module, "FreeString"));
    if (!convert || !freeString) return 3;
    std::string raw;
    while (std::getline(std::cin, raw)) {
      if (!raw.empty() && raw.back() == '\r') raw.pop_back();
      bool ok = false;
      const auto reading = ConvertRomaji(raw, &ok);
      const char* json = convert(reading.c_str(), 0);
      std::printf("RUNTIME\t%s\t%s\n", raw.c_str(), json ? json : "null");
      if (json) freeString(json);
    }
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--readme-demo") == 0)
    return ReadmeDemo() ? 0 : 1;
  if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) {
    if (!AzookeyEnsureReady()) return 2;
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    UserDictionary dictionary;
    std::string error;
    if (!dictionary.Load(std::filesystem::path(path).parent_path()/L"public_dictionary.tsv", &error, true)) return 3;
    std::string raw;
    while (std::getline(std::cin, raw)) {
      if (!raw.empty() && raw.back() == '\r') raw.pop_back();
      if (raw.empty()) continue;
      DecodeInput in;
      in.raw_text = raw; in.field = "prose"; in.phase = "end_of_phrase";
      in.candidate_limit = 16;
      const auto start = std::chrono::steady_clock::now();
      auto candidates = Decode(in);
      dictionary.Apply(in, &candidates, true);
      const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
      bool ok = false;
      const auto reading = ConvertRomaji(raw, &ok);
      std::printf("PROBE\t%s\t%s\t%d\t%.3f", raw.c_str(), reading.c_str(), ok ? 1 : 0, ms);
      for (const auto& candidate : candidates) std::printf("\t%s", candidate.output_text.c_str());
      std::printf("\n");
    }
    return 0;
  }
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) Dump(argv[i], argv[i], "end_of_phrase", false);
    return 0;
  }
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
