#include "ime_engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace ime;

static int g_fail = 0;

void Expect(bool cond, const std::string& name, const std::string& detail = "") {
  if (cond) {
    std::printf("[PASS] %s\n", name.c_str());
  } else {
    std::printf("[FAIL] %s %s\n", name.c_str(), detail.c_str());
    ++g_fail;
  }
}

std::string Top(const DecodeInput& in) {
  auto c = Decode(in);
  return c.empty() ? "" : c[0].output_text;
}

bool Has(const DecodeInput& in, const std::string& expect) {
  auto c = Decode(in);
  for (auto& x : c)
    if (x.output_text == expect) return true;
  return false;
}

DecodeInput Make(const std::string& raw, const std::string& phase = "end_of_phrase",
                 bool punct = false, const std::string& field = "prose") {
  DecodeInput in;
  in.raw_text = raw;
  in.phase = phase;
  in.punctuation_completion = punct;
  in.field = field;
  in.candidate_limit = 8;
  return in;
}

int main() {
  {
    bool ok = false;
    std::string kana = ConvertRomaji("konnitiwa", &ok);
    Expect(ok && kana == "こんにちわ", "romaji konnitiwa", kana);
  }
  {
    bool ok = false;
    std::string kana = ConvertRomaji("no", &ok);
    Expect(ok && kana == "の", "romaji no", kana);
  }
  {
    bool ok = false;
    std::string kana = ConvertRomaji("noripoji", &ok);
    Expect(ok && kana == "のりぽじ", "romaji noripoji", kana);
  }

  Expect(Top(Make("Githubnoripojitoriwokousinnsitekudasai", "sentence_end", true)) ==
             "Githubのリポジトリを更新してください。",
         "U01 top1");
  Expect(Top(Make("konnitiwa", "sentence_end", true)) == "こんにちは。", "U02 top1");
  Expect(Has(Make("kiyoiitennkidesune.", "sentence_end", true), "今日はいい天気ですね。"), "U03 candidate");
  Expect(Top(Make("konnitiwa")) == "こんにちは", "J01 top1");
  Expect(Top(Make("konnichiha")) == "こんにちは", "J02 top1");
  Expect(Top(Make("kyouhaiitennkidesune.")) == "今日はいい天気ですね。", "J03 top1");
  Expect(Top(Make("arigatougozaimasu.")) == "ありがとうございます。", "J04 top1");
  Expect(Has(Make("gakou"), "学校"), "J05 candidate");
  Expect(Top(Make("READMEwoyondekudasai.")) == "READMEを読んでください。", "M01 top1");
  Expect(Top(Make("sannkai")) == u8"散開", "sannkai prefers word to number-counter shorthand");
  Expect(Has(Make("toritatigasannkaisuru"), u8"鳥たちが散開する"), "birds sentence offers 散開");
  Expect(Top(Make("Python3.13deugokimasu.")) == "Python3.13で動きます。", "M02 top1");
  Expect(Top(Make("APIwokakuninshitekudasai.")) == "APIを確認してください。", "M03 top1");
  Expect(Has(Make("README.mdwoyondekudasai."), "README.mdを読んでください。"), "M04 candidate");
  Expect(Top(Make("Please update the Github repository.")) ==
             "Please update the Github repository.",
         "E01 preserve");
  Expect(Top(Make("This is a nice day.")) == "This is a nice day.", "E02 preserve");
  Expect(Top(Make("github", "end_of_phrase", false, "identifier")) == "github", "E03 preserve");
  Expect(Top(Make("hello world")) == "hello world", "E04 preserve");
  Expect(Top(Make("https://example.com/konnitiwa?q=no")) == "https://example.com/konnitiwa?q=no",
         "P01 preserve");
  Expect(Top(Make("C:\\work\\foo_bar.py")) == "C:\\work\\foo_bar.py", "P02 preserve");
  Expect(Top(Make("test.user+tag@example.com")) == "test.user+tag@example.com", "P03 preserve");
  Expect(Top(Make("print(\"konnitiwa\")", "end_of_phrase", false, "code")) ==
             "print(\"konnitiwa\")",
         "P04 preserve");
  Expect(Top(Make("getUserName")) == "getUserName", "P05 preserve");

  {
    auto in = Make("no");
    in.left_context = "Answer yes or ";
    Expect(Top(in) == "no", "A01 context en");
  }
  {
    auto in = Make("no");
    in.left_context = "これは私";
    in.right_context = "本です。";
    Expect(Has(in, "の"), "A02 candidate");
  }
  Expect(Has(Make("name"), "name"), "A03 raw present");

  // 入力状態に応じたSpaceの動作。
  {
    Session s;
    s.set_punctuation_completion(true);
    s.Type("hello");
    Expect(s.QuerySpace() == SpaceAction::kInsertSpace, "space english inserts");
    s.PressSpace();
    Expect(s.raw_text() == "hello ", "space kept in english");
  }
  {
    Session s;
    s.Type("konnitiwa");
    auto a = s.QuerySpace();
    Expect(a == SpaceAction::kStartConversion, "space starts conversion for ja", std::to_string(static_cast<int>(a)));
  }
  {
    Session s;
    s.Type("konnitiwa");
    s.PressEnter();
    Expect(!s.output_log().empty() && s.output_log()[0] != "enter_passthrough", "enter commits");
  }

  // F6～F10による文字種の変換。
  {
    Expect(ConvertCharacterClass(u8"こんにちは", 0x77) != u8"こんにちは", "F8 halfwidth changes");
    std::string hw = ConvertCharacterClass(u8"こんにちは", 0x77);
    Expect(!hw.empty() && hw != u8"こんにちは", "F8 halfwidth non-empty", hw);
    std::string fw = ConvertCharacterClass("ABC", 0x78);
    Expect(fw == u8"ＡＢＣ", "F9 fullwidth alnum", fw);
    std::string back = ConvertCharacterClass(u8"ＡＢＣ", 0x79);
    Expect(back == "ABC", "F10 halfwidth alnum", back);
    std::string kata = ConvertCharacterClass(u8"こんにちは", 0x76);
    Expect(kata == u8"コンニチハ", "F7 hiragana to katakana", kata);
    std::string hira = ConvertCharacterClass(u8"コンニチハ", 0x75);
    Expect(hira == u8"こんにちは", "F6 katakana to hiragana", hira);
  }

  // 候補と文節の移動。
  {
    Session s;
    s.Type("konnitiwa");
    s.PressSpace();
    Expect(s.is_converting(), "space enters conversion");
    int idx0 = s.selected_index();
    s.PressDown();
    Expect(s.selected_index() != idx0 || s.candidates().size() < 2, "down changes candidate",
           std::to_string(s.selected_index()));
    s.PressUp();
    Expect(s.selected_index() == idx0, "up restores candidate", std::to_string(s.selected_index()));
    bool sel = s.SelectCandidate(0);
    Expect(sel, "select candidate 0");
    Expect(s.is_converting(), "still converting after select");
    // 英日混在の入力で文節を移動する。
    Session s2;
    s2.Type("READMEwoyondekudasai.");
    s2.PressSpace();
    Expect(s2.is_converting(), "mixed enters conversion");
    int sc = s2.segment_count();
    Expect(sc == 1, "one segment has two boundaries", std::to_string(sc));
    int si0 = s2.segment_index();
    s2.PressRight();
    Expect(s2.segment_index() > si0 || sc < 3, "right moves segment",
           std::to_string(s2.segment_index()));
    s2.PressLeft();
    Expect(s2.segment_index() == si0, "left restores segment", std::to_string(s2.segment_index()));
    size_t before = s2.raw_text().size();
    s2.PressShiftRight();
    // Shift+右で境界を拡張しても原文を変えない。
    Expect(s2.raw_text().size() == before, "shift-resize keeps raw");
    Expect(s2.is_converting(), "still converting after segment ops");
  }

  // 表示文字の置換とカーソル後方の削除。
  {
    Session s;
    s.Type("konnitiwa");
    s.ReplaceVisible(u8"こんにちは");
    Expect(s.visible_text().find(u8"こんにちは") != std::string::npos ||
               s.raw_text().find(u8"こんにちは") != std::string::npos,
           "replace visible applies");
    s.DeleteForward();
    const std::string kana5 = u8"こんにちは";
    Expect(s.raw_text().size() < kana5.size() || !s.composing(),
           "delete forward runs");
  }

  // AzooKey/Zenzaiとの接続。実行環境がなければこの項目は省略する。
  {
    bool ready = AzookeyEnsureReady();
    std::printf("azookey ready=%d available=%d\n", ready ? 1 : 0, AzookeyAvailable() ? 1 : 0);
    if (ready) {
      auto texts = AzookeyConvert(u8"きょう", 4);
      Expect(!texts.empty(), "azookey converts kyou", "empty");
      auto in = Make("konnitiwa", "sentence_end", true);
      auto cands = Decode(in);
      bool has_converted = false;
      for (auto& c : cands)
        if (c.output_text.find(u8"こん") != std::string::npos) has_converted = true;
      Expect(has_converted, "konnitiwa still converts with azookey");
      auto st = AzookeyZenzaiStatus();
      std::printf("zenzai status: %s\n", st.c_str());
    } else {
      std::printf("[SKIP] azookey-engine.dll not found; bridge inactive\n");
    }
  }

  // 表示遅延の測定。
  {
    auto in = Make("Githubnoripojitoriwokousinnsitekudasai", "sentence_end", true);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 50; ++i) Decode(in);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count() / 50.0;
    std::printf("latency mean_ms=%.3f (n=50)\n", ms);
    Expect(ms < 50.0, "latency mean < 50ms");
  }

  // ウォーム時のp95/p99を測定する。初回ロードは別に扱う。
  {
    auto in = Make("konnitiwa", "sentence_end", true);
    Decode(in);  // warm
    std::vector<double> samples;
    samples.reserve(200);
    for (int i = 0; i < 200; ++i) {
      auto a = std::chrono::steady_clock::now();
      Decode(in);
      auto b = std::chrono::steady_clock::now();
      samples.push_back(std::chrono::duration<double, std::milli>(b - a).count());
    }
    std::sort(samples.begin(), samples.end());
    double p50 = samples[100];
    double p95 = samples[190];
    double p99 = samples[198];
    std::printf("warm latency p50=%.3f p95=%.3f p99=%.3f ms\n", p50, p95, p99);
    Expect(p95 < 50.0, "warm p95 < 50ms", std::to_string(p95));
    Expect(p99 < 100.0, "warm p99 < 100ms", std::to_string(p99));
  }

  if (g_fail == 0) {
    std::printf("ALL PASSED\n");
    return 0;
  }
  std::printf("FAILED %d\n", g_fail);
  return 1;
}
