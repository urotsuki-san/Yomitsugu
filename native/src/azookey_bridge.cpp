#include <string_view>
#include "azookey_bridge.h"
#include "cached_surfaces.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <list>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include "nlohmann/json.hpp"

namespace ime {
namespace {
using InitializeFn = int (*)(const char*, const char*);
using ConvertFn = const char* (*)(const char*, int);
using ContextConvertFn = const char* (*)(const char*, const char*, const char*, int, int);
using FreeFn = void (*)(const char*);
using BoolFn = void (*)(bool);
using IntFn = void (*)(int);
using PathFn = void (*)(const char*);
using StatusFn = const char* (*)();
std::mutex mutex;
HMODULE library = nullptr;
ConvertFn convert = nullptr;
ContextConvertFn convert_context = nullptr;
FreeFn free_string = nullptr;
BoolFn set_enabled = nullptr, set_gpu = nullptr;
IntFn set_limit = nullptr;
PathFn set_path = nullptr;
StatusFn get_status = nullptr;
ULONGLONG retry_at = 0;
std::string load_error;
ime::ConversionModel startup_model = ime::ConversionModel::Standard;
bool model_enabled = false, runtime_enabled = false, dictionary_preview = false;
thread_local AzookeyPartCapture* part_capture=nullptr;
struct CacheEntry { CachedSurfaces values; std::list<std::string>::iterator age; std::uint32_t bytes; float first_score; };
// Recency-list nodes own their keys; splice preserves the string addresses.
std::unordered_map<std::string_view, CacheEntry> cache;
std::list<std::string> ages;
size_t cache_bytes = 0;
bool cache_under_pressure = false;
size_t CacheByteLimit() { return cache_under_pressure ? 64 * 1024 : 8 * 1024 * 1024; }
void TrimCache(size_t incoming_entries, size_t incoming_bytes) {
  const size_t entries = cache_under_pressure ? 16 : 512;
  while (!ages.empty() && (cache.size() + incoming_entries > entries ||
                           cache_bytes + incoming_bytes > CacheByteLimit())) {
    const auto old = cache.find(ages.back());
    cache_bytes -= old->second.bytes;
    cache.erase(old);
    ages.pop_back();
  }
}
void ClearCache() { cache.clear(); ages.clear(); cache_bytes = 0; }
std::filesystem::path ModuleDirectory() {
  HMODULE module = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module);
  std::wstring path(32768, L'\0');
  DWORD n = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
  if (!n || n >= path.size()) return {};
  path.resize(n);
  return std::filesystem::path(path).parent_path();
}
std::string Take(const char* value) {
  if (!value) return {};
  std::string result(value);
  free_string(value);
  return result;
}
std::filesystem::path model_directory;
void ConfigureModel() {
  const bool compact = startup_model == ConversionModel::Compact || startup_model == ConversionModel::CompactExtendedSearch;
  const auto path = model_directory / L"models" / (compact ? L"zenz-v3.2-xsmall-Q5_K_M.gguf" : L"ggml-model-Q5_K_M.gguf");
  if (set_enabled && runtime_enabled) set_enabled(false);
  runtime_enabled = model_enabled = false;
  if (set_limit) set_limit(1);
  if (startup_model != ConversionModel::DictionaryOnly && set_path && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
    set_path(path.u8string().c_str());
    model_enabled = set_enabled != nullptr;
  }
  ClearCache();
}
bool Load() {
  if (library) return true;
  if (GetTickCount64() < retry_at) return false;
  retry_at = GetTickCount64() + 5000;
  auto root = ModuleDirectory();
  for (const auto& dir : {root / L"engine", root}) {
    auto file = dir / L"azookey-engine.dll";
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
    HMODULE h = LoadLibraryExW(file.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!h) { load_error = "dependency_load_failed"; continue; }
    auto init = reinterpret_cast<InitializeFn>(GetProcAddress(h, "Initialize"));
    auto cv = reinterpret_cast<ConvertFn>(GetProcAddress(h, "ConvertText"));
    auto fr = reinterpret_cast<FreeFn>(GetProcAddress(h, "FreeString"));
    if (!init || !cv || !fr) { FreeLibrary(h); load_error = "missing_exports"; continue; }
    if (!init(nullptr, nullptr)) { FreeLibrary(h); load_error = "initialize_failed"; continue; }
    library = h; convert = cv; free_string = fr;
    convert_context = reinterpret_cast<ContextConvertFn>(GetProcAddress(h, "ConvertTextWithContext"));
    set_enabled = reinterpret_cast<BoolFn>(GetProcAddress(h, "SetZenzaiEnabled"));
    set_gpu = reinterpret_cast<BoolFn>(GetProcAddress(h, "SetZenzaiUseGpu"));
    set_limit = reinterpret_cast<IntFn>(GetProcAddress(h, "SetZenzaiInferenceLimit"));
    set_path = reinterpret_cast<PathFn>(GetProcAddress(h, "SetZenzaiWeightPath"));
    get_status = reinterpret_cast<StatusFn>(GetProcAddress(h, "GetZenzaiStatus"));
    model_directory = dir;
    if (set_gpu) set_gpu(false);
    ConfigureModel();
    load_error.clear();
    return true;
  }
  if (load_error.empty()) load_error = "engine_not_installed";
  return false;
}
std::vector<size_t> ScalarOffsets(const std::string& s) {
  std::vector<size_t> result;
  for (size_t i = 0; i < s.size(); ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xc0) != 0x80) result.push_back(i);
  result.push_back(s.size());
  return result;
}
// The bundled Swift converter uses its refine flag only for readings of 4–12
// Characters. Restrict cross-phase reuse to precomposed Japanese characters,
// where the scalar count agrees with Swift's extended grapheme count.
bool SameDictionarySearchAcrossPhases(const std::string& reading) {
  if (reading.empty() || (reading.size()>=4*3 && reading.size()<=12*3) || reading.size()%3) return false;
  for (size_t i=0;i<reading.size();i+=3) {
    if ((static_cast<unsigned char>(reading[i+2]) & 0xc0) != 0x80) return false;
    std::string_view ch(reading.data()+i,3);
    if (!(ch>=u8"ぁ" && ch<=u8"ゖ") && !(ch>=u8"ァ" && ch<=u8"ヺ") &&
        ch!=u8"ー" && ch!=u8"、" && ch!=u8"。" && ch!=u8"・") return false;
  }
  return true;
}
// The bridge returns detailed word parts for every suggestion. Most lookups
// need just two top-level fields. Validate the whole reply without building
// objects and arrays for all the parts.
class CandidateReader : public nlohmann::json_sax<nlohmann::json> {
 public:
  std::vector<AzookeyCandidate> rows;
  bool start_array(size_t) override { ++depth_; return true; }
  bool end_array() override { --depth_; return true; }
  bool start_object(size_t) override {
    if (depth_==0) return false;
    if (depth_==1) { row_={}; inside_row_=rows.size()<256; }
    ++depth_; return true;
  }
  bool end_object() override {
    if (--depth_==1 && inside_row_) {
      if (row_.corresponding_count && !row_.text.empty()) rows.push_back(std::move(row_));
      inside_row_=false;
    }
    return true;
  }
  bool key(string_t& key) override {
    if (!inside_row_ || depth_!=2) return true;
    field_=Field::other;
    if (key=="text") { row_.text.clear(); field_=Field::text; }
    if (key=="score") field_=Field::score;
    if (key=="correspondingCount") { row_.corresponding_count=0; field_=Field::count; }
    return true;
  }
  bool string(string_t& value) override {
    if (inside_row_ && depth_==2 && field_==Field::text && value.size()<=65536 &&
        value.find('\0')==std::string::npos) row_.text=value;
    return true;
  }
  bool number_unsigned(number_unsigned_t value) override {
    if (inside_row_ && depth_==2 && field_==Field::score) row_.score=static_cast<float>(value);
    if (inside_row_ && depth_==2 && field_==Field::count && value>0 && value<=65536)
      row_.corresponding_count=static_cast<int>(value);
    return true;
  }
  bool number_integer(number_integer_t value) override {
    if (inside_row_ && depth_==2 && field_==Field::score) row_.score=static_cast<float>(value);
    return value<=0 || number_unsigned(static_cast<number_unsigned_t>(value));
  }
  bool null() override { return true; }
  bool boolean(bool) override { return true; }
  bool number_float(number_float_t value,const string_t&) override {
    if (inside_row_ && depth_==2 && field_==Field::score) row_.score=static_cast<float>(value);
    return true;
  }
  bool binary(binary_t&) override { return true; }
  bool parse_error(size_t,const std::string&,const nlohmann::detail::exception&) override { return false; }
 private:
  enum class Field { other,text,count,score };
  int depth_=0;
  bool inside_row_=false;
  Field field_=Field::other;
  AzookeyCandidate row_;
};
}

