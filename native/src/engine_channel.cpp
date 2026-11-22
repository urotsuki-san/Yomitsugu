#include "engine_channel.h"
#include "nlohmann/json.hpp"
#include <cstdint>
#include <tuple>

namespace ime {
using nlohmann::json;
namespace {
void Close(HANDLE& h) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h = nullptr; }
std::string NearbyContext(const std::string& text, bool tail) {
  if (text.size() <= 512) return text;
  size_t boundary = tail ? text.size()-512 : 512;
  if (tail) {
    while (boundary<text.size() && (static_cast<unsigned char>(text[boundary])&0xc0)==0x80) ++boundary;
    return text.substr(boundary);
  }
  while (boundary>0 && (static_cast<unsigned char>(text[boundary])&0xc0)==0x80) --boundary;
  return text.substr(0,boundary);
}
}
EngineChannel::~EngineChannel() { Stop(); }
VOID CALLBACK EngineChannel::WakeClient(PVOID context, BOOLEAN) {
  auto* channel = static_cast<EngineChannel*>(context);
  if (!channel->reply_posted_.exchange(true) &&
      !PostMessageW(channel->reply_window_, kEngineReplyMessage, channel->reply_worker_pid_, channel->reply_generation_))
    channel->reply_posted_.store(false);
}
void EngineChannel::Stop() {
  if (reply_wait_) UnregisterWaitEx(reply_wait_, INVALID_HANDLE_VALUE);
  reply_wait_ = nullptr;
  reply_posted_.store(false);
  Close(reply_event_);
  reply_confirmed_ = false;
  // Jobへ登録するのは、このクラスで起動した変換プロセスだけ。
  if (job_) TerminateJobObject(job_, 0);
  Close(job_); Close(process_); Close(write_); Close(read_); Close(cancel_);
  pending_.reset(); active_.reset(); received_.clear(); learning_.clear(); active_learning_ = false;
  active_cancellable_ = active_cancelled_ = false;
}
bool EngineChannel::Start(const std::wstring& executable, const std::wstring& profile_directory, HWND reply_window) {
  Stop();
  // Query the caller's job before putting the worker in its private lifetime job.
  // Processor affinity alone can overestimate the CPU time available to inference.
  job_threads_ = 0;
  JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
  if (QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &rate, sizeof(rate), nullptr) &&
      (rate.ControlFlags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP) && rate.CpuRate) {
    auto logical = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (logical) job_threads_ = (static_cast<unsigned long long>(rate.CpuRate)*logical + 9999)/10000;
  }
  if (profile_directory.find(L'"') != std::wstring::npos) return false;
  HANDLE input = nullptr, output = nullptr;
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  if (!CreatePipe(&input, &write_, &sa, 65536) || !CreatePipe(&read_, &output, &sa, 65536)) {
    Close(input); Close(output); Stop(); return false;
  }
  SetHandleInformation(write_, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(read_, HANDLE_FLAG_INHERIT, 0);
  HANDLE nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  cancel_ = CreateEventW(&sa, TRUE, FALSE, nullptr);
  DWORD owner_pid=0;
  if (reply_window && GetWindowThreadProcessId(reply_window, &owner_pid)==GetCurrentThreadId() &&
      owner_pid==GetCurrentProcessId())
    reply_event_ = CreateEventW(&sa, FALSE, FALSE, nullptr);
  HANDLE inherited[]{input, output, nul, cancel_, reply_event_};
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attributes(bytes);
  STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start);
  start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  bool initialized = InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &bytes) != FALSE;
  bool valid = initialized && nul != INVALID_HANDLE_VALUE && cancel_ && UpdateProcThreadAttribute(start.lpAttributeList, 0,
      PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(HANDLE) * (reply_event_ ? 5 : 4), nullptr, nullptr);
  start.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  start.StartupInfo.hStdInput = nul; start.StartupInfo.hStdOutput = nul; start.StartupInfo.hStdError = nul;
  std::wstring cmd = L"\"" + executable + L"\" --pipe " + std::to_wstring(reinterpret_cast<uintptr_t>(input)) +
                     L" " + std::to_wstring(reinterpret_cast<uintptr_t>(output));
  if (!profile_directory.empty()) {
    cmd += L" --profile \"" + profile_directory;
    if (profile_directory.back() == L'\\') cmd += L'\\';
    cmd += L'"';
  }
  job_ = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
  limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  valid = valid && job_ && SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limit, sizeof(limit));
  PROCESS_INFORMATION proc{};
  bool created = valid && CreateProcessW(executable.c_str(), cmd.data(), nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
      &start.StartupInfo, &proc);
  if (initialized) DeleteProcThreadAttributeList(start.lpAttributeList);
  Close(input); Close(output); Close(nul);
  if (!created) { Stop(); return false; }
  if (!AssignProcessToJobObject(job_, proc.hProcess)) {
    TerminateProcess(proc.hProcess, 1); Close(proc.hThread); Close(proc.hProcess); Stop(); return false;
  }
  process_ = proc.hProcess;
  reply_window_ = reply_window;
  reply_worker_pid_ = proc.dwProcessId;
  static std::atomic<DWORD> next_generation{0};
  reply_generation_ = ++next_generation;
  if (reply_event_ && !RegisterWaitForSingleObject(&reply_wait_, reply_event_, WakeClient, this,
                                                  INFINITE, WT_EXECUTEDEFAULT))
    reply_wait_ = nullptr;
  const auto resumed = ResumeThread(proc.hThread); Close(proc.hThread);
  if (resumed == static_cast<DWORD>(-1)) { Stop(); return false; }
  return true;
}
void EngineChannel::CancelObsolete(const DecodeInput& current) {
  auto obsolete = [&](const DecodeInput& prior) {
    return prior.revision != current.revision || prior.context_generation != current.context_generation ||
        prior.raw_text != current.raw_text || prior.field != current.field ||
        prior.left_context != current.left_context || prior.right_context != current.right_context;
  };
  // Typing lookups may omit an unfinished romaji suffix from this same edit.
  if (pending_ && (pending_->phase == "typing"
          ? pending_->revision != current.revision || pending_->context_generation != current.context_generation
          : obsolete(*pending_))) pending_.reset();
  if (active_cancellable_ && !active_cancelled_ && active_ && obsolete(*active_)) {
    active_cancelled_ = true;
    SetEvent(cancel_);
  }
}
void EngineChannel::Submit(const DecodeInput& request) {
  if (!process_ || request.raw_text.size() > 512 || request.raw_text.empty()) return;
  CancelObsolete(request);
  // One result is enough for repeated submissions of the same live edit.
  // Queued learning must be observed by a subsequent conversion.
  auto identity = [](const DecodeInput& r) {
    return std::tie(r.raw_text, r.left_context, r.right_context, r.field, r.phase,
                    r.punctuation_completion, r.dictionary_preview, r.shallow_search,
                    r.candidate_limit, r.revision, r.context_generation, r.interaction_generation);
  };
  if (active_ && !active_cancelled_ && !active_learning_ && learning_.empty() && identity(request) == identity(*active_)) {
    pending_.reset();
    return;
  }
  pending_ = request;  // At most one pending snapshot, the most recent input.
}
void EngineChannel::Learn(const DecodeInput& request, const std::string& chosen, bool explicit_selection, bool f10, const std::string& previous, std::uint8_t alternate_n) {
  if (!process_ || request.field != "prose" || chosen.empty() || request.raw_text.empty()) return;
  if (learning_.size() == 128) learning_.pop_front();
  learning_.push_back({request, chosen, explicit_selection, f10, alternate_n, previous.size() <= 256 ? previous : std::string{}});
}
void EngineChannel::FinishLearning(DWORD timeout_ms) {
  pending_.reset();
  const auto until = GetTickCount64() + timeout_ms;
  while (process_ && (active_learning_ || !learning_.empty()) && GetTickCount64() < until) {
    DecodeInput request; std::vector<Candidate> candidates;
    Poll(&request, &candidates); Sleep(1);
  }
}
bool EngineChannel::DispatchPending(bool previews) {
  if (active_ || (!pending_ && learning_.empty())) return true;
  const bool learning = !learning_.empty();
  const auto& r = learning ? learning_.front().request : *pending_;
  json j{{"version",1},{"raw",r.raw_text},{"left",NearbyContext(r.left_context,true)},
         {"right",NearbyContext(r.right_context,false)},{"field",r.field},{"phase",r.phase},{"limit",r.candidate_limit}};
  remaining_previews_ = !learning && previews && r.phase == "end_of_phrase" &&
      !r.dictionary_preview && !r.shallow_search ? 3 : 0;
  if (reply_wait_ && !learning && r.phase == "end_of_phrase")
    j["reply_event"] = reinterpret_cast<uintptr_t>(reply_event_);
  if (remaining_previews_) {
    j["allow_preview"] = true;
    j["max_previews"] = remaining_previews_;
    ResetEvent(cancel_);
    j["cancel_event"] = reinterpret_cast<uintptr_t>(cancel_);
  }
  active_cancellable_ = remaining_previews_ != 0;
  active_cancelled_ = false;
  if (job_threads_ && job_threads_ < 4) j["cpu_threads"] = job_threads_;
  if (r.punctuation_completion) j["punctuation_completion"] = true;
  if (r.shallow_search) j["shallow_search"] = true;
  if (r.dictionary_preview) j["dictionary_preview"] = true;
  if (learning) {
    j["operation"] = "learn"; j["chosen"] = learning_.front().chosen;
    j["explicit"] = learning_.front().explicit_selection;
    j["f10"] = learning_.front().f10;
    if (learning_.front().alternate_n) j["alternate_n"] = learning_.front().alternate_n;
    if (!learning_.front().previous.empty()) j["previous"] = learning_.front().previous;
  }
  auto data = j.dump(); uint32_t size = static_cast<uint32_t>(data.size());
  std::string frame(reinterpret_cast<const char*>(&size), sizeof(size)); frame += data;
  DWORD written = 0;
  if (frame.size() > 16384 || !WriteFile(write_, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) ||
      written != frame.size()) { Stop(); return false; }
  active_ = r; active_learning_ = learning;
  if (learning) learning_.pop_front(); else pending_.reset();
  sent_at_ = GetTickCount64();
  deadline_ = sent_at_ + 30000;
  return true;
}
bool EngineChannel::Poll(DecodeInput* request, std::vector<Candidate>* candidates, bool* provisional) {
  if (provisional) *provisional = false;
  if (!process_) return false;
  if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT || (active_ && GetTickCount64() > deadline_)) {
    Stop(); return false;
  }
  if (!DispatchPending(provisional != nullptr)) return false;
  DWORD available = 0;
  if (!PeekNamedPipe(read_, nullptr, 0, nullptr, &available, nullptr)) { Stop(); return false; }
  while (available) {
    char buffer[16384]; DWORD got = 0;
    if (!ReadFile(read_, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &got, nullptr)) {
      Stop(); return false;
    }
    if (!got) { Stop(); return false; }
    received_.append(buffer, got);
    available -= got;
  }
  bool delivered_preview = false;
  try {
    while (received_.size() >= sizeof(uint32_t)) {
      uint32_t size = 0;
      memcpy(&size, received_.data(), sizeof(size));
      if (size > 1024 * 1024) { Stop(); return false; }
      if (received_.size() < size + sizeof(size)) break;
      auto j = json::parse(received_.data() + sizeof(size), received_.data() + sizeof(size) + size);
      received_.erase(0, sizeof(size) + size);
      if (active_learning_) {
        if (!received_.empty() || !j.is_object() || !j.contains("learned") || !j["learned"].is_boolean()) { Stop(); return false; }
        active_.reset(); active_learning_ = false;
        DispatchPending(provisional != nullptr);
        return false;
      }
      bool preview = j.is_object() && j.contains("preview");
      if (preview) {
        if (!remaining_previews_ || !j["preview"].is_array() || j["preview"].size()!=1) { Stop(); return false; }
        --remaining_previews_;
        json payload;
        payload.swap(j["preview"]); j.swap(payload);
      }
      if (!active_ || !j.is_array() || j.size() > 32) { Stop(); return false; }
      std::vector<Candidate> result;
      for (const auto& item : j) {
        Candidate c; c.output_text = item.at("text").get<std::string>();
        c.reading_text = item.at("reading").get<std::string>(); c.is_raw = item.value("raw", false);
        if (c.output_text.empty() || c.output_text.size() > 65536 || c.reading_text.size() > 65536 ||
            c.output_text.find('\0') != std::string::npos || c.reading_text.find('\0') != std::string::npos ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, c.output_text.data(), static_cast<int>(c.output_text.size()), nullptr, 0)) {
          Stop(); return false;
        }
        result.push_back(std::move(c));
      }
      if (preview && (!provisional || active_cancelled_)) continue;
      if (!active_cancelled_) {
        *request = *active_; *candidates = std::move(result);
        if (provisional) *provisional = preview;
      }
      if (preview) {
        // Suppress superseded previews when the final frame is already buffered.
        delivered_preview = true;
        continue;
      }
      if (!received_.empty()) { Stop(); return false; }
      const bool deliver = !active_cancelled_;
      if (deliver) last_decode_ms_ = static_cast<DWORD>(GetTickCount64() - sent_at_);
      active_.reset(); remaining_previews_ = 0;
      active_cancellable_ = active_cancelled_ = false;
      DispatchPending(provisional != nullptr);
      return deliver;
    }
    return delivered_preview;
  } catch (...) { Stop(); return false; }
}
}
