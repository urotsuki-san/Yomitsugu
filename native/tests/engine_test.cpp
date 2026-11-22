#include "ime_engine.h"
#include "../src/cached_surfaces.h"

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
  Expect(Top(Make("Aoba.NeTwotsukau")) == u8"Aoba.NeTを使う",
         "retain original hostname spelling before a Japanese particle");
  Expect(ContainsLatinName("kumo.ioheiku") && !ContainsLatinName("asa.konbanha"),
         "distinguish a romanizable hostname from punctuation between readings");
  Expect(Top(Make("Aoba.NeTwotsukau", "end_of_phrase", false, "url")) == "Aoba.NeTwotsukau",
         "retain the entire input in a URL field");
  {
    const std::vector<std::string> rows = {u8"今日", {}, std::string("A\0B", 3),
                                           std::string(70000, 'x'), u8"今日も晴れ"};
    CachedSurfaces cached(rows);
    Expect(cached.read(0).empty() && CachedSurfaces({}).read(4).empty(), "empty dictionary cache ranges");
    for (size_t limit = 1; limit <= rows.size() + 1; ++limit) {
      const std::vector<std::string> expected(rows.begin(), rows.begin() + std::min(limit, rows.size()));
      Expect(cached.read(limit) == expected, "dictionary cache retains candidate bytes, order and limit");
    }
  }

  for (const char* value : {"C:/tools/z/test.exe", "src/tool/file.txt", "tmp/tools/cache", "git/status", "../docs/readme.md"}) {
    DecodeInput in; in.raw_text = value;
    const auto rows = Decode(in);
    Expect(!rows.empty() && rows.front().output_text == value, "forward-slash path remains literal", value);
  }
  {
    DecodeInput in; in.raw_text = "ko-hiz/ti-";
    const auto rows = Decode(in);
    Expect(!rows.empty() && rows.front().output_text.find(u8"・") != std::string::npos,
           "romaji z slash keeps the middle dot in normal prose");
  }

  {
    Candidate japan; japan.output_text=u8"日本";
    Session edited; edited.set_deferred_decoding(true); edited.Type("nihongo");
    const auto old = edited.decode_input();
    edited.Backspace();   // nihong: incomplete syllable
    Expect(!edited.needs_typing_lookup(),"incomplete deletion skips a discarded worker request");
    edited.Backspace();   // nihon: complete reading
    Expect(!edited.ApplyCandidates(old,{japan}) && edited.typing() && edited.needs_typing_lookup() &&
           edited.ApplyCandidates(edited.preview_request(),{japan}) && edited.visible_text()==u8"日本",
           "complete tail deletion converts the remaining reading without another letter");
    edited.PressHome(); edited.DeleteForward();
    Expect(!edited.typing() && !edited.needs_typing_lookup(),"middle editing keeps the editable caret");
    for (const std::string name : {"GitHub","Github","github","GITHUB","gItHuB",
                                   "Google","google","GOOGLE","YouTube","OpenAI","PostgreSQL","Spotify"}) {
      Expect(Top(Make(name+"wotsukau"))==name+u8"を使う","original spelling before Japanese: "+name);
      Session preview; preview.set_deferred_decoding(true); preview.Type(name+"wotsu");
      Expect(preview.visible_text()==name+u8"をつ","immediate mixed-case composition: "+name);
    }
    Expect(Top(Make("GitHubActions"))=="GitHubActions" &&
           Top(Make("GitHubwotsukau","end_of_phrase",false,"code"))=="GitHubwotsukau",
           "identifier and code fields retain spelling");
    Expect(Top(Make("google.com"))=="google.com" && Top(Make("google_workspace"))=="google_workspace",
           "domain and snake case stay literal in prose");
    Expect(Top(Make("Zyqvexwotsukau"))==u8"Zyqvexを使う" &&
           !ContainsLatinName("kumawomiru"), "unlisted Latin stem and Japanese homophone");
  }
  {
    const std::string good=R"({"parts":[{"text":"nested","correspondingCount":9}],"text":"top","correspondingCount":1})";
    auto rows=ParseAzookeyCandidates("["+good+R"(,{"text":"ok","text":[],"correspondingCount":3},{"text":"bad","correspondingCount":1.5},[)"+good+"]]");
    Expect(rows.size()==1 && rows[0].text=="top" && rows[0].corresponding_count==1,
           "dictionary reader keeps only valid fields on direct candidate objects");
    Expect(ParseAzookeyCandidates("["+good+",").empty(),"malformed dictionary reply discards all rows");
    std::string many="[";
    for (int i=0;i<260;++i) {if(i)many+=',';many+=good;}
    Expect(ParseAzookeyCandidates(many+"]").size()==256 && ParseAzookeyCandidates(many+",?]").empty(),
           "dictionary reader caps retained rows while validating trailing data");
  }
  {
    Candidate japan; japan.output_text=u8"日本"; japan.reading_text=u8"にほん";
    Session live; live.set_deferred_decoding(true); live.Type("nihon");
    const auto prefix=live.decode_input();
    live.Type("g");
    Expect(live.preview_request().raw_text=="nihon" && live.decode_input().raw_text=="nihong",
           "background preview uses complete kana and preserves the editable raw input");
    Expect(!live.ApplyCandidates(prefix,{japan}) && live.ApplyTypingPrefix(prefix,japan) &&
           live.visible_text()==u8"日本g" && !live.candidates_ready() && !live.needs_typing_lookup(),
           "late append-only response updates the live prefix without resolving the new revision");
    live.Type("o");
    Expect(live.needs_typing_lookup() && live.visible_text()==u8"日本ご","completed kana requests its own conversion");
    Candidate language; language.output_text=u8"日本語";
    const auto full=live.decode_input();
    live.ApplyCandidates(full,{language});
    Expect(!live.ApplyTypingPrefix(prefix,japan) && live.visible_text()==u8"日本語","newer result wins");
    live.Type("k");live.Type("y");
    Expect(live.visible_text()==u8"日本語ky" && !live.needs_typing_lookup(),"unfinished digraph retains kanji");
    live.RefineCandidates();
    Expect(live.needs_typing_lookup() && !live.candidates_ready(),"explicit refinement always requests full input");
    live.Reset();live.Type("aozora");Candidate sky;sky.output_text=u8"青空";
    Expect(live.ApplyCandidates(live.decode_input(),{sky}),"establish a converted prefix");
    live.Type("ka");const auto short_reading=live.decode_input();live.Type("ze");
    Candidate fragment;fragment.output_text=u8"青空蚊";
    Expect(!live.ApplyTypingPrefix(short_reading,fragment) && live.visible_text()==u8"青空かぜ",
           "a stale spelling for a lone kana does not overwrite the continued word");
    fragment.output_text=u8"青空ka";
    Expect(!live.ApplyTypingPrefix(short_reading,fragment) && live.visible_text()==u8"青空かぜ",
           "a stale residual romaji fragment does not replace a complete reading");
    Candidate wind;wind.output_text=u8"青空風";
    Expect(live.ApplyCandidates(live.decode_input(),{wind}) && live.visible_text()==u8"青空風",
           "the full current reading still updates normally");
    live.Reset();live.Type("nihong");
    Expect(!live.ApplyTypingPrefix(prefix,japan),"old composition never supplies a new prefix");
    live.Reset();live.Type("nihon");const auto before=live.decode_input();live.Type("g");live.Backspace();live.Type("g");
    Expect(!live.ApplyTypingPrefix(before,japan),"delete and retype invalidate the old request");
    live.Reset();live.Type("nihon");const auto ambiguous=live.decode_input();live.Type("a");
    Expect(!live.ApplyTypingPrefix(ambiguous,japan),"an n reinterpreted as na is not a matching kana prefix");
    live.Reset();live.Type("nihon");const auto context=live.decode_input();live.Type("g");live.set_context("","x");
    Expect(!live.ApplyTypingPrefix(context,japan),"context change rejects late preview");
    live.set_field("identifier");live.Type("o");
    Expect(!live.ApplyTypingPrefix(context,japan),"identifier field rejects late preview");
  }
  {
    for (const auto& item : std::vector<std::pair<std::string,std::string>>{
             {"kyou,ame",u8"今日、雨"},{"[kyou]",u8"「今日」"}}) {
      Session live;live.set_deferred_decoding(true);live.Type(item.first);
      const auto preview=live.preview_request();Candidate first;first.output_text=item.second;
      live.Type("g");
      Expect(live.ApplyTypingPrefix(preview,first) && live.visible_text()==item.second+"g",
             "a delayed Japanese prefix survives punctuation: "+item.first);
      live.Type("a");
      Expect(live.visible_text()==item.second+u8"が","punctuated prefix stays visible across keys");
    }
    Session live;live.set_deferred_decoding(true);live.Type("hashi");
    const auto typing=live.decode_input();
    live.RefineCandidates();
    Candidate dictionary;dictionary.output_text=u8"橋";
    Candidate neural;neural.output_text=u8"箸";
    Expect(live.ApplyPendingPreview(typing,{dictionary}) && live.visible_text()==u8"橋" &&
           !live.candidates_ready(),"exact last preview can display while full conversion remains pending");
    const auto full=live.decode_input();
    Candidate interim;interim.output_text=u8"端";
    auto stale=full;stale.context_generation++;
    Expect(!live.ApplyPendingPreview(stale,{interim},1),"intermediate from another context rejected");
    stale=full;stale.interaction_generation++;
    Expect(!live.ApplyPendingPreview(stale,{interim},1),"intermediate from another interaction rejected");
    Expect(live.ApplyPendingPreview(full,{interim},1) && live.visible_text()==u8"端" && !live.candidates_ready() &&
           !live.ApplyPendingPreview(typing,{dictionary}),"neural intermediate precedes a late typing reply");
    Expect(live.ApplyPendingPreview(full,{neural},2) && !live.ApplyPendingPreview(full,{interim},1) &&
           live.visible_text()==u8"箸" && !live.candidates_ready(),"newest neural intermediate stays provisional");
    auto selected=live;selected.PressSpace();
    Expect(!selected.ApplyPendingPreview(full,{dictionary},3) &&
           !selected.ApplyPendingPreview(selected.decode_input(),{dictionary},3),"selection protects visible candidate");
    auto provisional=live;provisional.PressEnter();
    Expect(!provisional.last_commit_learnable() && provisional.learning_log().empty(),
           "an automatically committed intermediate cannot train the IME");
    Expect(live.ApplyCandidates(live.decode_input(),{neural}) && live.candidates_ready() &&
           !live.ApplyPendingPreview(typing,{dictionary}) && !live.ApplyPendingPreview(full,{interim},3) &&
           live.visible_text()==u8"箸",
           "late dictionary text cannot replace a resolved full result");
    live.Type("wo");live.RefineCandidates();
    Expect(!live.ApplyPendingPreview(typing,{dictionary}),"old revision cannot supply a pending preview");
  }
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

  {
    std::string paragraph;
    for (int i = 0; i < 40; ++i) paragraph += "nihongo";
    const auto parts = Decode(Make(paragraph));
    Expect(!parts.empty() && !parts.front().output_text.empty(), "long prose returns a conversion");
    Expect(Top(Make("editor.team+ime@research.example.jp")) == "editor.team+ime@research.example.jp",
           "dotted email and plus suffix retain their complete spelling");
    Expect(Top(Make("HTTPS://Example.org/LongPath")) == "HTTPS://Example.org/LongPath",
           "mixed-case URL scheme is protected");
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
