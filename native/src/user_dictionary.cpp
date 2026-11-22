#include "user_dictionary.h"
#include "public_index.h"
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
  if (public_dictionary) {
    auto index = PublicIndex::Open(path, error);
    if (!index) return false;
    *this = UserDictionary{};
    count_ = index->size();
    public_index_ = std::move(index);
    return true;
  }
  auto fail = [&](const char* why) { if (error) *error = why; return false; };
  std::error_code ec;
  const auto max_bytes = 4*1024*1024;
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
      if (count <= 1000) export_entries.push_back({{"reading",reading},{"word",word},{"pos",pos}});
      else if (count == 1001) export_entries = nlohmann::json::array();
    }
    if (count > 100000 || words.size() > 32) return fail("Too many dictionary entries");
  }
  if (!file.eof()) return fail("Dictionary read failed");
  auto json = export_entries.dump();
  public_index_.reset();
  entries_ = std::move(entries); symbol_only_readings_ = std::move(symbol_only_readings);
  supplemental_readings_ = std::move(supplemental_readings);
  priority_readings_ = std::move(priority_readings);
  english_words_ = std::move(english_words);
  supplemental_words_ = std::move(supplemental_words);
  count_ = count; json_ = std::move(json);
  if (error) error->clear(); return true;
}
std::vector<std::pair<std::string,std::string>> UserDictionary::UniqueWordsIn(const std::string& reading, bool* has_entry) const {
  std::vector<std::pair<std::string,std::string>> result;
  if (has_entry) *has_entry = false;
  if (entries_.empty() || reading.size() > 384) return result;
  for (size_t start = 0; start < reading.size(); ++start) {
    if ((static_cast<unsigned char>(reading[start]) & 0xc0) == 0x80) continue;
    for (size_t end = start + 1; end <= reading.size(); ++end) {
      if (end < reading.size() && (static_cast<unsigned char>(reading[end]) & 0xc0) == 0x80) continue;
      const auto prefix = reading.substr(start, end - start);
      auto it = entries_.lower_bound(prefix);
      if (it == entries_.end() || it->first.compare(0, prefix.size(), prefix)) break;
      if (prefix.size() >= 6 && it->first == prefix) {
        if (has_entry) *has_entry = true;
        if (it->second.size() == 1 && !symbol_only_readings_.count(prefix) && !supplemental_readings_.count(prefix))
          result.emplace_back(prefix, it->second.front());
      }
    }
  }
  return result;
}
unsigned UserDictionary::Flags(std::string_view reading) const {
  if (public_index_) return public_index_->Flags(reading);
  const std::string key(reading);
  if (!entries_.count(key)) return 0;
  return Present | (symbol_only_readings_.count(key) ? SymbolOnly : 0) |
         (supplemental_readings_.count(key) ? Supplemental : 0) |
         (priority_readings_.count(key) ? Priority : 0) | (english_words_.count(key) ? English : 0);
}
std::vector<std::string> UserDictionary::Words(std::string_view reading, bool supplemental_only) const {
  if (public_index_) return public_index_->Words(reading, supplemental_only);
  return (supplemental_only ? supplemental_words_ : entries_).at(std::string(reading));
}
std::string UserDictionary::UnfinishedRomaji(const DecodeInput& input) const {
  const auto& raw = input.raw_text;
  if (input.field != "prose" || input.phase != "end_of_phrase" || raw.size() < 4 || raw.size() > 96 ||
      raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz'-") != raw.npos ||
      !std::strchr("bcdfghjklmpqrstvwxyz", raw.back()) || (Flags(raw) & English) || ContainsLatinName(raw)) return {};
  std::string display;
  if (ConvertRomajiChecked(raw, &display)) return {};
  const auto tail = display.find_first_of("abcdefghijklmnopqrstuvwxyz");
  // At least three complete Japanese mora precede one unfinished syllable.
  if (tail == display.npos || tail < 9 || display.size()-tail > 3) return {};
  const auto consonants = display.substr(tail);
  if (consonants.find_first_not_of("bcdfghjklmpqrstvwxyz") != consonants.npos) return {};
  for (const char vowel : std::string_view("aiueo")) {
    if (ConvertRomajiChecked(consonants+vowel, nullptr)) return display;
  }
  return {};
}
void UserDictionary::Apply(const DecodeInput& input, std::vector<Candidate>* candidates,
                           bool public_dictionary, bool final, Candidate* early_promotion, AzookeyReadingSample* sample) const {
  if (public_dictionary && ApplyEnglish(input,candidates)) return;
  ApplyExact(input, candidates, public_dictionary);
  if (public_dictionary) {
    ApplyCorrections(input, candidates);
    if (input.field != "prose" || input.candidate_limit <= 0 || candidates->empty()) return;
    AzookeyReadingComparison comparison;
    comparison.sample = sample;
    if (final && input.phase != "typing" && !input.dictionary_preview && !input.shallow_search) {
      if (!RecoverNasalBoundary(input, *candidates, comparison)) RecoverOmittedConsonant(input, *candidates, comparison);
    } else if (early_promotion && RecoverNasalBoundary(input, *candidates, comparison, true)) {
      *early_promotion = candidates->front();
    }
  }
}
namespace {
bool PlaceRecoveredCandidate(const DecodeInput& input, std::vector<Candidate>& rows,
                             const std::string& text, const std::string& reading, size_t position) {
  if (position >= static_cast<size_t>(input.candidate_limit)) return false;
  position = std::min(position, rows.size());
  auto existing = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.output_text == text; });
  if (existing != rows.end()) {
    if (existing >= rows.begin()+position) std::rotate(rows.begin()+position, existing, existing+1);
    return true;
  }
  if (rows.size() >= static_cast<size_t>(input.candidate_limit)) {
    auto drop = rows.end()-1;
    if (drop != rows.begin() && (drop->is_raw || drop->output_text == input.raw_text)) --drop;
    if (static_cast<size_t>(drop-rows.begin()) < position) return false;
    rows.erase(drop);
  }
  rows.insert(rows.begin()+position, Candidate{text, reading, false});
  return true;
}
}  // namespace
namespace {
// size() denotes multiple possible boundaries. npos denotes none.
bool StartsGrammaticalSuffix(std::string_view suffix) {
  for (const std::string_view prefix : {u8"を",u8"に",u8"が",u8"は",u8"で",u8"と",u8"も",u8"の",u8"へ",u8"する",u8"して",u8"した"})
    if (suffix.compare(0,prefix.size(),prefix)==0) return true;
  return false;
}
size_t NasalBoundary(const std::string& raw) {
  size_t boundary = raw.npos;
  for (size_t i = 1; i + 2 < raw.size(); ++i) {
    if (raw[i] != 'n' || !std::strchr("aiueo", raw[i-1]) || !std::strchr("aiueo", raw[i+1]) ||
        raw[i+2] != 'n' || (i+3 < raw.size() && std::strchr("aiueoy", raw[i+3]))) continue;
    if (boundary != raw.npos) return raw.size();
    boundary = i;
  }
  return boundary;
}
}
bool UserDictionary::MayPromoteNasal(const std::string& raw) {
  if (raw.size() < 4 || raw.size() > 64 || raw.find('n') == raw.npos) return false;
  const auto at = NasalBoundary(raw);
  if (at >= raw.size()) return false;
  // A word inside an unknown transliteration must not capture its suffix.
  // For immediate display require the word to end here or precede a particle.
  size_t suffix = at + 3;
  if (suffix < raw.size() && raw[suffix] == 'n') ++suffix;
  if (suffix == raw.size()) return true;
  std::string tail;
  return ConvertRomajiChecked(raw.substr(suffix), &tail) && StartsGrammaticalSuffix(tail);
}
bool UserDictionary::RecoverNasalBoundary(const DecodeInput& input, std::vector<Candidate>& rows,
                                         AzookeyReadingComparison& comparison, bool top_only) const {
  const auto& raw = input.raw_text;
  if (raw.size() < 4 || raw.size() > 64 || raw.find('n') == raw.npos ||
      raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz") != raw.npos) return false;
  size_t boundary = NasalBoundary(raw);
  if (boundary == raw.size() || (top_only && boundary == raw.npos)) return false;
  const bool whole_word = boundary == raw.npos;
  if (whole_word) {
    // Other vowel and palatalized boundaries are widely ambiguous. Offer
    // just one complete dictionary word, while leaving the first choice intact.
    if (raw.size() > 20 || input.candidate_limit < 2) return false;
    for (size_t i = 1; i + 1 < raw.size(); ++i) {
      if (raw[i] != 'n' || !std::strchr("aiueo", raw[i-1])) continue;
      const char onset = raw[i+1];
      const bool palatalized = onset == 'y' && i+2 < raw.size() && std::strchr("auo", raw[i+2]);
      if (!std::strchr("aiueo", onset) && !palatalized) continue;
      if (boundary != raw.npos) return false;
      boundary = i;
    }
    if (boundary == raw.npos) return false;
  }
  std::string prefix, reading, alternate;
  if (!ConvertRomajiChecked(raw, &reading) ||
      !ConvertRomajiChecked(raw.substr(0,boundary+1) + "'" + raw.substr(boundary+1), &alternate)) return false;
  // In a phrase, require a registered word that spans the changed syllables.
  size_t begin = 0, end = alternate.size(), past_last_start = 1;
  if (!whole_word) {
    if (!ConvertRomajiChecked(raw.substr(0,boundary), &prefix)) return false;
    end = prefix.size() + 9;
    if (end > alternate.size() || alternate.compare(end-3,3,u8"ん") != 0) return false;
    // One trailing n can still become the start of a longer katakana word.
    // Wait for the next key or the ordinary end-of-phrase request when the
    // literal reading lacks its own dictionary headword.
    if (top_only && input.phase == "typing" && end == alternate.size() &&
        raw.back() == 'n' && raw[raw.size()-2] != 'n' && !Flags(reading)) return false;
    if (top_only && end < alternate.size() && !StartsGrammaticalSuffix(std::string_view(alternate).substr(end))) return false;
    begin = prefix.size() > 12 ? prefix.size()-12 : 0;
    past_last_start = prefix.size();
  }
  for (size_t start = begin; start < past_last_start; start+=3) {
    const auto key = std::string_view(alternate).substr(start,end-start);
    if (!Flags(key)) continue;
    const auto words = Words(key);
    const auto& original = rows.front().output_text;
    // Preserve loanwords for which the proposed boundary would split the
    // original transliteration and leave part of the word behind.
    if (end < alternate.size() && original == ConvertCharacterClass(reading, VK_F7)) return false;
    const std::string_view incumbent = rows.front().reading_text == alternate
        ? std::string_view(original) : std::string_view{};
    const bool is_headword = whole_word || Flags(alternate);
    comparison.minimum_gain = is_headword && !top_only ? -5.0f : 5.0f;
    comparison.incumbent = incumbent;
    const auto offered = AzookeyAlternativeReading(reading, alternate, input.left_context, input.right_context, &comparison);
    if (offered.empty()) return false;
    const auto& text = offered.front();
    if (std::none_of(words.begin(),words.end(),[&](const auto& word) {
          return word != key && (whole_word ? text == word : text.find(word) != text.npos);
        })) return false;
    // A single final n can still belong to the next syllable. With no
    // context, keep the literal if independent spellings compete closely
    // for a corrected reading and the ordinary reading has no headword.
    const bool ambiguous_ending = end == alternate.size() && raw.back() == 'n' &&
        raw[raw.size()-2] != 'n' && input.left_context.empty() && input.right_context.empty() &&
        !Flags(reading) && comparison.lead_over_second <= 2.0f;
    if (ambiguous_ending) {
      PlaceRecoveredCandidate(input, rows, reading, reading, 0);
      PlaceRecoveredCandidate(input, rows, text, alternate, 1);
      return true;
    }
    if (text == original) return !top_only || comparison.first_gain > 5.0f;
    const bool repeated = !original.empty() &&
        (input.left_context.find(original) != std::string::npos || input.right_context.find(original) != std::string::npos);
    const size_t position = repeated || whole_word ? 1 : (comparison.first_gain > 5.0f ? 0 : 2);
    if (top_only && position != 0) return false;
    return PlaceRecoveredCandidate(input, rows, text, alternate, position);
  }
  return false;
}
void UserDictionary::RecoverOmittedConsonant(const DecodeInput& input, std::vector<Candidate>& rows,
                                             AzookeyReadingComparison& comparison) const {
  const auto& raw = input.raw_text;
  if (input.candidate_limit < 3 || raw.size() < 3 || raw.size() > 20 ||
      raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz-") != raw.npos) return;
  struct Repair { std::string reading; bool ambiguous = false; } geminate, nasal;
  auto check = [&](Repair& repair, size_t at, const std::string& addition) {
    if (repair.ambiguous) return;
    std::string candidate;
    if (!ConvertRomajiChecked(raw.substr(0,at) + addition + raw.substr(at), &candidate) ||
        !(Flags(candidate) & Supplemental)) return;
    if (!repair.reading.empty() && repair.reading != candidate) repair.ambiguous = true;
    else repair.reading = std::move(candidate);
  };
  // Check whole-word dictionary entries before querying their scores. More
  // than one eligible position for the same edit is deliberately ambiguous.
  for (size_t i = 1; i+1 < raw.size(); ++i) {
    if (!std::strchr("aiueo", raw[i-1]) || !std::strchr("bcdfghjkmprstwyz", raw[i])) continue;
    if (std::strchr("bcdfghjkpstz", raw[i]) && raw[i] != raw[i+1]) check(geminate, i, std::string(1,raw[i]));
    check(nasal, i, "n'");
  }
  if ((geminate.ambiguous || geminate.reading.empty()) && (nasal.ambiguous || nasal.reading.empty())) return;
  std::string reading;
  if (!ConvertRomajiChecked(raw, &reading)) return;
  comparison.minimum_gain = -5.0f;
  comparison.incumbent = {};
  comparison.include_kana = true;
  size_t position = 2;  // Keep the user's original first and second choices.
  for (const auto* repair : {&geminate, &nasal}) {
    if (repair->ambiguous || repair->reading.empty() || repair->reading == reading || !comparison.remaining) continue;
    const auto words = Words(repair->reading, true);
    if (std::find(words.begin(),words.end(),rows.front().output_text) != words.end()) continue;
    const auto offered = AzookeyAlternativeReading(reading, repair->reading, input.left_context, input.right_context, &comparison);
    for (const auto& text : offered) {
      if (std::find(words.begin(), words.end(), text) == words.end()) continue;
      if (PlaceRecoveredCandidate(input, rows, text, repair->reading, position)) ++position;
      break;
    }
  }
}
bool UserDictionary::ApplyEnglish(const DecodeInput& input, std::vector<Candidate>* candidates) const {
  if (input.field!="prose" || input.candidate_limit<=0 || input.raw_text.size()>512) return false;
  auto raw=input.raw_text, lower=raw;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char ch){return static_cast<char>(std::tolower(ch));});
  bool ok=false;auto reading=ConvertRomaji(raw,&ok);
  // animeやsushiのように日本語の読みでもある単語は、かな漢字変換を優先する。
  if(ok && Flags(reading)) return false;
  std::string output;
  bool exact=(Flags(lower) & English)!=0;
  if(exact) output=raw;
  else {
    for(size_t end=std::min<size_t>(30,raw.size());end>=4;--end) {
      if(end==raw.size() || !(Flags(std::string_view(lower).substr(0,end)) & English)) continue;
      bool prefix_ok=false;
      const auto prefix_reading=ConvertRomaji(raw.substr(0,end),&prefix_ok);
      if(prefix_ok && Flags(prefix_reading)) continue;
      auto suffix=raw.substr(end);
      bool particle=false;
      for(auto prefix: {"wo","ga","no","ni","ha","de","to","mo","he"})
        if(suffix.compare(0,std::strlen(prefix),prefix)==0) particle=true;
      bool suffix_ok=false;ConvertRomaji(suffix,&suffix_ok);
      if(!particle || !suffix_ok) continue;
      auto request=input;request.raw_text=suffix;
      request.left_context+=raw.substr(0,end);
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
namespace {
bool HasLiteralVersionPrefix(const DecodeInput& input, const std::vector<Candidate>& candidates) {
  const auto& raw = input.raw_text;
  const auto end = raw.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
  if (candidates.empty() || !end || end == std::string::npos) return false;
  const auto number = end + (raw[end] == '.');
  if (number >= raw.size() || raw[number] < '0' || raw[number] > '9') return false;
  // Retain a versioned Latin stem already preserved by the decoder. A kana
  // prefix copied from its first few keystrokes is not a word boundary.
  return candidates.front().reading_text.compare(0, end, raw, 0, end) == 0 &&
         !ConvertRomajiChecked(raw.substr(0, end), nullptr);
}
}

void UserDictionary::ApplyExact(const DecodeInput& input, std::vector<Candidate>* candidates,
                                bool public_dictionary) const {
  if (!count_ || input.raw_text.size() > 512 || input.candidate_limit <= 0 ||
      input.field == "code" || input.field == "identifier" || input.field == "password" ||
      input.field == "url" || input.field == "path" || input.field == "email") return;
  bool ok = false;
  auto reading = ConvertRomaji(input.raw_text, &ok);
  if (!ok && public_dictionary && !HasLiteralVersionPrefix(input, *candidates)) {
    // 英語が続く場合も文頭の登録語を探し、残った入力を変換する。
    for (size_t end = reading.size(); end >= 3; --end) {
      if (end < reading.size() &&
          (static_cast<unsigned char>(reading[end]) & 0xc0) == 0x80) continue;
      auto key = std::string_view(reading).substr(0, end);
      const auto flags = Flags(key);
      if (!flags || ((flags & Supplemental) && !(flags & Priority))) continue;
      size_t raw_end = 0;
      for (size_t i = 1; i <= input.raw_text.size(); ++i) {
        bool prefix_ok = false;
        auto converted = ConvertRomaji(input.raw_text.substr(0, i), &prefix_ok);
        if (prefix_ok && converted == key) { raw_end = i; break; }
      }
      if (!raw_end || raw_end >= input.raw_text.size()) continue;
      DecodeInput suffix_input = input;
      suffix_input.raw_text = input.raw_text.substr(raw_end);
      const auto words = Words(key);
      suffix_input.left_context += words.front();
      auto suffix_candidates = Decode(suffix_input);
      if (suffix_candidates.empty() || suffix_candidates.front().is_raw) continue;
      std::vector<Candidate> combined;
      std::set<std::string> seen;
      for (const auto& word : words) {
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
  std::string_view key(reading);
  auto flags = Flags(key);
  const bool exact = flags != 0;
  std::string prefix, suffix;
  if (!exact) {
    // 最初に見つかる最長の登録語を使い、前後の文字列を残した候補を作る。
    for (size_t start=0; start<reading.size() && !flags; ++start) {
      if ((static_cast<unsigned char>(reading[start]) & 0xC0) == 0x80) continue;
      const auto tail = std::string_view(reading).substr(start);
      const size_t bound = public_index_ ? public_index_->MaxEmbeddedLength(tail) : tail.size();
      for (size_t end=start+bound; end>start; --end) {
        if (end<reading.size() && (static_cast<unsigned char>(reading[end]) & 0xC0) == 0x80) continue;
        // 1文字の読みは単語の途中に適用しない。
          if (end-start<6) continue;
          auto part = std::string_view(reading).substr(start,end-start);
          const auto found = Flags(part);
          // 短い一般語を文中へ機械的に差し込むと、全文候補が同音の断片で埋まる。
          if (public_dictionary && end-start<12 && (found & Supplemental) && !(found & Priority)) continue;
          if (found) { flags=found; key=part; prefix=reading.substr(0,start); suffix=reading.substr(end); break; }
      }
    }
    if (!flags) return;
  }
  const auto words = Words(key);
  if (!exact) {
      auto surface=[&input](const std::string& kana, const std::string& left, const std::string& right) {
        if(kana.empty())return kana;
        auto list=AzookeyConvert(kana,1,left,right,input.phase!="typing"); return list.empty()?kana:list.front();
      };
      prefix=surface(prefix,input.left_context,{});
      suffix=surface(suffix,input.left_context+prefix+words.front(),input.right_context);
  }
  std::vector<Candidate> combined; std::set<std::string> seen;
  bool first_is_arrow = false;
  if (exact && !words.empty()) {
    const auto& first = words.front();
    if (first.size() >= 3) {
      const auto a = static_cast<unsigned char>(first[0]);
      const auto b = static_cast<unsigned char>(first[1]);
      const auto c = static_cast<unsigned char>(first[2]);
      const auto codepoint = ((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f);
      first_is_arrow = a == 0xe2 && 0x2190 <= codepoint && codepoint <= 0x21ff;
    }
  }
  const bool preserve_primary = public_dictionary && exact &&
      !first_is_arrow && ((flags & SymbolOnly) || ((flags & Supplemental) && !(flags & Priority)));
  if ((!exact || preserve_primary) && !candidates->empty()) {
    combined.push_back(candidates->front()); seen.insert(candidates->front().output_text);
  }
  for (const auto& word : words) {
    Candidate c; c.output_text = prefix + word + suffix; c.reading_text = reading;
    if (seen.insert(c.output_text).second) combined.push_back(c);
    if (!exact && combined.size()>=4) break;
  }
  if (public_dictionary && !exact && !candidates->empty() && combined.size() > 1) {
    // Independently decoding the text around a partial public entry can
    // introduce a wrong word boundary. Prefer complete-phrase alternatives
    // if none of the inserted spellings also occurred in the original list.
    const bool supported = std::any_of(combined.begin() + 1, combined.end(), [&](const auto& word) {
      return std::any_of(candidates->begin(), candidates->end(), [&](const auto& row) {
        return row.output_text == word.output_text;
      });
    });
    if (!supported) {
      size_t position = 1;
      for (const auto& row : *candidates) {
        if (row.is_raw || row.output_text == input.raw_text || !seen.insert(row.output_text).second) continue;
        combined.insert(combined.begin() + position, row);
        if (++position == 3) break;
      }
    }
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
  if (input.field != "prose" || input.candidate_limit < 2 ||
      (public_index_ ? !public_index_->has_supplemental() : supplemental_readings_.empty())) return;
  auto raw = input.raw_text;
  std::string punctuation;
  if (!raw.empty() && raw.back() == '.') { raw.pop_back(); punctuation = u8"。"; }
  if (raw.size() < 6 || raw.size() > 64 ||
      raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz-'") != std::string::npos) return;
  bool original_ok = false;
  const auto original = ConvertRomaji(raw, &original_ok);
  if (Flags(original)) return; // valid dictionary words are never corrected

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
      if (!(Flags(prefix) & Supplemental) || original.compare(0,end,prefix)==0) continue;
      auto suffix=reading.substr(end);
      if (!suffix.empty()) {
        if (!StartsGrammaticalSuffix(suffix) || end*2<reading.size()) continue;
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
  if (!original_ok && matches.front().suffix.empty() && matches.front().repair.cost<=12 &&
      (matches.size()==1 || matches[1].repair.cost>matches[0].repair.cost) && !candidates->empty()) {
    const auto& surface=candidates->front().output_text;
    const bool latin=std::any_of(surface.begin(),surface.end(),[](unsigned char c){return c>='a'&&c<='z';});
    const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,surface.data(),static_cast<int>(surface.size()),nullptr,0);
    std::wstring wide(static_cast<size_t>(length),L'\0');
    if(length) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,surface.data(),static_cast<int>(surface.size()),wide.data(),length);
    const bool japanese=std::any_of(wide.begin(),wide.end(),[](wchar_t c){
      return (c>=0x3041&&c<=0x3096)||(c>=0x30a1&&c<=0x30fa)||(c>=0x3400&&c<=0x9fff)||
          (c>=0xff66&&c<=0xff6f)||(c>=0xff71&&c<=0xff9d);
    });
    // 壊れたローマ字の一部だけを変換するより、一意に復元できる辞書語を優先する。
    if (latin && japanese) promote=true;
  }
  // 元の文頭が辞書に一致する場合は、その読みを優先する。
  for (size_t end=original.size(); promote && end>=12; --end) {
    if (Flags(std::string_view(original).substr(0,end)) && end+6>=matches.front().reading.size()) {
      const auto suffix=original.substr(end);
      for(auto particle : {u8"を",u8"に",u8"が",u8"は",u8"で",u8"と",u8"も",u8"の",u8"へ"})
        if(suffix.compare(0,std::strlen(particle),particle)==0) promote=false;
    }
  }
  if (!candidates->empty() && candidates->front().output_text==input.raw_text) promote=false;
  // エンジンが既に辞書にある仮名語へ補正した場合、別の機械的な補正で上書きしない。
  if (!candidates->empty() && Flags(candidates->front().output_text)) promote=false;
  std::vector<Candidate> combined;
  std::set<std::string> seen;
  auto append=[&](const Candidate& c) { if(seen.insert(c.output_text).second) combined.push_back(c); };
  if (!promote && !candidates->empty()) append(candidates->front());
  for (size_t i=0; i<matches.size() && i<3; ++i) {
    const auto& match=matches[i];
    const auto words=Words(match.reading,true);
    auto suffix=match.suffix;
    if (!suffix.empty()) {
      auto converted=AzookeyConvert(suffix,1,input.left_context+words.front(),input.right_context,input.phase!="typing");
      if (!converted.empty()) suffix=converted.front();
    }
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
