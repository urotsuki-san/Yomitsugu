#include "user_dictionary.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <algorithm>
#include <set>
#include <cstring>
#include <cctype>
#include "nlohmann/json.hpp"
namespace ime {
std::filesystem::path PublicDictionaryPath(const std::filesystem::path& bundled,
                                           const std::filesystem::path& cached) {
  // 古い記号辞書が新版を隠さないよう、出典ファイルの形式番号を確認する。
  std::error_code ec;
  if (!std::filesystem::exists(cached, ec)) return bundled;
  auto metadata = cached; metadata.replace_extension(L".sources.json");
  if (std::filesystem::file_size(metadata, ec)>8192 || ec) return bundled;
  std::ifstream stream(metadata, std::ios::binary);
  auto json=nlohmann::json::parse(stream, nullptr, false);
  if (!json.is_object() || !json.contains("format_version") ||
      !json["format_version"].is_number_integer() || json["format_version"]!=2) return bundled;
  return cached;
}
std::filesystem::path UserDictionaryPath() {
  PWSTR folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) return {};
  auto path = std::filesystem::path(folder) / L"ImeMixed" / L"user_dictionary.tsv";
  CoTaskMemFree(folder); return path;
}
bool UserDictionary::Load(const std::filesystem::path& path, std::string* error, bool public_dictionary) {
  auto fail = [&](const char* why) { if (error) *error = why; return false; };
  std::error_code ec;
  const auto max_bytes = public_dictionary ? 32*1024*1024 : 4*1024*1024;
  if (std::filesystem::file_size(path, ec) > max_bytes || ec) return fail("Missing dictionary or size exceeds limit");
  std::ifstream file(path, std::ios::binary);
  if (!file) return fail("Cannot open dictionary");
  std::map<std::string,std::vector<std::string>> entries;
  std::set<std::string> symbol_only_readings;
  std::set<std::string> supplemental_readings;
  std::set<std::string> priority_readings;
  std::set<std::string> english_words;
  std::map<std::string,std::vector<std::string>> supplemental_words;
  auto export_entries = nlohmann::json::array();
  std::string line; size_t count = 0, row = 0;
  while (std::getline(file,line)) {
    ++row;
    if (row == 1 && line.compare(0,3,"\xEF\xBB\xBF") == 0) line.erase(0,3);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#' || line[0] == '!') continue;
    if (line.find('\0') != std::string::npos || line.size() > 4096 ||
        !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), static_cast<int>(line.size()), nullptr, 0))
      return fail("Dictionary must be valid UTF-8, maximum 4096 bytes per row");
    auto tab = line.find('\t'); if (tab == std::string::npos) return fail("Expected reading TAB word [TAB part-of-speech]");
    auto next = line.find('\t',tab+1);
    auto reading = line.substr(0,tab), word = line.substr(tab+1, next == std::string::npos ? next : next-tab-1);
    if (reading.empty() || reading.size() > 512 || word.empty() || word.size() > 1024)
      return fail("Empty or oversized reading/word");
    auto& words = entries[reading];
    if (std::find(words.begin(),words.end(),word) == words.end()) {
      words.push_back(word); ++count;
      std::string pos = next == std::string::npos ? "noun" : line.substr(next+1, line.find('\t',next+1)-next-1);
      if (pos == u8"英単語" && reading == word) english_words.insert(reading);
      if (pos == u8"名詞" || pos == "noun") priority_readings.insert(reading);
      if (pos == u8"補助語") {
        supplemental_readings.insert(reading);
        supplemental_words[reading].push_back(word);
      }
      if (words.size() == 1 && (pos == u8"記号" || pos == "symbol")) symbol_only_readings.insert(reading);
      else if (pos != u8"記号" && pos != "symbol") symbol_only_readings.erase(reading);
      static const std::map<std::string,std::string> categories{
        {u8"名詞","noun"},{u8"固有名詞","proper_noun"},{u8"人名","personal_name"},{u8"姓","family_name"},
        {u8"名","first_name"},{u8"地名","place_name"},{u8"組織","organization"},{u8"サ変名詞","sahen_noun"},
        {u8"形容詞","adjective"},{u8"副詞","adverb"},{u8"感動詞","interjection"},{u8"記号","symbol"}};
      auto category = categories.find(pos);
      if (category != categories.end()) pos = category->second;
      if (!public_dictionary) export_entries.push_back({{"reading",reading},{"word",word},{"pos",pos}});
    }
    if (count > (public_dictionary ? 700000 : 100000) || words.size() > 32) return fail("Too many dictionary entries");
  }
  if (!file.eof()) return fail("Dictionary read failed");
  auto json = export_entries.dump();
  entries_ = std::move(entries); symbol_only_readings_ = std::move(symbol_only_readings);
  supplemental_readings_ = std::move(supplemental_readings);
  priority_readings_ = std::move(priority_readings);
  english_words_ = std::move(english_words);
  supplemental_words_ = std::move(supplemental_words);
  count_ = count; json_ = std::move(json);
  if (error) error->clear(); return true;
}
void UserDictionary::Apply(const DecodeInput& input, std::vector<Candidate>* candidates,
                           bool public_dictionary) const {
  if (public_dictionary && ApplyEnglish(input,candidates)) return;
  ApplyExact(input, candidates, public_dictionary);
  if (public_dictionary) ApplyCorrections(input, candidates);
}
bool UserDictionary::ApplyEnglish(const DecodeInput& input, std::vector<Candidate>* candidates) const {
  if (input.field!="prose" || input.candidate_limit<=0 || input.raw_text.size()>512) return false;
  auto raw=input.raw_text, lower=raw;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
  bool ok=false;auto reading=ConvertRomaji(raw,&ok);
  // animeやsushiのように日本語の読みでもある単語は、かな漢字変換を優先する。
  if(ok && entries_.count(reading)) return false;
  std::string output;
  bool exact=english_words_.count(lower)!=0;
  if(exact) output=raw;
  else {
    for(size_t end=std::min<size_t>(30,raw.size());end>=4;--end) {
      if(end==raw.size() || !english_words_.count(lower.substr(0,end))) continue;
      bool prefix_ok=false;
      const auto prefix_reading=ConvertRomaji(raw.substr(0,end),&prefix_ok);
      if(prefix_ok && entries_.count(prefix_reading)) continue;
      auto suffix=raw.substr(end);
      bool particle=false;
      for(auto prefix: {"wo","ga","no","ni","ha","de","to","mo","he"})
        if(suffix.compare(0,std::strlen(prefix),prefix)==0) particle=true;
      bool suffix_ok=false;ConvertRomaji(suffix,&suffix_ok);
      if(!particle || !suffix_ok) continue;
      auto request=input;request.raw_text=suffix;
      auto converted=Decode(request);
      if(converted.empty() || converted.front().output_text==suffix) continue;
      output=raw.substr(0,end)+converted.front().output_text;
      break;
    }
  }
  if(output.empty()) return false;
  std::vector<Candidate> combined;std::set<std::string> seen;
  Candidate first;first.output_text=output;first.reading_text=reading;first.is_raw=exact;
  combined.push_back(first);seen.insert(output);
  for(const auto& candidate:*candidates) if(seen.insert(candidate.output_text).second) combined.push_back(candidate);
  const auto limit=static_cast<size_t>(input.candidate_limit);
  if(combined.size()>limit) combined.resize(limit);
  if(limit>1 && std::none_of(combined.begin(),combined.end(),[&](const Candidate& c){return c.output_text==raw;})) {
    if(combined.size()==limit) combined.pop_back();
    Candidate literal;literal.output_text=literal.reading_text=raw;literal.is_raw=true;combined.push_back(literal);
  }
  *candidates=std::move(combined);
  return true;
}
void UserDictionary::ApplyExact(const DecodeInput& input, std::vector<Candidate>* candidates,
                                bool public_dictionary) const {
  if (entries_.empty() || input.raw_text.size() > 512 || input.candidate_limit <= 0 ||
      input.field == "code" || input.field == "identifier" || input.field == "password" ||
      input.field == "url" || input.field == "path" || input.field == "email") return;
  bool ok = false;
  auto reading = ConvertRomaji(input.raw_text, &ok);
  if (!ok && public_dictionary) {
    // 英語が続く場合も文頭の登録語を探し、残った入力を変換する。
    for (size_t end = reading.size(); end >= 3; --end) {
      if (end < reading.size() &&
          (static_cast<unsigned char>(reading[end]) & 0xc0) == 0x80) continue;
      auto found = entries_.find(reading.substr(0, end));
      if (found == entries_.end()) continue;
      if (supplemental_readings_.count(found->first) && !priority_readings_.count(found->first)) continue;
      size_t raw_end = 0;
      for (size_t i = 1; i <= input.raw_text.size(); ++i) {
        bool prefix_ok = false;
        auto converted = ConvertRomaji(input.raw_text.substr(0, i), &prefix_ok);
        if (prefix_ok && converted == found->first) { raw_end = i; break; }
      }
      if (!raw_end || raw_end >= input.raw_text.size()) continue;
      DecodeInput suffix_input = input;
      suffix_input.raw_text = input.raw_text.substr(raw_end);
      auto suffix_candidates = Decode(suffix_input);
      if (suffix_candidates.empty() || suffix_candidates.front().is_raw) continue;
      std::vector<Candidate> combined;
      std::set<std::string> seen;
      for (const auto& word : found->second) {
        Candidate candidate;
        candidate.output_text = word + suffix_candidates.front().output_text;
        candidate.reading_text = reading;
        if (seen.insert(candidate.output_text).second) combined.push_back(std::move(candidate));
        if (combined.size() >= 3) break;
      }
      for (const auto& candidate : *candidates) {
        if (seen.insert(candidate.output_text).second) combined.push_back(candidate);
      }
      Candidate raw; raw.output_text = raw.reading_text = input.raw_text; raw.is_raw = true;
      const auto limit = static_cast<size_t>(input.candidate_limit);
      if (combined.size() > limit) combined.resize(limit);
      if (limit > 1 && !seen.count(input.raw_text)) {
        if (combined.size() == limit) combined.pop_back();
        combined.push_back(std::move(raw));
      }
      *candidates = std::move(combined);
      return;
    }
  }
  if (!ok) reading = input.raw_text;
  auto it = entries_.find(reading);
  const bool exact = it != entries_.end();
  std::string prefix, suffix;
  if (!exact) {
    // 最初に見つかる最長の登録語を使い、前後の文字列を残した候補を作る。
    for (size_t start=0; start<reading.size() && it==entries_.end(); ++start) {
      if ((static_cast<unsigned char>(reading[start]) & 0xC0) == 0x80) continue;
      for (size_t end=reading.size(); end>start; --end) {
        if (end<reading.size() && (static_cast<unsigned char>(reading[end]) & 0xC0) == 0x80) continue;
        // 1文字の読みは単語の途中に適用しない。
        if (end-start<6) continue;
        auto found=entries_.find(reading.substr(start,end-start));
        if (found != entries_.end()) { it=found; prefix=reading.substr(0,start); suffix=reading.substr(end); break; }
      }
    }
    if (it==entries_.end()) return;
    auto surface=[](const std::string& kana) {
      if(kana.empty())return kana;
      auto list=AzookeyConvert(kana,1); return list.empty()?kana:list.front();
    };
    prefix=surface(prefix); suffix=surface(suffix);
  }
  std::vector<Candidate> combined; std::set<std::string> seen;
  bool first_is_arrow = false;
  if (exact && !it->second.empty()) {
    const auto& first = it->second.front();
    if (first.size() >= 3) {
      const auto a = static_cast<unsigned char>(first[0]);
      const auto b = static_cast<unsigned char>(first[1]);
      const auto c = static_cast<unsigned char>(first[2]);
      const auto codepoint = ((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f);
      first_is_arrow = a == 0xe2 && 0x2190 <= codepoint && codepoint <= 0x21ff;
    }
  }
  const bool preserve_primary = public_dictionary && exact &&
      ((symbol_only_readings_.count(reading) && !first_is_arrow) ||
       (supplemental_readings_.count(reading) && !priority_readings_.count(reading) && !first_is_arrow));
  if ((!exact || preserve_primary) && !candidates->empty()) {
    combined.push_back(candidates->front()); seen.insert(candidates->front().output_text);
  }
  for (const auto& word : it->second) {
    Candidate c; c.output_text = prefix + word + suffix; c.reading_text = reading;
    if (seen.insert(c.output_text).second) combined.push_back(c);
    if (!exact && combined.size()>=4) break;
  }
  for (const auto& c : *candidates) if (seen.insert(c.output_text).second) combined.push_back(c);
  Candidate raw; raw.output_text = raw.reading_text = input.raw_text; raw.is_raw = true;
  auto limit = static_cast<size_t>(input.candidate_limit);
  if (combined.size() > limit) combined.resize(limit);
  if (limit > 1 && std::none_of(combined.begin(),combined.end(),[&](const Candidate& c){return c.output_text == input.raw_text;})) {
    if (combined.size() == limit) combined.pop_back(); combined.push_back(raw);
  }
  *candidates = std::move(combined);
}

void UserDictionary::ApplyCorrections(const DecodeInput& input, std::vector<Candidate>* candidates) const {
  // 補正した読みを先に辞書で照合し、変換器に渡す候補を絞る。
  if (input.field != "prose" || input.candidate_limit < 2 || supplemental_readings_.empty()) return;
  auto raw = input.raw_text;
  std::string punctuation;
  if (!raw.empty() && raw.back() == '.') { raw.pop_back(); punctuation = u8"。"; }
  if (raw.size() < 6 || raw.size() > 64 ||
      raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz-'") != std::string::npos) return;
  bool original_ok = false;
  const auto original = ConvertRomaji(raw, &original_ok);
  if (entries_.count(original)) return; // valid dictionary words are never corrected

  struct Repair { int cost; bool promote; };
  std::map<std::string, Repair> readings;
  auto try_raw = [&](std::string edited, int cost, bool promote = false) {
    bool ok = false;
    auto reading = ConvertRomaji(edited, &ok);
    if (!ok || reading == original) return;
    auto it = readings.find(reading);
    if (it == readings.end() || cost < it->second.cost) readings[reading] = {cost, promote};
  };
  // niやnaの通常の読みを残し、nの後で区切る読みも辞書で照合する。
  for (size_t i=0; i<raw.size(); ++i) {
    if (raw[i]=='n' && i+1<raw.size() && std::strchr("aiueoy",raw[i+1]))
      try_raw(raw.substr(0,i+1)+"'"+raw.substr(i+1), 5, true);
    if (raw[i]=='n' && i+2<raw.size() && raw[i+1]=='n' && std::strchr("aiueoy",raw[i+2]))
      try_raw(raw.substr(0,i)+"n'"+raw.substr(i+2), 5, true);
    if (raw[i]=='m' && i+1<raw.size() && std::strchr("bmp",raw[i+1])) {
      auto edited=raw; edited[i]='n'; try_raw(edited, 8, true);
    }
    if (i+1<raw.size() && raw[i]==raw[i+1])
      try_raw(raw.substr(0,i)+raw.substr(i+1), 8, true);
    if (i+1<raw.size() && raw[i]!=raw[i+1]) {
      auto edited=raw; std::swap(edited[i],edited[i+1]); try_raw(edited, 12);
    }
    if (raw[i]=='-') {
      // 長音の打鍵が前後の音節へずれた場合を試す。
      auto without=raw.substr(0,i)+raw.substr(i+1);
      const auto begin=i>3?i-3:0;
      for(size_t j=begin;j<=std::min(without.size(),i+3);++j)
        if(j!=i) try_raw(without.substr(0,j)+"-"+without.substr(j),18);
    }
    try_raw(raw.substr(0,i)+raw.substr(i+1), 20);
    // 1文字を置き換え、辞書にある読みだけを補正候補にする。
    for (char ch : std::string("abcdefghijklmnopqrstuvwxyz-")) {
      if (ch==raw[i]) continue;
      auto edited=raw; edited[i]=ch; try_raw(edited, 24);
    }
  }
  // 1文字の抜けと長音を補う。探索は入力長と1回の編集に制限する。
  for (size_t i=0; i<=raw.size(); ++i)
    for (char ch : std::string("aiueon-"))
      try_raw(raw.substr(0,i)+ch+raw.substr(i), ch=='-' ? 12 : 24);

  struct Match { std::string reading, suffix; Repair repair; };
  std::vector<Match> matches;
  for (const auto& [reading, repair] : readings) {
    for (size_t end=reading.size(); end>=12; --end) {
      if (end<reading.size() && (static_cast<unsigned char>(reading[end])&0xc0)==0x80) continue;
      auto prefix=reading.substr(0,end);
      if (!supplemental_readings_.count(prefix) || original.compare(0,end,prefix)==0) continue;
      auto suffix=reading.substr(end);
      if (!suffix.empty()) {
        bool grammatical=false;
        for (auto particle : {u8"を",u8"に",u8"が",u8"は",u8"で",u8"と",u8"も",u8"の",u8"へ",u8"する",u8"して",u8"した"})
          if (suffix.compare(0,std::strlen(particle),particle)==0) grammatical=true;
        if (!grammatical || end*2<reading.size()) continue;
      }
      matches.push_back({std::move(prefix),std::move(suffix),repair});
      break;
    }
  }
  if (matches.empty()) return;
  std::stable_sort(matches.begin(),matches.end(),[](const Match& a,const Match& b) {
    if (a.repair.cost!=b.repair.cost) return a.repair.cost<b.repair.cost;
    return a.reading.size()>b.reading.size();
  });
  // 表記の正規化は優先し、曖昧な1文字の補正は後続候補に置く。
  bool promote=matches.front().repair.promote &&
      (matches.size()==1 || matches[1].repair.cost>matches[0].repair.cost);
  // 元の文頭が辞書に一致する場合は、その読みを優先する。
  for (size_t end=original.size(); promote && end>=12; --end) {
    if (entries_.count(original.substr(0,end)) && end+6>=matches.front().reading.size()) {
      const auto suffix=original.substr(end);
      for(auto particle : {u8"を",u8"に",u8"が",u8"は",u8"で",u8"と",u8"も",u8"の",u8"へ"})
        if(suffix.compare(0,std::strlen(particle),particle)==0) promote=false;
    }
  }
  if (!candidates->empty() && candidates->front().output_text==input.raw_text) promote=false;
  std::vector<Candidate> combined;
  std::set<std::string> seen;
  auto append=[&](const Candidate& c) { if(seen.insert(c.output_text).second) combined.push_back(c); };
  if (!promote && !candidates->empty()) append(candidates->front());
  for (size_t i=0; i<matches.size() && i<3; ++i) {
    const auto& match=matches[i];
    auto suffix=match.suffix;
    if (!suffix.empty()) {
      auto converted=AzookeyConvert(suffix,1);
      if (!converted.empty()) suffix=converted.front();
    }
    const auto& words=supplemental_words_.at(match.reading);
    for(size_t j=0;j<words.size() && j<2;++j) {
      Candidate c; c.output_text=words[j]+suffix+punctuation;
      c.reading_text=match.reading+match.suffix; c.edit_cost=match.repair.cost/10.0;
      append(c);
    }
  }
  for (const auto& c:*candidates) append(c);
  const auto limit=static_cast<size_t>(input.candidate_limit);
  if(combined.size()>limit) combined.resize(limit);
  if(std::none_of(combined.begin(),combined.end(),[&](const Candidate& c){return c.output_text==input.raw_text;})) {
    if(combined.size()==limit) combined.pop_back();
    Candidate c; c.output_text=c.reading_text=input.raw_text;c.is_raw=true;combined.push_back(c);
  }
  *candidates=std::move(combined);
}
}
