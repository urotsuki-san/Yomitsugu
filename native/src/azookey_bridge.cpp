#include "azookey_bridge.h"
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
struct CacheEntry { std::vector<std::string> values; std::list<std::string>::iterator age; size_t bytes; };
std::unordered_map<std::string, CacheEntry> cache;
std::list<std::string> ages;
size_t cache_bytes = 0;
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
    auto model = dir / L"models" / L"ggml-model-Q5_K_M.gguf";
    if (set_gpu) set_gpu(false);
    if (set_limit) set_limit(1);
    if (set_path && GetFileAttributesW(model.c_str()) != INVALID_FILE_ATTRIBUTES) {
      set_path(model.u8string().c_str());
      if (set_enabled) set_enabled(true);
    }
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
}

std::vector<AzookeyCandidate> ParseAzookeyCandidates(const std::string& data) {
  std::vector<AzookeyCandidate> result;
  if (data.size() > 1024 * 1024) return result;
  auto json = nlohmann::json::parse(data, nullptr, false);
  if (!json.is_array()) return result;
  for (const auto& value : json) {
    if (!value.is_object() || !value.contains("text") || !value["text"].is_string() ||
        !value.contains("correspondingCount") || !value["correspondingCount"].is_number_integer()) continue;
    const auto count = value["correspondingCount"].get<int64_t>();
    auto text = value["text"].get<std::string>();
    if (count <= 0 || count > 65536 || text.empty() || text.size() > 65536 ||
        text.find('\0') != std::string::npos) continue;
    result.push_back({std::move(text), static_cast<int>(count)});
    if (result.size() == 256) break;
  }
  return result;
}
bool AzookeyEnsureReady() { std::lock_guard<std::mutex> lock(mutex); return Load(); }
bool AzookeyAvailable() { std::lock_guard<std::mutex> lock(mutex); return library != nullptr; }
std::vector<std::string> AzookeyConvert(const std::string& reading, int limit,
                                     const std::string& left_context, const std::string& right_context, bool refine) {
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
  const auto key = nlohmann::json::array({reading,left,right,refine}).dump();
  auto found = cache.find(key);
  if (found == cache.end()) {
    auto items = ParseAzookeyCandidates(Take(convert_context ?
        convert_context(reading.c_str(), left.c_str(), right.c_str(), 0, refine ? 1 : 0) : convert(reading.c_str(), 0)));
    auto offsets = ScalarOffsets(reading);
    const auto count = offsets.size() - 1;
    std::stable_sort(items.begin(), items.end(), [count](const auto& a, const auto& b) {
      return (a.corresponding_count == count) > (b.corresponding_count == count);
    });
    std::vector<std::string> values;
    size_t bytes = key.size();
    for (auto& item : items) {
      if (item.corresponding_count > count) continue;
      auto text = item.text + reading.substr(offsets[item.corresponding_count]);
      if (std::find(values.begin(), values.end(), text) != values.end()) continue;
      bytes += text.size(); values.push_back(std::move(text));
      if (values.size() >= 32) break;
    }
    if (values.empty()) return {};
    while (!ages.empty() && (cache.size() >= 512 || cache_bytes + bytes > 8 * 1024 * 1024)) {
      auto old = cache.find(ages.back()); cache_bytes -= old->second.bytes;
      cache.erase(old); ages.pop_back();
    }
    ages.push_front(key);
    found = cache.emplace(key, CacheEntry{std::move(values), ages.begin(), bytes}).first;
    cache_bytes += bytes;
  } else ages.splice(ages.begin(), ages, found->second.age);
  auto result = found->second.values;
  if (result.size() > static_cast<size_t>(limit)) result.resize(limit);
  return result;
}
void AzookeySetZenzai(bool enabled, const std::string& path, int limit) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!Load()) return;
  ClearCache();
  if (set_path && !path.empty()) set_path(path.c_str());
  if (set_limit) set_limit(std::clamp(limit, 1, 5));
  if (set_gpu) set_gpu(false);
  if (set_enabled) set_enabled(enabled);
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
