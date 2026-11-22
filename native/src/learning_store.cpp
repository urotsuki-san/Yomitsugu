#include "learning_store.h"
#include "alternate_n.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <memory>
#include "nlohmann/json.hpp"

namespace ime {
namespace {
using nlohmann::json;
constexpr size_t kMaximumEntries = 2048;
std::string ContextKey(const DecodeInput& input) {
  if (input.left_context.empty() && input.right_context.empty()) return {};
  auto nearby=[](const std::string& text, bool tail) {
    std::vector<size_t> offsets;
    for(size_t i=0;i<text.size();++i) if((static_cast<unsigned char>(text[i])&0xc0)!=0x80) offsets.push_back(i);
    if(offsets.size()<=8) return text;
    return tail ? text.substr(offsets[offsets.size()-8]) : text.substr(0,offsets[8]);
  };
  // 周囲の文章は保存せず、直前・直後8文字から照合用の値を作る。
  const auto context=nearby(input.left_context,true)+std::string(1,'\0')+nearby(input.right_context,false);
  std::uint64_t hash=14695981039346656037ull;
  for(unsigned char ch:context) { hash^=ch; hash*=1099511628211ull; }
  std::string result(16,'0');
  for(int i=15;i>=0;--i) { result[i]="0123456789abcdef"[hash&15];hash>>=4; }
  return result;
}
std::string EnglishChoiceKey(const DecodeInput& input) {
  if (input.field != "prose" || input.raw_text.size() < 2 || input.raw_text.size() > 32) return {};
  std::string key = "en:";
  for (unsigned char ch : input.raw_text) {
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
    if (ch < 'a' || ch > 'z') return {};
    key += static_cast<char>(ch);
  }
  return key;
}
bool ValidText(const std::string& text, size_t maximum) {
  return !text.empty() && text.size() <= maximum && text.find('\0') == std::string::npos &&
      text.find_first_of("\r\n\t") == std::string::npos &&
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) > 0;
}
class HistoryLock {
 public:
  HistoryLock() {
    handle_ = CreateMutexW(nullptr, FALSE, L"Local\\YomitsuguLearningHistory");
    if (handle_) {
      const auto result = WaitForSingleObject(handle_, 1000);
      locked_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
  }
  ~HistoryLock() { if (locked_) ReleaseMutex(handle_); if (handle_) CloseHandle(handle_); }
  explicit operator bool() const { return locked_; }
 private:
  HANDLE handle_ = nullptr;
  bool locked_ = false;
};
bool ReplaceSnapshot(const std::filesystem::path& temporary, const std::filesystem::path& path) {
  if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  const DWORD reason = GetLastError();
  if (reason != ERROR_ACCESS_DENIED && reason != ERROR_SHARING_VIOLATION) return false;

  // Classic rename cannot replace an open destination, even with FILE_SHARE_DELETE.
  // POSIX rename preserves existing snapshots and honors readers that prohibit deletion.
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  const auto& name = absolute.native();
  if (error || name.size() > 32767) return false;
  const DWORD name_bytes = static_cast<DWORD>(name.size() * sizeof(wchar_t));
  const DWORD bytes = sizeof(FILE_RENAME_INFO) + name_bytes;
  auto storage = std::make_unique<unsigned char[]>(bytes);
  auto* info = new (storage.get()) FILE_RENAME_INFO{};
  info->Flags = FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
  info->FileNameLength = name_bytes;
  std::memcpy(info->FileName, name.data(), name_bytes);
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  const bool saved = FlushFileBuffers(file) && SetFileInformationByHandle(file, FileRenameInfoEx, info, bytes);
  CloseHandle(file);
  return saved;
}
bool WriteJson(const std::filesystem::path& path, const json& data) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;
  auto temporary = path;
  temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) return false;
    stream << data.dump() << '\n'; stream.flush();
    if (!stream) { stream.close(); std::filesystem::remove(temporary, error); return false; }
  }
  const bool saved = ReplaceSnapshot(temporary, path);
  if (!saved) std::filesystem::remove(temporary, error);
  return saved;
}
// Buffer a bounded snapshot without allocating a second copy of the JSON.
class SnapshotBuffer : public std::streambuf {
 public:
  SnapshotBuffer(HANDLE file, DWORD bytes) : file_(file), remaining_(bytes) {}
  bool failed() const { return failed_; }
 protected:
  int_type underflow() override {
    if (!remaining_) return traits_type::eof();
    DWORD read = 0;
    const DWORD count = (std::min)(remaining_, static_cast<DWORD>(sizeof(buffer_)));
    if (!ReadFile(file_, buffer_, count, &read, nullptr) || !read) {
      failed_ = true;
      return traits_type::eof();
    }
    remaining_ -= read;
    setg(buffer_, buffer_, buffer_ + read);
    return traits_type::to_int_type(*gptr());
  }
 private:
  HANDLE file_;
  DWORD remaining_;
  bool failed_ = false;
  char buffer_[4096];
};

