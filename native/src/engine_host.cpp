#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include "ime_engine.h"
#include "user_dictionary.h"
#include "learning_store.h"
#include "file_revision.h"
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
  if ((argc != 4 && argc != 6) || wcscmp(argv[1], L"--pipe") || (argc == 6 && wcscmp(argv[4], L"--profile"))) return 2;
  HANDLE input = reinterpret_cast<HANDLE>(_wcstoui64(argv[2], nullptr, 10));
  HANDLE output = reinterpret_cast<HANDLE>(_wcstoui64(argv[3], nullptr, 10));
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  using nlohmann::json;
  ime::UserDictionary dictionary;
  auto dictionary_path = ime::UserDictionaryPath();
  if (argc == 6) dictionary_path = std::filesystem::path(argv[5]) / L"user_dictionary.tsv";
  ime::LearningStore learning(dictionary_path.parent_path());
  std::optional<ime::FileRevision> dictionary_revision;
  ime::UserDictionary public_dictionary;
  auto public_override = dictionary_path.parent_path() / L"public_dictionary.tsv";
  wchar_t executable_path[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, executable_path, MAX_PATH)) return 6;
  auto public_bundled = std::filesystem::path(executable_path).parent_path() / L"public_dictionary.tsv";
  std::filesystem::path public_loaded_path;
  std::optional<ime::FileRevision> public_revision;
  ime::AzookeyEnsureReady();
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
      request.phase = j.value("phase", "end_of_phrase");
      request.candidate_limit = (std::clamp)(j.value("limit", 8), 1, 16);
      if (j.value("operation", "decode") == "learn") {
        const bool saved = learning.Record(request, j.at("chosen").get<std::string>(), j.value("explicit", true));
        data = json({{"learned", saved}}).dump(); size = static_cast<uint32_t>(data.size());
        if (!Transfer(output, &size, sizeof(size), true) || !Transfer(output, data.data(), size, true)) return 0;
        continue;
      }
      std::error_code ec;
      auto stamp = ime::ReadFileRevision(dictionary_path);
      if (stamp && stamp != dictionary_revision) {
        std::string error;
        if (dictionary.Load(dictionary_path, &error)) {
          // 上流の動的辞書は線形検索のため、大量の登録語はC++側の索引で検索する。
          ime::AzookeySetUserDictionary(dictionary.size() <= 1000 ? dictionary.json() : "[]");
          dictionary_revision = stamp;
        }
      } else if (!stamp && dictionary.size() && !std::filesystem::exists(dictionary_path, ec) && !ec) {
        dictionary = ime::UserDictionary(); ime::AzookeySetUserDictionary("[]"); dictionary_revision.reset();
      }
      auto public_path = ime::PublicDictionaryPath(public_bundled, public_override);
      auto public_stamp = ime::ReadFileRevision(public_path);
      if (public_stamp && (public_path != public_loaded_path || public_stamp != public_revision)) {
        std::string error;
        if (public_dictionary.Load(public_path, &error, true)) {
          public_loaded_path = public_path;
          public_revision = public_stamp;
        } else if (public_loaded_path.empty() && public_path != public_bundled) {
          if (public_dictionary.Load(public_bundled, &error, true)) public_loaded_path = public_bundled;
        }
      }
      auto result = ime::Decode(request);
      public_dictionary.Apply(request, &result, true);
      learning.Apply(request, &result);
      dictionary.Apply(request, &result);
      json response = json::array();
      for (const auto& c : result) response.push_back({{"text",c.output_text},{"reading",c.reading_text},{"raw",c.is_raw}});
      data = response.dump(); size = static_cast<uint32_t>(data.size());
      if (size > 1024*1024 || !Transfer(output, &size, sizeof(size), true) ||
          !Transfer(output, data.data(), size, true)) return 0;
    } catch (...) { return 5; }
  }
}
