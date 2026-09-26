#pragma once
#include <filesystem>
#include <string>

namespace ime {
struct DictionaryStatus {
  bool bundled = true;
  unsigned entries = 0;
  std::string result = "unknown", updated_at, checked_at;
};
DictionaryStatus ReadDictionaryStatus(const std::filesystem::path& bundled, const std::filesystem::path& cached);
std::wstring DictionaryTimeLabel(const std::string& utc);
}