class JsonSnapshot {
 public:
  explicit JsonSnapshot(const std::filesystem::path& path)
      : file_(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
    if (file_ == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file_, &info_))
      error_ = GetLastError();
  }
  ~JsonSnapshot() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
  JsonSnapshot(const JsonSnapshot&) = delete;
  JsonSnapshot& operator=(const JsonSnapshot&) = delete;
  bool ready() const { return error_ == ERROR_SUCCESS; }
  bool missing() const { return error_ == ERROR_PATH_NOT_FOUND || error_ == ERROR_FILE_NOT_FOUND; }
  std::uint64_t identity() const { return Combine(info_.nFileIndexHigh, info_.nFileIndexLow); }
  std::uint64_t size() const { return Combine(info_.nFileSizeHigh, info_.nFileSizeLow); }
  std::uint64_t stamp() const {
    return Combine(info_.ftLastWriteTime.dwHighDateTime, info_.ftLastWriteTime.dwLowDateTime);
  }
  bool Read(DWORD maximum, json& value) const {
    if (!ready()) return missing();
    if (size() > maximum) return true;
    SnapshotBuffer buffer(file_, static_cast<DWORD>(size()));
    std::istream stream(&buffer);
    value = json::parse(stream, nullptr, false);
    return !buffer.failed();
  }
 private:
  static std::uint64_t Combine(DWORD high, DWORD low) {
    return (static_cast<std::uint64_t>(high) << 32) | low;
  }
  HANDLE file_;
  BY_HANDLE_FILE_INFORMATION info_{};
  DWORD error_ = ERROR_SUCCESS;
};
}
LearningStore::LearningStore(std::filesystem::path directory)
    : directory_(std::move(directory)), history_(directory_ / L"learning.json"), settings_(directory_ / L"settings.json") {}