std::vector<AzookeyCandidate> ParseAzookeyCandidates(const std::string& data) {
  CandidateReader reader;
  if (data.size()>1024*1024 || !nlohmann::json::sax_parse(data,&reader)) return {};
  return std::move(reader.rows);
}
void AzookeySelectStartupModel(ConversionModel model) {
  std::lock_guard<std::mutex> lock(mutex);
  if (startup_model == model) return;
  startup_model = model;
  if (library) ConfigureModel();
}
void AzookeySetCachePressure(bool low) {
  std::lock_guard<std::mutex> lock(mutex);
  cache_under_pressure = low;
  if (low) TrimCache(0, 0);
}
void AzookeySetDictionaryPreview(bool enabled) {
  std::lock_guard<std::mutex> lock(mutex);
  dictionary_preview = enabled;
}
bool AzookeyEnsureReady() { std::lock_guard<std::mutex> lock(mutex); return Load(); }
bool AzookeyAvailable() { std::lock_guard<std::mutex> lock(mutex); return library != nullptr; }
std::vector<std::string> AzookeyConvert(const std::string& reading, int limit,
                                     const std::string& left_context, const std::string& right_context, bool refine, bool shallow_search) {
  if (reading.empty() || limit <= 0 || reading.size() > 4096) return {};
  std::lock_guard<std::mutex> lock(mutex);
  if (!Load()) return {};
  auto bound = [](const std::string& text, bool tail) {
    const auto offsets = ScalarOffsets(text);
    const auto count = offsets.size()-1;
    if (count <= 128) return text;
    return tail ? text.substr(offsets[count-128]) : text.substr(0,offsets[128]);
  };
  const auto left = bound(left_context,true), right = bound(right_context,false);
  const bool use_model = model_enabled && !dictionary_preview;
  const bool extended = startup_model == ConversionModel::ExtendedSearch || startup_model == ConversionModel::CompactExtendedSearch;
  const bool deep = extended && refine && !shallow_search;
  const bool phase_independent = !use_model && convert_context && SameDictionarySearchAcrossPhases(reading);
  const bool same_dictionary = phase_independent && reading.size()>12*3;
  // Length prefixes keep arbitrary input bytes, including embedded NULs, distinct.
  std::string key;
  key.reserve(1 + 2 * sizeof(std::uint32_t) + reading.size() + left.size() + right.size());
  key.push_back(static_cast<char>((refine && !same_dictionary) | (use_model << 1) |
                                  ((deep && !same_dictionary) << 2)));
  for (const auto* part : {&reading, &left}) {
    const auto length = static_cast<std::uint32_t>(part->size());
    key.append(reinterpret_cast<const char*>(&length), sizeof(length));
    key.append(*part);
  }
  key.append(right);
  auto found = cache.find(key);
  if (found == cache.end()) {
    if (set_enabled && runtime_enabled != use_model) {
      set_enabled(use_model); runtime_enabled = use_model;
    }
    if (extended && set_limit) set_limit(deep ? 5 : 1);
    auto json = Take(convert_context ?
        convert_context(reading.c_str(), left.c_str(), right.c_str(), 0, refine ? 1 : 0) : convert(reading.c_str(), 0));
    auto items = ParseAzookeyCandidates(json);
    if (!use_model && refine && convert_context && part_capture && json.size() <= 65536 &&
        part_capture->Matches(reading,left,right)) part_capture->json=std::move(json);
    auto offsets = ScalarOffsets(reading);
    const auto count = offsets.size() - 1;
    std::stable_sort(items.begin(), items.end(), [count](const auto& a, const auto& b) {
      return (a.corresponding_count == count) > (b.corresponding_count == count);
    });
    const float first_score=!items.empty() && items.front().corresponding_count==count
        ? items.front().score : -std::numeric_limits<float>::infinity();
    if (!use_model && (refine || phase_independent) && part_capture && part_capture->Matches(reading,left,right))
      part_capture->first_score=first_score;
    std::vector<std::string> values;
    for (auto& item : items) {
      if (item.corresponding_count > count) continue;
      auto text = item.text + reading.substr(offsets[item.corresponding_count]);
      if (std::find(values.begin(), values.end(), text) != values.end()) continue;
      values.push_back(std::move(text));
      if (values.size() >= 32) break;
    }
    if (values.empty()) return {};
    CachedSurfaces packed(values);
    const auto bytes = key.capacity() + packed.bytes();
    if (bytes <= CacheByteLimit()) {
      TrimCache(1, bytes);
      ages.push_front(std::move(key));
      try {
        cache.emplace(ages.front(), CacheEntry{std::move(packed), ages.begin(), static_cast<std::uint32_t>(bytes), first_score});
      } catch (...) {
        ages.pop_front();
        throw;
      }
      cache_bytes += bytes;
    }
    if (values.size() > static_cast<size_t>(limit)) values.resize(limit);
    return values;
  }
  ages.splice(ages.begin(), ages, found->second.age);
  if (!use_model && (refine || phase_independent) && part_capture && part_capture->Matches(reading,left,right))
    part_capture->first_score=found->second.first_score;
  return found->second.values.read(static_cast<size_t>(limit));
}
AzookeyPartCapture::AzookeyPartCapture(const std::string& reading,const std::string& left,const std::string& right)
    : reading_(reading),left_(left),right_(right),previous_(part_capture) {part_capture=this;}
