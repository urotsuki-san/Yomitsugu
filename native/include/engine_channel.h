#pragma once
#include <windows.h>
#include <optional>
#include <deque>
#include "ime_engine.h"

namespace ime {
// UIスレッドから使う、継承ハンドルによる専用パイプ。候補の受信は非同期。
class EngineChannel {
 public:
  ~EngineChannel();
  bool Start(const std::wstring& executable, const std::wstring& profile_directory = {});
  void Stop();
  void Submit(const DecodeInput& request);
  void Learn(const DecodeInput& request, const std::string& chosen, bool explicit_selection = true);
  bool busy() const { return active_.has_value() || pending_.has_value() || !learning_.empty(); }
  void FinishLearning(DWORD timeout_ms);
  bool Poll(DecodeInput* request, std::vector<Candidate>* candidates);
  bool running() const { return process_ != nullptr; }
 private:
  HANDLE process_ = nullptr, job_ = nullptr, write_ = nullptr, read_ = nullptr;
  std::optional<DecodeInput> pending_, active_;
  struct LearningEvent { DecodeInput request; std::string chosen; bool explicit_selection; };
  std::deque<LearningEvent> learning_;
  bool active_learning_ = false;
  std::string received_;
  ULONGLONG deadline_ = 0;
};
}
