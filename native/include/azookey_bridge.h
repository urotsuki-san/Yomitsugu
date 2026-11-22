#pragma once

#include <limits>
#include <string>
#include <vector>

namespace ime {

struct AzookeyCandidate {
  std::string text;
  int corresponding_count = 0;
  float score = -std::numeric_limits<float>::infinity();
};
std::vector<AzookeyCandidate> ParseAzookeyCandidates(const std::string& json);

// azookey-engine.dllを動的に読み込む。利用できない場合は空の結果を返す。
enum class ConversionModel { Standard, Compact, DictionaryOnly, ExtendedSearch, CompactExtendedSearch, HybridLowMemory, HybridFull, HybridDictionaryVerify, Automatic };
void AzookeySelectStartupModel(ConversionModel model);
void AzookeySetCachePressure(bool low);
void AzookeySetDictionaryPreview(bool enabled);
bool AzookeyEnsureReady();
bool AzookeyAvailable();
std::vector<std::string> AzookeyConvert(const std::string& hiragana, int limit,
                                     const std::string& left_context = {}, const std::string& right_context = {},
                                     bool refine = true, bool shallow_search = false);
void AzookeySetZenzai(bool enabled, const std::string& weight_path, int inference_limit);
std::string AzookeyZenzaiStatus();
bool AzookeySetUserDictionary(const std::string& json);
// Request-local dictionary analysis; callers validate the input and word boundaries.
std::string AzookeyDictionaryParts(const std::string& reading, const std::string& left,
                                   const std::string& right);
std::vector<std::string> AzookeyAlternativeReading(const std::string& reading,const std::string& alternative,
                                                  const std::string& left,const std::string& right);
class AzookeyPartCapture {
 public:
  AzookeyPartCapture(const std::string& reading,const std::string& left,const std::string& right);
  ~AzookeyPartCapture();
  AzookeyPartCapture(const AzookeyPartCapture&) = delete;
  bool Matches(const std::string& reading,const std::string& left,const std::string& right) const;
  std::string json;
  float first_score = -std::numeric_limits<float>::infinity();
 private:
  const std::string &reading_, &left_, &right_;
  AzookeyPartCapture* previous_;
};

}  // namespace ime
