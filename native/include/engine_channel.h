#pragma once
#include <windows.h>
#include <optional>
#include "ime_engine.h"

namespace ime {
// UI-thread-only, private inherited pipes. Never waits for inference.
class EngineChannel {
 public:
  ~EngineChannel();
  bool Start(const std::wstring& executable);
  void Stop();
  void Submit(const DecodeInput& request);
  bool Poll(DecodeInput* request, std::vector<Candidate>* candidates);
  bool running() const { return process_ != nullptr; }
 private:
  HANDLE process_ = nullptr, job_ = nullptr, write_ = nullptr, read_ = nullptr;
  std::optional<DecodeInput> pending_, active_;
  std::string received_;
  ULONGLONG deadline_ = 0;
};
}
