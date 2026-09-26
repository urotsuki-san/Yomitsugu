#pragma once

#include <string>
#include <vector>

namespace ime {

struct AzookeyCandidate {
  std::string text;
  int corresponding_count = 0;
};
std::vector<AzookeyCandidate> ParseAzookeyCandidates(const std::string& json);

// azookey-engine.dllを動的に読み込む。利用できない場合は空の結果を返す。
bool AzookeyEnsureReady();
bool AzookeyAvailable();
std::vector<std::string> AzookeyConvert(const std::string& hiragana, int limit);
void AzookeySetZenzai(bool enabled, const std::string& weight_path, int inference_limit);
std::string AzookeyZenzaiStatus();
bool AzookeySetUserDictionary(const std::string& json);

}  // namespace ime