AzookeyPartCapture::~AzookeyPartCapture() {part_capture=previous_;}
bool AzookeyPartCapture::Matches(const std::string& reading,const std::string& left,const std::string& right) const {
  return reading_==reading && left_==left && right_==right;
}
std::string AzookeyDictionaryParts(const std::string& reading, const std::string& left,
                                   const std::string& right) {
  if (reading.empty() || reading.size() > 384 || left.size() > 512 || right.size() > 512) return {};
  if (part_capture && !part_capture->json.empty() && part_capture->Matches(reading,left,right))
    return std::move(part_capture->json);
  std::lock_guard<std::mutex> lock(mutex);
  if (!Load() || !convert_context || !set_enabled) return {};
  if (runtime_enabled) set_enabled(false);
  auto result = Take(convert_context(reading.c_str(), left.c_str(), right.c_str(), 0, 1));
  if (runtime_enabled) set_enabled(true);
  return result;
}
std::vector<std::string> AzookeyAlternativeReading(const std::string& reading, const std::string& alternative,
                                                  const std::string& left, const std::string& right,
                                                  AzookeyReadingComparison* state) {
  AzookeyReadingComparison temporary;
  auto& comparison = state ? *state : temporary;
  if (!comparison.remaining) return {};
  --comparison.remaining;
  comparison.first_gain = -std::numeric_limits<float>::infinity();
  const bool saved = comparison.sample && comparison.sample->original == reading && comparison.sample->alternate == alternative;
  if (saved) comparison.original_score = comparison.sample->original_score;
  if (std::isnan(comparison.original_score)) {
    comparison.original_score = part_capture && part_capture->Matches(reading,left,right) ? part_capture->first_score
        : -std::numeric_limits<float>::infinity();
    const auto n = ScalarOffsets(reading).size()-1;
    if (!std::isfinite(comparison.original_score)) {
      for (const auto& c : ParseAzookeyCandidates(AzookeyDictionaryParts(reading,left,right))) {
        if (c.corresponding_count == n) { comparison.original_score = c.score; break; }
      }
    }
  }
  auto first_score = comparison.original_score;
  const auto alternative_count = ScalarOffsets(alternative).size()-1;
  if (!std::isfinite(first_score)) return {};
  std::vector<AzookeyCandidate> fetched;
  if (!saved) fetched = ParseAzookeyCandidates(AzookeyDictionaryParts(alternative,left,right));
  auto* sample = comparison.sample;
  if (!saved && sample && sample->alternate.empty()) {
    size_t count = 0, bytes = 0;
    for (const auto& c : fetched) {
      if (c.corresponding_count != alternative_count) continue;
      ++count;
      bytes += c.text.size();
    }
    // Partial-conversion rows never participate in this score comparison.
    if (count && count <= 32 && bytes <= 4096) {
      sample->original = reading;
      sample->alternate = alternative;
      sample->original_score = comparison.original_score;
      sample->candidates.reserve(count);
      for (auto& c : fetched)
        if (c.corresponding_count == alternative_count) sample->candidates.push_back(std::move(c));
    }
  }
  const auto& alternatives = sample && sample->original == reading && sample->alternate == alternative ? sample->candidates : fetched;
  if (!comparison.incumbent.empty()) {
    // A pre-existing typo repair may have this reading already. Compare the
    // suggested spelling to its incumbent on the same dictionary score scale.
    // For an unreturned incumbent, use the last full candidate as an upper bound.
    float incumbent_score = -std::numeric_limits<float>::infinity();
    for (const auto& c : alternatives) {
      if (c.corresponding_count != alternative_count || !std::isfinite(c.score)) continue;
      incumbent_score = c.score;
      if (c.text == comparison.incumbent) break;
    }
    first_score = std::max(first_score, incumbent_score);
  }
  std::vector<std::string> result;
  for (const auto& c : alternatives) {
    if (c.corresponding_count!=alternative_count) continue;
    if (!std::isfinite(c.score) || c.score<=first_score+comparison.minimum_gain) break;
    if ((c.text != alternative || comparison.include_kana) && std::find(result.begin(), result.end(), c.text) == result.end()) {
      if (result.empty()) comparison.first_gain = c.score - first_score;
      result.push_back(c.text);
    }
    if (result.size()==2) break;
  }
  return result;
}
void AzookeySetZenzai(bool enabled, const std::string& path, int limit) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!Load()) return;
  ClearCache();
  if (set_path && !path.empty()) set_path(path.c_str());
  if (set_limit) set_limit(std::clamp(limit, 1, 5));
  if (set_gpu) set_gpu(false);
  if (set_enabled) { set_enabled(enabled); model_enabled = runtime_enabled = enabled; }
}
std::string AzookeyZenzaiStatus() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!Load()) return nlohmann::json({{"active",false},{"error",load_error}}).dump();
  return get_status ? Take(get_status()) : "{}";
}
bool AzookeySetUserDictionary(const std::string& json) {
  if (json.size() > 16 * 1024 * 1024) return false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!Load()) return false;
  using SetDictionaryFn = int (*)(const char*);
  auto set = reinterpret_cast<SetDictionaryFn>(GetProcAddress(library, "SetUserDictionary"));
  if (!set || !set(json.c_str())) return false;
  ClearCache(); return true;
}
}
