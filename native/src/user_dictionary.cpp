#include "user_dictionary.h"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <algorithm>
#include <set>
#include "nlohmann/json.hpp"
namespace ime {
std::filesystem::path UserDictionaryPath() {
  PWSTR folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &folder))) return {};
  auto path = std::filesystem::path(folder) / L"ImeMixed" / L"user_dictionary.tsv";
  CoTaskMemFree(folder); return path;
}
bool UserDictionary::Load(const std::filesystem::path& path, std::string* error) {
  auto fail = [&](const char* why) { if (error) *error = why; return false; };
  std::error_code ec;
  if (std::filesystem::file_size(path, ec) > 4*1024*1024 || ec) return fail("Missing dictionary or size exceeds 4 MiB");
  std::ifstream file(path, std::ios::binary);
  if (!file) return fail("Cannot open dictionary");
  std::map<std::string,std::vector<std::string>> entries;
  std::set<std::string> symbol_only_readings;
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
      if (words.size() == 1 && (pos == u8"記号" || pos == "symbol")) symbol_only_readings.insert(reading);
      else if (pos != u8"記号" && pos != "symbol") symbol_only_readings.erase(reading);
      static const std::map<std::string,std::string> categories{
        {u8"名詞","noun"},{u8"固有名詞","proper_noun"},{u8"人名","personal_name"},{u8"姓","family_name"},
        {u8"名","first_name"},{u8"地名","place_name"},{u8"組織","organization"},{u8"サ変名詞","sahen_noun"},
        {u8"形容詞","adjective"},{u8"副詞","adverb"},{u8"感動詞","interjection"},{u8"記号","symbol"}};
      auto category = categories.find(pos);
      if (category != categories.end()) pos = category->second;
      export_entries.push_back({{"reading",reading},{"word",word},{"pos",pos}});
    }
    if (count > 100000 || words.size() > 32) return fail("Too many entries (100000 total, 32 per reading)");
  }
  if (!file.eof()) return fail("Dictionary read failed");
  auto json = export_entries.dump();
  entries_ = std::move(entries); symbol_only_readings_ = std::move(symbol_only_readings);
  count_ = count; json_ = std::move(json);
  if (error) error->clear(); return true;
}
void UserDictionary::Apply(const DecodeInput& input, std::vector<Candidate>* candidates,
                           bool public_dictionary) const {
  if (entries_.empty() || input.raw_text.size() > 512 || input.field == "code" || input.field == "identifier" || input.candidate_limit <= 0) return;
  bool ok = false;
  auto reading = ConvertRomaji(input.raw_text, &ok);
  if (!ok && public_dictionary) {
    // The public reading may be a prefix of a mixed Japanese/English sentence.
    // ConvertRomaji preserves the Latin tail when it reaches a proper noun.
    // Match a registered prefix before that boundary, then let the normal
    // decoder handle the remaining raw input as a separate clause.
    for (size_t end = reading.size(); end >= 3; --end) {
      if (end < reading.size() &&
          (static_cast<unsigned char>(reading[end]) & 0xc0) == 0x80) continue;
      auto found = entries_.find(reading.substr(0, end));
      if (found == entries_.end()) continue;
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
    // Longest registered term at the earliest scalar boundary. Offer it as an
    // alternative, preserving both surrounding spans; never auto-rewrite prose.
    for (size_t start=0; start<reading.size() && it==entries_.end(); ++start) {
      if ((static_cast<unsigned char>(reading[start]) & 0xC0) == 0x80) continue;
      for (size_t end=reading.size(); end>start; --end) {
        if (end<reading.size() && (static_cast<unsigned char>(reading[end]) & 0xC0) == 0x80) continue;
        // Single-kana registrations should not manufacture mid-word replacements.
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
      symbol_only_readings_.count(reading) && !first_is_arrow;
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
}
