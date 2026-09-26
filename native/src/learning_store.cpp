#include "learning_store.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include "nlohmann/json.hpp"

namespace ime {
namespace {
using nlohmann::json;
constexpr size_t kMaximumEntries = 2048;
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
  const bool saved = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!saved) std::filesystem::remove(temporary, error);
  return saved;
}
json ReadJson(const std::filesystem::path& path, size_t maximum) {
  std::error_code error;
  const auto bytes = std::filesystem::file_size(path, error);
  if (error || bytes > maximum) return nullptr;
  std::ifstream stream(path, std::ios::binary);
  return json::parse(stream, nullptr, false);
}
}
LearningStore::LearningStore(std::filesystem::path directory)
    : directory_(std::move(directory)), history_(directory_ / L"learning.json"), settings_(directory_ / L"settings.json") {}
bool LearningStore::enabled() const {
  std::error_code error;
  if (!std::filesystem::exists(settings_, error)) return !error;
  const auto settings = ReadJson(settings_, 65536);
  return settings.is_object() && settings.contains("learning_enabled") &&
      settings["learning_enabled"].is_boolean() && settings["learning_enabled"].get<bool>();
}
bool LearningStore::SetEnabled(bool enabled) {
  HistoryLock lock; if (!lock) return false;
  auto settings = ReadJson(settings_, 65536);
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
void LearningStore::Load(bool force) {
  std::error_code error;
  auto stamp = std::filesystem::last_write_time(history_, error);
  if (error) { entries_.clear(); stamp_ = {}; file_id_ = file_size_ = 0; return; }
  HANDLE file = CreateFileW(history_.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  BY_HANDLE_FILE_INFORMATION info{};
  const bool identified = file != INVALID_HANDLE_VALUE && GetFileInformationByHandle(file, &info);
  if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
  const auto identity = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  const auto bytes = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
  // 保存時のファイル置換を識別し、更新日時が同じでも他プロセスの変更を読む。
  if (!force && identified && stamp == stamp_ && identity == file_id_ && bytes == file_size_) return;
  entries_.clear(); stamp_ = stamp; file_id_ = identity; file_size_ = bytes;
  const auto data = ReadJson(history_, 1024 * 1024);
  if (!data.is_object() || !data.contains("version") || data["version"] != 1 || !data.contains("entries") || !data["entries"].is_array()) return;
  try {
    for (const auto& value : data["entries"]) {
      if (!value.is_object() || !value.contains("count") || !value["count"].is_number_unsigned() ||
          !value.contains("used") || !value["used"].is_number_unsigned() ||
          value["count"].get<std::uint64_t>() > 100001) continue;
      Entry entry{value.at("key").get<std::string>(), value.at("text").get<std::string>(),
                  value.at("count").get<unsigned>(), value.at("used").get<std::uint64_t>()};
      if (ValidText(entry.key, 96) && ValidText(entry.text, 256) && entry.count && entry.used < 0x7fffffffffffffffull)
        entries_.push_back(std::move(entry));
      if (entries_.size() == kMaximumEntries) break;
    }
  } catch (...) { entries_.clear(); }
}
bool LearningStore::Save() {
  auto entries = json::array();
  for (const auto& entry : entries_) entries.push_back({{"key", entry.key}, {"text", entry.text}, {"count", entry.count}, {"used", entry.used}});
  return WriteJson(history_, {{"version", 1}, {"entries", entries}});
}
bool LearningStore::Clear() {
  HistoryLock lock; if (!lock) return false;
  entries_.clear();
  if (Save()) return true;
  Load(true); return false;
}
size_t LearningStore::size() { Load(); return entries_.size(); }
bool LearningStore::Record(const DecodeInput& input, const std::string& chosen) {
  const auto key = Key(input);
  if (key.empty() || !ValidText(chosen, 256) || chosen == input.raw_text ||
      std::none_of(chosen.begin(), chosen.end(), [](unsigned char ch) { return ch >= 128; })) return false;
  HistoryLock lock; if (!lock || !enabled()) return false;
  Load(true);
  auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.key == key && entry.text == chosen; });
  if (found == entries_.end()) { entries_.push_back({key, chosen, 0, 0}); found = entries_.end() - 1; }
  found->count = (std::min)(found->count, 100000u) + 1;
  FILETIME time{}; GetSystemTimeAsFileTime(&time);
  auto next_used = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
  // 同じ時計刻み内の選択や時計の巻き戻しでも、後の選択を優先する。
  for (const auto& entry : entries_) next_used = (std::max)(next_used, entry.used + 1);
  found->used = next_used;
  std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.used > b.used; });
  if (entries_.size() > kMaximumEntries) entries_.resize(kMaximumEntries);
  if (Save()) return true;
  Load(true); return false;
}
void LearningStore::Apply(const DecodeInput& input, std::vector<Candidate>* candidates) {
  const auto key = Key(input);
  if (!candidates || key.empty() || input.candidate_limit <= 0 || !enabled()) return;
  Load();
  const Entry* selected = nullptr;
  for (const auto& entry : entries_) {
    if (entry.key != key) continue;
    if (!selected || entry.count > selected->count || (entry.count == selected->count && entry.used > selected->used)) selected = &entry;
  }
  if (!selected) return;
  Candidate learned; learned.output_text = selected->text; learned.reading_text = key;
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
