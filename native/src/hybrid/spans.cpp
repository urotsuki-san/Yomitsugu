#include "spans.h"
#include "azookey_bridge.h"
#include "iroha/unicode.h"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <string_view>

using iroha::Utf8ToUtf32;
using iroha::Utf32ToUtf8;

ReadingPaths::ReadingPaths(std::string reading, const ime::DecodeInput& request)
    : reading_(std::move(reading)), request_(request) {}

void ReadingPaths::Load() {
  if (loaded_) return;
  loaded_ = true;
  kana_ = Utf8ToUtf32(reading_);
  if (kana_.empty() || kana_.size() > 96) return;
  const auto raw = ime::AzookeyDictionaryParts(reading_, request_.left_context, request_.right_context);
  if (raw.size() > 1024 * 1024) return;
  auto json = nlohmann::json::parse(raw, nullptr, false);
  if (!json.is_array()) return;
  edges_.resize(kana_.size() + 1);
  for (size_t index = 0; index < std::min<size_t>(16, json.size()); ++index) {
    const auto& item = json[index];
    if (!item.is_object() || !item.contains("parts") || !item["parts"].is_array() ||
        item["parts"].empty() || item["parts"].size() > 96 || !item.contains("text") || !item["text"].is_string()) continue;
    size_t pos = 0;
    std::u32string full;
    std::vector<Edge> path;
    for (const auto& part : item["parts"]) {
      if (!part.is_object() || !part.contains("ruby") || !part.contains("word") ||
          !part["ruby"].is_string() || !part["word"].is_string()) break;
      auto ruby = Utf8ToUtf32(part["ruby"].get_ref<const std::string&>());
      for (auto& c : ruby) if (c >= U'ァ' && c <= U'ヶ') c -= 96;
      auto word = Utf8ToUtf32(part["word"].get_ref<const std::string&>());
      if (word.empty() || ruby.empty() || kana_.compare(pos, ruby.size(), ruby) != 0) break;
      pos += ruby.size();
      full += word;
      path.push_back({pos,std::move(word)});
    }
    if (pos != kana_.size() || path.size() != item["parts"].size() ||
        Utf32ToUtf8(full) != item["text"].get_ref<const std::string&>()) continue;
    if (first_.empty()) first_ = full;
    pos = 0;
    for (auto& edge : path) {
      auto& outgoing = edges_[pos];
      pos = edge.end;
      if (std::none_of(outgoing.begin(), outgoing.end(), [&](const Edge& e){return e.end == edge.end && e.text == edge.text;}))
        outgoing.push_back(std::move(edge));
    }
  }
}

void ReadingPaths::AddAlternateSyllabicN(std::vector<ime::Candidate>& rows) {
  const std::string& raw=request_.raw_text;
  if (rows.empty() || rows.front().is_raw || request_.candidate_limit<4 ||
      raw.size()>256 || raw.find("nn")==std::string::npos || reading_.empty() ||
      request_.field!="prose" || request_.phase!="end_of_phrase" || request_.dictionary_preview ||
      request_.shallow_search || !request_.right_context.empty()) return;

  std::string alternate_raw=raw;
  bool ambiguous=false;
  for (size_t i=0; i+2<raw.size(); ++i) {
    if (raw[i]!='n' || raw[i+1]!='n' || (i && raw[i-1]=='n') ||
        std::string_view("aiueo").find(raw[i+2])==std::string_view::npos) continue;
    // For a word with two such boundaries, both conventions are possible.
    // Avoid distinguishing them by launching multiple sentence lookups on each keystroke.
    if (ambiguous) return;
    ambiguous=true;
    alternate_raw[i+1]='\'';
  }
  std::string alternative;
  if (!ambiguous || !ime::ConvertRomajiChecked(alternate_raw,&alternative) ||
      alternative==reading_) return;
  const auto offered=ime::AzookeyAlternativeReading(reading_,alternative,request_.left_context,request_.right_context);
  if (offered.empty()) return;
  auto phonetic=Utf8ToUtf32(alternative);
  for (auto& c:phonetic) if (c>=U'ぁ' && c<=U'ゖ') c+=96;
  const auto katakana=Utf32ToUtf8(phonetic);
  size_t at=1;
  for (const auto& text : offered) {
    if (text==katakana || std::any_of(rows.begin(),rows.end(),[&](const auto& v){return v.output_text==text;})) continue;
    if (rows.size()>=static_cast<size_t>(request_.candidate_limit)) {
      auto drop=rows.end()-1;
      if ((drop->is_raw || drop->output_text==raw) && drop!=rows.begin()) --drop;
      if (drop==rows.begin()) break;
      rows.erase(drop);
    }
    rows.insert(rows.begin()+at++,ime::Candidate{text,alternative,false});
  }
}

