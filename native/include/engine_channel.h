#pragma once
#include <windows.h>
#include <optional>
#include <atomic>
#include <deque>
#include "ime_engine.h"

namespace ime {
inline constexpr UINT kEngineReplyMessage = WM_APP + 0x412;
// UIスレッドから使う、継承ハンドルによる専用パイプ。候補の受信は非同期。
class EngineChannel {
 public:
  ~EngineChannel();
  bool Start(const std::wstring& executable, const std::wstring& profile_directory = {}, HWND reply_window = nullptr);
  void Stop();
  bool AcceptReplyWake(DWORD pid, DWORD generation) {
    if (!reply_wait_ || pid != reply_worker_pid_ || generation != reply_generation_ ||
        !reply_posted_.exchange(false)) return false;
    reply_confirmed_ = true;
    return busy();
  }
  bool awaits_notified_reply() const {
    return reply_confirmed_ && active_ && !active_learning_ && active_->phase == "end_of_phrase";
  }
  void Submit(const DecodeInput& request);
  void CancelObsolete(const DecodeInput& current);
  bool may_cancel() const { return active_cancellable_ || pending_.has_value(); }
  void Learn(const DecodeInput& request, const std::string& chosen, bool explicit_selection = true, bool f10 = false,
             const std::string& previous = {}, std::uint8_t alternate_n = 0);
  bool busy() const { return active_.has_value() || pending_.has_value() || !learning_.empty(); }
  void FinishLearning(DWORD timeout_ms);
  bool Poll(DecodeInput* request, std::vector<Candidate>* candidates, bool* provisional = nullptr);
  bool running() const { return process_ != nullptr; }
  DWORD last_decode_ms() const { return last_decode_ms_; }
 private:
  bool DispatchPending(bool previews);
  static VOID CALLBACK WakeClient(PVOID context, BOOLEAN);
  HWND reply_window_ = nullptr;
  HANDLE reply_event_ = nullptr, reply_wait_ = nullptr;
  DWORD reply_worker_pid_ = 0, reply_generation_ = 0;
  std::atomic<bool> reply_posted_{false};
  bool reply_confirmed_ = false;
  HANDLE process_ = nullptr, job_ = nullptr, write_ = nullptr, read_ = nullptr, cancel_ = nullptr;
  std::optional<DecodeInput> pending_, active_;
  struct LearningEvent { DecodeInput request; std::string chosen; bool explicit_selection, f10; std::uint8_t alternate_n; std::string previous; };
  std::deque<LearningEvent> learning_;
  bool active_learning_ = false, active_cancellable_ = false, active_cancelled_ = false;
  unsigned remaining_previews_ = 0;
  std::string received_;
  ULONGLONG deadline_ = 0, sent_at_ = 0;
  DWORD last_decode_ms_ = 0;
  unsigned job_threads_ = 0;
};
}
