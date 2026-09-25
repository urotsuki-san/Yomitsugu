#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include "ime_engine.h"
#include "user_dictionary.h"
#include "nlohmann/json.hpp"

static bool Transfer(HANDLE pipe, void* data, DWORD size, bool write) {
  char* p = static_cast<char*>(data);
  while (size) {
    DWORD n = 0;
    BOOL ok = write ? WriteFile(pipe, p, size, &n, nullptr) : ReadFile(pipe, p, size, &n, nullptr);
    if (!ok || !n) return false;
    p += n; size -= n;
  }
  return true;
}
int wmain(int argc, wchar_t** argv) {
  if (argc != 4 || wcscmp(argv[1], L"--pipe")) return 2;
  HANDLE input = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
  HANDLE output = reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10));
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  using nlohmann::json;
  ime::UserDictionary dictionary;
  auto dictionary_path = ime::UserDictionaryPath();
  std::filesystem::file_time_type dictionary_time{};
  ime::UserDictionary public_dictionary;
  auto public_override = dictionary_path.parent_path() / L"public_dictionary.tsv";
  wchar_t executable_path[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, executable_path, MAX_PATH)) return 6;
  auto public_bundled = std::filesystem::path(executable_path).parent_path() / L"public_dictionary.tsv";
  std::filesystem::path public_loaded_path;
  std::filesystem::file_time_type public_time{};
  for (;;) {
    uint32_t size = 0;
    if (!Transfer(input, &size, sizeof(size), false)) return 0;
    if (!size || size > 16384) return 3;
    std::string data(size, '\0');
    if (!Transfer(input, data.data(), size, false)) return 0;
    try {
      auto j = json::parse(data);
      if (j.at("version").get<int>() != 1) return 4;
      ime::DecodeInput request;
      request.raw_text = j.at("raw").get<std::string>(); request.left_context = j.value("left", "");
      request.right_context = j.value("right", ""); request.field = j.value("field", "prose");
      request.candidate_limit = (std::clamp)(j.value("limit", 8), 1, 16);
      std::error_code ec;
      auto stamp = std::filesystem::last_write_time(dictionary_path, ec);
      if (!ec && stamp != dictionary_time) {
        std::string error;
        if (dictionary.Load(dictionary_path, &error)) {
          // Upstream dynamic lookup is linear; keep large imports in our indexed
          // dictionary and always offer explicit native candidates as well.
          ime::AzookeySetUserDictionary(dictionary.size() <= 1000 ? dictionary.json() : "[]");
          dictionary_time = stamp;
        }
      } else if (ec && dictionary.size() && !std::filesystem::exists(dictionary_path)) {
        dictionary = ime::UserDictionary(); ime::AzookeySetUserDictionary("[]"); dictionary_time = {};
      }
      auto public_path = std::filesystem::exists(public_override) ? public_override : public_bundled;
      std::error_code public_error;
      auto public_stamp = std::filesystem::last_write_time(public_path, public_error);
      if (!public_error && (public_path != public_loaded_path || public_stamp != public_time)) {
        std::string error;
        if (public_dictionary.Load(public_path, &error)) {
          public_loaded_path = public_path;
          public_time = public_stamp;
        }
      }
      auto result = ime::Decode(request);
      public_dictionary.Apply(request, &result, true);
      dictionary.Apply(request, &result);
      json response = json::array();
      for (const auto& c : result) response.push_back({{"text",c.output_text},{"reading",c.reading_text},{"raw",c.is_raw}});
      data = response.dump(); size = static_cast<uint32_t>(data.size());
      if (size > 1024*1024 || !Transfer(output, &size, sizeof(size), true) ||
          !Transfer(output, data.data(), size, true)) return 0;
    } catch (...) { return 5; }
  }
}