ReadingPaths::Alignment ReadingPaths::Align(const std::u32string& text,const std::vector<size_t>& boundaries) {
  Load();
  Alignment m;
  if (edges_.empty() || edges_[0].empty() || text.empty() || text.size() > 128) return m;
  m.width = text.size() + 1;
  m.prefix.resize(edges_.size() * m.width);
  m.prefix.front() = 1;
  for (size_t r = 0; r < kana_.size(); ++r)
    for (size_t s = 0; s < text.size(); ++s) {
      if (!m.prefix[r * m.width + s]) continue;
      for (const auto& edge : edges_[r])
        if (std::none_of(boundaries.begin(),boundaries.end(),[&](size_t b){return r < b && b < edge.end;}) &&
            text.compare(s, edge.text.size(), edge.text) == 0)
          m.prefix[edge.end * m.width + s + edge.text.size()] = 1;
    }
  if (!m.complete()) return m;
  m.suffix.resize(m.prefix.size());
  m.suffix.back() = 1;
  for (size_t r = kana_.size(); r-- > 0;)
    for (size_t s = 0; s < text.size(); ++s) {
      if (!m.prefix[r * m.width + s]) continue;
      for (const auto& edge : edges_[r])
        if (std::none_of(boundaries.begin(),boundaries.end(),[&](size_t b){return r < b && b < edge.end;}) &&
            text.compare(s, edge.text.size(), edge.text) == 0 &&
            m.suffix[edge.end * m.width + s + edge.text.size()]) {
          m.suffix[r * m.width + s] = 1;
          break;
        }
    }
  return m;
}

std::optional<size_t> ReadingPaths::Alignment::Unique(size_t r) const {
  std::optional<size_t> offset;
  if (!complete()) return offset;
  for (size_t s = 0; s < width; ++s) {
    if (!prefix[r * width + s] || !suffix[r * width + s]) continue;
    if (offset) return {};
    offset = s;
  }
  return offset;
}

bool ReadingPaths::Supports(const std::string& surface) { return Align(Utf8ToUtf32(surface)).complete(); }

WordPreference ReadingPaths::ChangedWord(const std::string& previous, const std::string& selected) {
  if (previous.empty() || previous == selected) return {};
  const auto old = Utf8ToUtf32(previous), next = Utf8ToUtf32(selected);
  const auto before = Align(old), after = Align(next);
  if (!before.complete() || !after.complete()) return {};
  WordPreference choice;
  // Only learn a single dictionary word when the surrounding output is identical.
  for (size_t start = 0; start < kana_.size(); ++start) {
    const auto a = before.Unique(start), b = after.Unique(start);
    if (!a || !b || *a != *b || next.compare(0, *b, old, 0, *a)) continue;
    for (const auto& e : edges_[start]) {
      if (e.end - start < 2) continue;
      auto x = before.Unique(e.end), y = after.Unique(e.end);
      if (!x || !y || *b + e.text.size() != *y || next.compare(*b,e.text.size(),e.text) ||
          next.compare(*y,next.npos,old,*x,old.npos)) continue;
      WordPreference word{Utf32ToUtf8(kana_.substr(start,e.end-start)),Utf32ToUtf8(e.text)};
      if (!choice.first.empty() && choice != word) return {};
      choice = std::move(word);
    }
  }
  return choice;
}

void ReadingPaths::Protect(const std::vector<WordPreference>& words, std::vector<ime::Candidate>& rows) {
  if (words.empty() || rows.empty() || rows.front().is_raw) return;
  Load();
  if (edges_.empty()) return;
  struct Span {size_t start, end; std::u32string word;};
  std::vector<Span> spans;
  for (const auto& [key, value] : words) {
    auto part = Utf8ToUtf32(key);
    auto pos = kana_.find(part);
    if (part.size() < 2 || pos == kana_.npos || kana_.find(part,pos + 1) != kana_.npos) continue;
    const auto end = pos + part.size();
    if (pos == 0 && end == kana_.size()) continue; // Whole-input preferences are handled by the existing stores.
    bool duplicate = false;
    for (const auto& s : spans) {
      if (s.start == pos && s.end == end) {duplicate = true; break;} // caller places registrations first
      if (pos < s.end && s.start < end) return;
    }
    if (!duplicate) spans.push_back({pos,end,Utf8ToUtf32(value)});
  }
  if (spans.empty()) return;
  std::sort(spans.begin(), spans.end(), [](const Span& a,const Span& b){return a.start > b.start;});
  if (spans.size() > 3) return;
  std::vector<size_t> boundaries;
  for (const auto& span:spans) {boundaries.push_back(span.start); boundaries.push_back(span.end);}
  auto text = Utf8ToUtf32(rows.front().output_text);
  auto aligned = Align(text,boundaries);
  if (!aligned.complete()) {
    // A dictionary may give one entry for a surname plus honorific. Query the
    // exact registered reading to verify the surname independently on this request.
    for (const auto& span:spans) {
      ReadingPaths local(Utf32ToUtf8(kana_.substr(span.start,span.end-span.start)),request_);
      local.Load();
      if (local.edges_.empty()) continue;
      for (const auto& e:local.edges_[0])
        if (e.end==span.end-span.start) edges_[span.start].push_back({span.end,e.text});
    }
    aligned=Align(text,boundaries);
  }
  if (aligned.complete()) {
    bool valid = true;
    for (const auto& span : spans) {
      auto a = aligned.Unique(span.start), b = aligned.Unique(span.end);
      if (!a || !b || *a >= *b) {valid = false; break;}
      text.replace(*a, *b - *a, span.word);
    }
    if (!valid) return;
    const auto next = Utf32ToUtf8(text);
    if (next == rows.front().output_text) return;
    auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& c){return c.output_text==next;});
    if (found != rows.end()) std::rotate(rows.begin(),found,found+1);
    else {
      auto c = rows.front(); c.output_text = next; c.reading_text = reading_;
      const auto limit = static_cast<size_t>(request_.candidate_limit);
      if (rows.size() >= limit) {
        const bool raw_last = rows.back().is_raw && rows.size() > 1;
        rows.erase(rows.end() - (raw_last ? 2 : 1));
      }
      rows.insert(rows.begin(),std::move(c));
    }
    return;
  }
}

