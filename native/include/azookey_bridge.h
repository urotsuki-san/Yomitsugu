#pragma once

#include <string>
#include <vector>

namespace ime {

struct AzookeyCandidate {
  std::string text;
  int corresponding_count = 0;
};
std::vector<AzookeyCandidate> ParseAzookeyCandidates(const std::string& json);

// Dynamic azookey-engine.dll bridge (optional). Safe no-ops when unavailable.
bool AzookeyEnsureReady();
bool AzookeyAvailable();
std::vector<std::string> AzookeyConvert(const std::string& hiragana, int limit);
void AzookeySetZenzai(bool enabled, const std::string& weight_path, int inference_limit);
std::string AzookeyZenzaiStatus();
bool AzookeySetUserDictionary(const std::string& json);

}  // namespace ime