bool LearningStore::enabled() const {
  JsonSnapshot snapshot(settings_);
  if (snapshot.missing()) return true;
  json settings;
  if (!snapshot.Read(65536, settings)) return false;
  return settings.is_object() && settings.contains("learning_enabled") &&
      settings["learning_enabled"].is_boolean() && settings["learning_enabled"].get<bool>();
}
ConversionModel LearningStore::model() const {
  json settings;
  if (!JsonSnapshot(settings_).Read(65536, settings)) return ConversionModel::Automatic;
  if (settings.is_object() && settings.contains("conversion_model")) {
    const auto& model = settings["conversion_model"];
    if (model == "automatic") return ConversionModel::Automatic;
    if (model == "standard") return ConversionModel::Standard;
    if (model == "compact") return ConversionModel::Compact;
    if (model == "dictionary") return ConversionModel::DictionaryOnly;
    if (model == "extended") return ConversionModel::ExtendedSearch;
    if (model == "compact_extended") return ConversionModel::CompactExtendedSearch;
    if (model == "hybrid_low_memory") return ConversionModel::HybridLowMemory;
    if (model == "hybrid_full") return ConversionModel::HybridFull;
    if (model == "hybrid_dictionary_verify") return ConversionModel::HybridDictionaryVerify;
  }
  return ConversionModel::Automatic;
}
bool LearningStore::SetModel(ConversionModel model) {
  HistoryLock lock;
  if (!lock) return false;
  json settings;
  if (!JsonSnapshot(settings_).Read(65536, settings)) return false;
  if (!settings.is_object()) settings = json::object();
  if (!settings.contains("learning_enabled")) settings["learning_enabled"] = true;
  settings["conversion_model"] = model == ConversionModel::Automatic ? "automatic" :
                                model == ConversionModel::Compact ? "compact" :
                                model == ConversionModel::DictionaryOnly ? "dictionary" :
                                model == ConversionModel::CompactExtendedSearch ? "compact_extended" :
                                model == ConversionModel::HybridLowMemory ? "hybrid_low_memory" :
                                model == ConversionModel::HybridFull ? "hybrid_full" :
                                model == ConversionModel::HybridDictionaryVerify ? "hybrid_dictionary_verify" :
                                model == ConversionModel::ExtendedSearch ? "extended" : "standard";
  return WriteJson(settings_, settings);
}
bool LearningStore::SetEnabled(bool enabled) {
  HistoryLock lock; if (!lock) return false;
  json settings;
  if (!JsonSnapshot(settings_).Read(65536, settings)) return false;
  if (!settings.is_object()) settings = json::object();
  settings["learning_enabled"] = enabled;
  return WriteJson(settings_, settings);
}
std::string LearningStore::Key(const DecodeInput& input) {
  if (input.field != "prose" || input.raw_text.size() < 2 || input.raw_text.size() > 64) return {};
  for (unsigned char ch : input.raw_text)
    if (ch < 128 && !std::isalpha(ch) && ch != '-' && ch != '\'') return {};
  bool ok = false;
  auto key = ConvertRomaji(input.raw_text, &ok);
  if (!ok || key.empty() || key.size() > 96) return {};
  return key;
}
bool LearningStore::Load(bool force) {
  JsonSnapshot snapshot(history_);
  if (!snapshot.ready()) {
    if (!snapshot.missing()) return false;
    entries_.clear(); stamp_ = file_id_ = file_size_ = 0;
    return true;
  }
  // Metadata and content identify the same file even when another process replaces it.
  if (!force && snapshot.stamp() == stamp_ && snapshot.identity() == file_id_ &&
      snapshot.size() == file_size_) return true;
  json data;
  if (!snapshot.Read(1024 * 1024, data)) return false;
  entries_.clear();
  stamp_ = snapshot.stamp(); file_id_ = snapshot.identity(); file_size_ = snapshot.size();
  if (!data.is_object() || !data.contains("version") || data["version"] != 1 || !data.contains("entries") || !data["entries"].is_array()) return true;
  try {
    for (const auto& value : data["entries"]) {
      if (!value.is_object() || !value.contains("count") || !value["count"].is_number_unsigned() ||
          !value.contains("used") || !value["used"].is_number_unsigned() ||
          value["count"].get<std::uint64_t>() > 100001) continue;
      Entry entry{value.at("key").get<std::string>(), value.at("text").get<std::string>(),
                  value.at("count").get<unsigned>(), 0, value.at("used").get<std::uint64_t>()};
      if (value.contains("alternate_n") && value["alternate_n"].is_number_unsigned()) {
        const auto offset = value["alternate_n"].get<std::uint64_t>();
        auto reading = entry.key;
        if (offset <= 96 && RestoreSyllabicN(reading, static_cast<unsigned>(offset)))
          entry.alternate_n = static_cast<std::uint8_t>(offset);
      }
      if (value.contains("selected")) {
        if (!value["selected"].is_number_unsigned()) continue;
        entry.selected = value["selected"].get<std::uint64_t>();
        if (entry.selected > entry.used) continue;
      }
      if (value.contains("context")) {
        if (!value["context"].is_string()) continue;
        entry.context=value["context"].get<std::string>();
        if (!entry.context.empty() && (entry.context.size()!=16 || entry.context.find_first_not_of("0123456789abcdef")!=std::string::npos)) continue;
      }
      if (value.contains("word_key") && value.contains("word_text") &&
          value["word_key"].is_string() && value["word_text"].is_string()) {
        auto key = value["word_key"].get<std::string>(), text = value["word_text"].get<std::string>();
        if (entry.selected && ValidText(key, 96) && ValidText(text, 256) &&
            entry.key.find(key) != std::string::npos && entry.text.find(text) != std::string::npos)
          entry.word = {std::move(key), std::move(text)};
      }
      if (ValidText(entry.key, 96) && ValidText(entry.text, 256) && entry.count && entry.used < 0x7fffffffffffffffull)
        entries_.push_back(std::move(entry));
      if (entries_.size() == kMaximumEntries) break;
    }
  } catch (...) { entries_.clear(); }
  return true;
}
bool LearningStore::Save() {
  auto entries = json::array();
  for (const auto& entry : entries_) {
    json row = {{"key",entry.key},{"text",entry.text},{"count",entry.count},{"used",entry.used},{"selected",entry.selected},{"context",entry.context}};
    if (entry.alternate_n) row["alternate_n"] = entry.alternate_n;
    if (!entry.word.first.empty()) { row["word_key"] = entry.word.first; row["word_text"] = entry.word.second; }
    entries.push_back(std::move(row));
  }
  return WriteJson(history_, {{"version", 1}, {"entries", entries}});
}
bool LearningStore::Clear() {
  HistoryLock lock; if (!lock) return false;
  if (!WriteJson(history_, {{"version", 1}, {"entries", json::array()}})) return false;
  entries_.clear(); stamp_ = file_id_ = file_size_ = 0;
  return true;
}
size_t LearningStore::size() { return Load() ? entries_.size() : 0; }
std::vector<LearningStore::WordChoice> LearningStore::ExplicitWordsIn(const DecodeInput& input, const std::string& reading) {
  std::vector<WordChoice> result;
  if (input.field != "prose" || input.phase == "typing" || reading.size() > 384) return result;
  if (!Load() || entries_.empty() || !enabled()) return result;
  const auto context = ContextKey(input);
  std::map<std::string,const Entry*> latest;
  for (const auto& entry : entries_) {
    if (entry.word.first.empty() || entry.context != context || reading.find(entry.word.first) == std::string::npos) continue;
    auto& match = latest[entry.word.first];
    if (!match || match->selected < entry.selected) match = &entry;
  }
  for (const auto& match : latest) result.push_back(match.second->word);
  return result;
}
bool LearningStore::Record(const DecodeInput& input, const std::string& chosen, bool explicit_selection, bool f10, const WordChoice& word, unsigned alternate_n) {
  const bool literal = f10 && explicit_selection && chosen == input.raw_text;
  const auto key = literal ? EnglishChoiceKey(input) : Key(input);
  if (key.empty() || !ValidText(chosen, 256)) return false;
  if (!literal && (chosen == input.raw_text ||
      std::none_of(chosen.begin(), chosen.end(), [](unsigned char ch) { return ch >= 128; }))) return false;
  const auto stored = literal ? key.substr(3) : chosen;
  HistoryLock lock; if (!lock || !enabled()) return false;
  if (!Load(true)) return false;
  const auto context = literal ? std::string{} : ContextKey(input);
  auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.key == key && entry.text == stored && entry.context == context; });
  if (found == entries_.end()) { entries_.push_back({key, stored, 0, 0, 0, 0, context}); found = entries_.end() - 1; }
  found->count = (std::min)(found->count, 100000u) + 1;
  FILETIME time{}; GetSystemTimeAsFileTime(&time);
  auto next_used = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  // 同じ時計刻み内の選択や時計の巻き戻しでも、後の選択を優先する。
  for (const auto& entry : entries_) next_used = (std::max)(next_used, entry.used + 1);
  found->used = next_used;
  if (explicit_selection) {
    found->selected = next_used;
    if (!literal && alternate_n && alternate_n <= 96 && input.raw_text.find("nn") != std::string::npos) {
      auto reading = key;
      if (RestoreSyllabicN(reading, alternate_n)) found->alternate_n = static_cast<std::uint8_t>(alternate_n);
    }
    if (!literal && ValidText(word.first, 96) && ValidText(word.second, 256) &&
        key.find(word.first) != std::string::npos && chosen.find(word.second) != std::string::npos)
      found->word = word;
  }
  std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.used > b.used; });
  if (entries_.size() > kMaximumEntries) entries_.resize(kMaximumEntries);
  if (Save()) return true;
  stamp_ = file_id_ = file_size_ = 0;
  Load(true); return false;
}
void LearningStore::Apply(const DecodeInput& input, std::vector<Candidate>* candidates) {
  const auto key = Key(input);
  const auto english = EnglishChoiceKey(input);
  if (!candidates || (key.empty() && english.empty()) || input.candidate_limit <= 0 || !enabled()) return;
  if (!Load()) return;
  const auto context=ContextKey(input);
  const Entry* selected = nullptr;
  for (const auto& entry : entries_) {
    const bool literal = !english.empty() && entry.key == english && entry.count >= 2;
    if (!literal && (entry.key != key || entry.context != context)) continue;
    // 手で選び直した候補を、過去の自動確定回数で押し下げない。
    if (!selected || entry.selected > selected->selected ||
        (entry.selected == selected->selected &&
         (entry.count > selected->count || (entry.count == selected->count && entry.used > selected->used)))) selected = &entry;
  }
  if (!selected) return;
  Candidate learned;
  learned.is_raw = !english.empty() && selected->key == english;
  learned.output_text = learned.is_raw ? input.raw_text : selected->text;
  learned.reading_text = key.empty() ? input.raw_text : key;
  if (!learned.is_raw && !RestoreSyllabicN(learned.reading_text, selected->alternate_n)) {
    const auto offered=std::find_if(candidates->begin(), candidates->end(), [&](const Candidate& c) {
      return !c.is_raw && c.output_text==learned.output_text && !c.reading_text.empty();
    });
    if (offered!=candidates->end()) learned.reading_text=offered->reading_text;
  }
  std::vector<Candidate> result{learned};
  for (const auto& candidate : *candidates) if (candidate.output_text != learned.output_text) result.push_back(candidate);
  const auto limit = static_cast<size_t>(input.candidate_limit);
  if (result.size() > limit) result.resize(limit);
  if (limit > 1 && std::none_of(result.begin(), result.end(), [&](const Candidate& candidate) { return candidate.output_text == input.raw_text; })) {
    if (result.size() == limit) result.pop_back();
    Candidate literal; literal.output_text = literal.reading_text = input.raw_text; literal.is_raw = true; result.push_back(literal);
  }
  *candidates = std::move(result);
}
}