namespace {
bool Digit(char c) {return c >= '0' && c <= '9';}
std::vector<std::string_view> Numbers(std::string_view s) {
  std::vector<std::string_view> result;
  for (size_t i = 0; i < s.size();) {
    if (!Digit(s[i])) {++i; continue;}
    const size_t start = i++;
    while (i < s.size()) {
      if (Digit(s[i])) ++i;
      else if (i+1 < s.size() && std::string_view("/-.,:").find(s[i]) != s.npos && Digit(s[i+1])) i += 2;
      else break;
    }
    result.push_back(s.substr(start,i-start));
  }
  return result;
}
std::string CountedExpression(const ime::DecodeInput& q,const std::vector<std::string_view>& numbers) {
  if (q.raw_text.size()>96 || numbers.empty() || numbers.size()>4 ||
      Digit(q.raw_text.back()) ||
      q.raw_text.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789'-")!=std::string::npos) return {};
  std::string_view raw=q.raw_text;
  std::string result;
  auto pos=static_cast<size_t>(numbers.front().data()-raw.data());
  if (pos) {
    const auto prefix=q.raw_text.substr(0,pos);
    if (prefix=="dai") result=u8"第";
    else {
      std::string kana;
      if (!ime::ConvertRomajiChecked(prefix,&kana) || kana.size()<9) return {};
      ReadingPaths path(kana,q);
      const auto word=path.FirstSurface();
      if (word.size()<3 || !std::all_of(word.begin(),word.end(),[](char32_t c){return c>=U'ァ'&&c<=U'ー';})) return {};
      result=Utf32ToUtf8(word);
    }
  }
  for (size_t i=0;i<numbers.size();++i) {
    const auto number=numbers[i];
    if (number.find_first_not_of("0123456789")!=number.npos) return {};
    const auto end=static_cast<size_t>(number.data()-raw.data())+number.size();
    const auto next=i+1<numbers.size()?static_cast<size_t>(numbers[i+1].data()-raw.data()):raw.size();
    auto unit=raw.substr(end,next-end);
    if (i+1<numbers.size()) {
      if (unit.size()<3 || unit.substr(unit.size()-2)!="to") return {};
      unit.remove_suffix(2);
    }
    constexpr std::pair<std::string_view,std::string_view> units[]={
      {"dai",u8"台"},{"ko",u8"個"},{"nin",u8"人"},{"en",u8"円"},{"kai",u8"回"},{"hon",u8"本"}};
    auto found=std::find_if(std::begin(units),std::end(units),[&](const auto& e){return e.first==unit;});
    if (found==std::end(units)) return {};
    result+=number; result+=found->second;
    if (i+1<numbers.size()) result+=u8"と";
  }
  return result;
}
}
void ProtectNumericLiterals(const ime::DecodeInput& input, std::vector<ime::Candidate>& rows,bool allow_dictionary) {
  if (input.field != "prose" || input.phase == "typing" || input.raw_text.find_first_of("0123456789") == std::string::npos) return;
  const auto wanted = Numbers(input.raw_text);
  rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const auto& c){
    return c.output_text != input.raw_text && Numbers(c.output_text) != wanted;
  }), rows.end());
  if (rows.empty()) {
    ime::Candidate raw; raw.is_raw = true; raw.output_text = raw.reading_text = input.raw_text;
    rows.push_back(std::move(raw));
  }
  const auto& head=rows.front().output_text;
  if (allow_dictionary && input.candidate_limit>0 &&
      (head==input.raw_text || head.find_first_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos ||
       (input.left_context.empty() && input.right_context.empty() && head.size()>=3 && head.compare(head.size()-3,3,u8"第")==0))) {
    auto counted=CountedExpression(input,wanted);
    if (!counted.empty() && counted!=head) {
      ime::Candidate c; c.output_text=counted; c.reading_text=input.raw_text;
      rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const auto& v){return v.output_text==counted;}),rows.end());
      rows.insert(rows.begin(),std::move(c));
      if (rows.size()>static_cast<size_t>(input.candidate_limit)) {
        if (input.candidate_limit>1 && rows.back().is_raw) rows.erase(rows.end()-2);
        else rows.resize(input.candidate_limit);
      }
    }
  }
}
