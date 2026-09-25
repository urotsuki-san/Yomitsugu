#include "ime_engine.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace ime;

struct Case {
  const char* id;
  const char* raw;
  std::vector<std::string> expects;  // any match in top-k counts (unless top1)
  bool top1_required;
  const char* phase;
  bool punct;
};

static std::string Top(const std::vector<Candidate>& c) {
  return c.empty() ? std::string() : c[0].output_text;
}

static bool Has(const std::vector<Candidate>& c, const std::string& e) {
  for (auto& x : c)
    if (x.output_text == e) return true;
  return false;
}

static bool HasPrefixInAny(const std::vector<Candidate>& c, const std::string& e) {
  for (auto& x : c)
    if (x.output_text.find(e) != std::string::npos) return true;
  return false;
}

int main() {
  if (!AzookeyEnsureReady()) {
    std::printf("azookey NOT ready - abort\n");
    return 2;
  }
  std::printf("azookey ready zenzai=%s\n", AzookeyZenzaiStatus().c_str());

  const Case cases[] = {
      // --- typos ---
      {"T01", "gakou", {"\xe5\xad\xa6\xe6\xa0\xa1"}, false, "end_of_phrase", false},
      {"T02", "gakkou", {"\xe5\xad\xa6\xe6\xa0\xa1"}, true, "end_of_phrase", false},
      {"T03", "konitiha", {"\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf", "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x82\x8f"}, true, "end_of_phrase", false},
      {"T04", "konnitiha", {"\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"}, true, "end_of_phrase", false},
      {"T05", "konnitiwa", {"\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"}, true, "end_of_phrase", false},
      {"T06", "konnnichiha", {"\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf"}, false, "end_of_phrase", false},
      {"T07", "arigatougozaimas", {"\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86\xe3\x81\x94\xe3\x81\x96\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x99"}, false, "end_of_phrase", false},
      {"T08", "arigatougozaimasu", {"\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86\xe3\x81\x94\xe3\x81\x96\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x99"}, true, "end_of_phrase", false},
      {"T09", "de.s", {"\xe3\x81\xa7\xe3\x81\x99", "de.s"}, false, "end_of_phrase", false},
      {"T11", "watasiha", {"\xe7\xa7\x81\xe3\x81\xaf", "\xe3\x82\x8f\xe3\x81\x9f\xe3\x81\x97\xe3\x81\xaf"}, false, "end_of_phrase", false},
      {"T12", "watashiha", {"\xe7\xa7\x81\xe3\x81\xaf", "\xe3\x82\x8f\xe3\x81\x9f\xe3\x81\x97\xe3\x81\xaf"}, true, "end_of_phrase", false},
      {"T13", "kyouha", {"\xe4\xbb\x8a\xe6\x97\xa5\xe3\x81\xaf", "\xe3\x81\x8d\xe3\x82\x87\xe3\x81\x86\xe3\x81\xaf"}, false, "end_of_phrase", false},
      {"T17", "hai", {"\xe3\x81\xaf\xe3\x81\x84"}, true, "end_of_phrase", false},
      {"T18", "zyouzu", {"\xe4\xb8\x8a\xe6\x89\x8b", "\xe3\x81\x98\xe3\x82\x87\xe3\x81\x86\xe3\x81\x9a"}, false, "end_of_phrase", false},
      {"T19", "jouzu", {"\xe4\xb8\x8a\xe6\x89\x8b", "\xe3\x81\x98\xe3\x82\x87\xe3\x81\x86\xe3\x81\x9a"}, false, "end_of_phrase", false},
      {"T20", "gengogaku", {"\xe8\xa8\x80\xe8\xaa\x9e\xe5\xad\xa6"}, false, "end_of_phrase", false},
      {"T21", "tyuo", {"\xe4\xb8\xad\xe5\xa4\xae", "\xe4\xb8\xad\xe6\xac\xa7", "\xe3\x81\xa1\xe3\x82\x85\xe3\x81\x86\xe3\x81\x8a\xe3\x81\x86"}, false, "end_of_phrase", false},
      {"T22", "chuo", {"\xe4\xb8\xad\xe5\xa4\xae", "\xe4\xb8\xad\xe6\xac\xa7", "\xe3\x81\xa1\xe3\x82\x85\xe3\x81\x86\xe3\x81\x8a\xe3\x81\x86"}, false, "end_of_phrase", false},
      {"T24", "sindai", {"\xe8\xba\xab\xe4\xbd\x93", "\xe5\xaf\x9d\xe5\x8f\xb0", "\xe6\x96\xb0\xe5\x8f\xb0", "\xe3\x81\x97\xe3\x82\x93\xe3\x81\xa0\xe3\x81\x84"}, false, "end_of_phrase", false},
      {"T25", "kisyo", {"\xe6\xb0\x97\xe8\xb1\xa1", "\xe3\x81\x8d\xe3\x81\x97\xe3\x82\x87\xe3\x81\x86"}, false, "end_of_phrase", false},
      {"T26", "daijoubudesu", {"\xe5\xa4\xa7\xe4\xb8\x88\xe5\xa4\xab\xe3\x81\xa7\xe3\x81\x99"}, false, "end_of_phrase", false},
      {"T27", "daijyoubudesu", {"\xe5\xa4\xa7\xe4\xb8\x88\xe5\xa4\xab\xe3\x81\xa7\xe3\x81\x99"}, false, "end_of_phrase", false},
      {"T28", "arigatuugozaimasu", {"\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86\xe3\x81\x94\xe3\x81\x96\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x99"}, false, "end_of_phrase", false},
      {"T29", "gakusei", {"\xe5\xad\xa6\xe7\x94\x9f"}, false, "end_of_phrase", false},
      {"T30", "sensei", {"\xe5\x85\x88\xe7\x94\x9f"}, false, "end_of_phrase", false},
      {"T31", "tokyo", {"\xe6\x9d\xb1\xe4\xba\xac", "\xe3\x81\xa8\xe3\x81\x86\xe3\x81\x8d\xe3\x82\x87\xe3\x81\x86"}, false, "end_of_phrase", false},
      {"T32", "nihongo", {"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}, false, "end_of_phrase", false},

      // --- long / mixed ---
      {"L01", "kyouhaiitennkidesune.", {"\xe4\xbb\x8a\xe6\x97\xa5\xe3\x81\xaf\xe3\x81\x84\xe3\x81\x84\xe5\xa4\xa9\xe6\xb0\x97\xe3\x81\xa7\xe3\x81\x99\xe3\x81\xad\xe3\x80\x82"}, true, "sentence_end", true},
      {"L02", "kyouhaidesugokumoiidesune.", {"\xe4\xbb\x8a\xe6\x97\xa5\xe3\x81\xaf"}, false, "sentence_end", true},
      {"L03", "asanomadeisogasiidesita", {"\xe6\x9c\x9d\xe3\x81\xbe\xe3\x81\xa7"}, false, "end_of_phrase", false},
      {"L04", "arigatougozaimasitahonntouni", {"\xe3\x81\x82\xe3\x82\x8a\xe3\x81\x8c\xe3\x81\xa8\xe3\x81\x86\xe3\x81\x94\xe3\x81\x96\xe3\x81\x84\xe3\x81\xbe\xe3\x81\x97\xe3\x81\x9f"}, false, "end_of_phrase", false},
      {"L06", "Githubnoripojitoriwokousinnsitekudasai", {"Github\xe3\x81\xae\xe3\x83\xaa\xe3\x83\x9d\xe3\x82\xb8\xe3\x83\x88\xe3\x83\xaa\xe3\x82\x92\xe6\x9b\xb4\xe6\x96\xb0\xe3\x81\x97\xe3\x81\xa6\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84\xe3\x80\x82", "Github\xe3\x81\xae\xe3\x83\xaa\xe3\x83\x9d\xe3\x82\xb8\xe3\x83\x88\xe3\x83\xaa\xe3\x82\x92\xe6\x9b\xb4\xe6\x96\xb0\xe3\x81\x97\xe3\x81\xa6\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84"}, false, "sentence_end", true},
      {"L07", "Pythonnositengodeumakuugokimasendesita", {"Python"}, false, "end_of_phrase", false},
      {"L08", "mainitenomasiyouwokurikaesitekudasai", {"\xe6\xaf\x8e\xe6\x97\xa5"}, false, "end_of_phrase", false},
      {"L14", "sukodesu", {"\xe5\xa5\xbd\xe3\x81\x8d\xe3\x81\xa7\xe3\x81\x99", "\xe3\x82\xb9\xe3\x82\xb3\xe3\x81\xa7\xe3\x81\x99"}, false, "end_of_phrase", false},
      {"L15", "daijoubudesu", {"\xe5\xa4\xa7\xe4\xb8\x88\xe5\xa4\xab\xe3\x81\xa7\xe3\x81\x99"}, false, "end_of_phrase", false},
      {"L17", "syukudaiwohagesimitimasyou", {"\xe5\xae\xbf\xe9\xa1\x8c"}, false, "end_of_phrase", false},
      {"L18", "honntowaniomosirokatta", {"\xe6\x9c\xac\xe5\xbd\x93"}, false, "end_of_phrase", false},
      {"L20", "korehokanjidesu", {"\xe3\x81\x93\xe3\x82\x8c\xe3\x81\xaf\xe6\xbc\xa2\xe5\xad\x97\xe3\x81\xa7\xe3\x81\x99"}, false, "end_of_phrase", false},

      // --- preserve must not regress ---
      {"E01", "Please update the Github repository.", {"Please update the Github repository."}, true, "end_of_phrase", false},
      {"E02", "This is a nice day.", {"This is a nice day."}, true, "end_of_phrase", false},
      {"E04", "hello world", {"hello world"}, true, "end_of_phrase", false},
      {"E05", "The quick brown fox jumps over the lazy dog.", {"The quick brown fox jumps over the lazy dog."}, true, "end_of_phrase", false},
      {"P01", "https://example.com/konnitiwa?q=no", {"https://example.com/konnitiwa?q=no"}, true, "end_of_phrase", false},
      {"U01", "Githubnoripojitoriwokousinnsitekudasai", {"Github\xe3\x81\xae\xe3\x83\xaa\xe3\x83\x9d\xe3\x82\xb8\xe3\x83\x88\xe3\x83\xaa\xe3\x82\x92\xe6\x9b\xb4\xe6\x96\xb0\xe3\x81\x97\xe3\x81\xa6\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84\xe3\x80\x82"}, true, "sentence_end", true},
      {"U03", "kiyoiitennkidesune.", {"\xe4\xbb\x8a\xe6\x97\xa5\xe3\x81\xaf\xe3\x81\x84\xe3\x81\x84\xe5\xa4\xa9\xe6\xb0\x97\xe3\x81\xa7\xe3\x81\x99\xe3\x81\xad\xe3\x80\x82"}, false, "sentence_end", true},
  };

  int pass = 0, fail = 0, top1_ok = 0, top1_need = 0;
  for (const auto& c : cases) {
    DecodeInput in;
    in.raw_text = c.raw;
    in.phase = c.phase;
    in.punctuation_completion = c.punct;
    in.field = "prose";
    in.candidate_limit = 8;
    auto outs = Decode(in);
    bool hit_top1 = false;
    bool hit_any = false;
    for (const auto& e : c.expects) {
      if (!outs.empty() && outs[0].output_text == e) hit_top1 = true;
      if (Has(outs, e) || HasPrefixInAny(outs, e)) hit_any = true;
    }
    bool ok = c.top1_required ? hit_top1 : hit_any;
    if (c.top1_required) {
      ++top1_need;
      if (hit_top1) ++top1_ok;
    }
    if (ok) {
      ++pass;
      std::printf("[OK ] %s top1=%s\n", c.id, Top(outs).c_str());
    } else {
      ++fail;
      std::printf("[NG ] %s raw=%s\n", c.id, c.raw);
      std::printf("     top1=%s\n", Top(outs).c_str());
      for (size_t i = 0; i < outs.size() && i < 5; ++i)
        std::printf("     [%zu] %s\n", i, outs[i].output_text.c_str());
    }
  }
  std::printf("SUMMARY pass=%d fail=%d top1=%d/%d\n", pass, fail, top1_ok, top1_need);
  return fail ? 1 : 0;
}
