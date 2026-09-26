#include "engine_channel.h"
#include "nlohmann/json.hpp"
#include <cstdint>

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
void EngineChannel::Stop() {
  // Jobへ登録するのは、このクラスで起動した変換プロセスだけ。
  if (job_) TerminateJobObject(job_, 0);
  Close(job_); Close(process_); Close(write_); Close(read_);
  pending_.reset(); active_.reset(); received_.clear(); learning_.clear(); active_learning_ = false;
}
bool EngineChannel::Start(const std::wstring& executable, const std::wstring& profile_directory) {
  Stop();
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
  HANDLE inherited[]{input, output, nul};
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attributes(bytes);
  STARTUPINFOEXW start{}; start.StartupInfo.cb = sizeof(start);
  start.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
  bool initialized = InitializeProcThreadAttributeList(start.lpAttributeList, 1, 0, &bytes) != FALSE;
  bool valid = initialized && nul != INVALID_HANDLE_VALUE && UpdateProcThreadAttribute(start.lpAttributeList, 0,
      PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr);
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
  const auto resumed = ResumeThread(proc.hThread); Close(proc.hThread);
  if (resumed == static_cast<DWORD>(-1)) { Stop(); return false; }
  return true;
}
void EngineChannel::Submit(const DecodeInput& request) {
  if (!process_ || request.raw_text.size() > 512 || request.raw_text.empty()) return;
  pending_ = request;  // At most one pending snapshot, the most recent input.
}
void EngineChannel::Learn(const DecodeInput& request, const std::string& chosen, bool explicit_selection) {
  if (!process_ || request.field != "prose" || chosen.empty() || request.raw_text.empty()) return;
  if (learning_.size() == 128) learning_.pop_front();
  learning_.push_back({request, chosen, explicit_selection});
}
void EngineChannel::FinishLearning(DWORD timeout_ms) {
  pending_.reset();
  const auto until = GetTickCount64() + timeout_ms;
  while (process_ && (active_learning_ || !learning_.empty()) && GetTickCount64() < until) {
    DecodeInput request; std::vector<Candidate> candidates;
    Poll(&request, &candidates); Sleep(1);
  }
}
bool EngineChannel::Poll(DecodeInput* request, std::vector<Candidate>* candidates) {
  if (!process_) return false;
  if (WaitForSingleObject(process_, 0) != WAIT_TIMEOUT || (active_ && GetTickCount64() > deadline_)) {
    Stop(); return false;
  }
  if (!active_ && (pending_ || !learning_.empty())) {
    const bool learning = !learning_.empty();
    const auto& r = learning ? learning_.front().request : *pending_;
    json j{{"version",1},{"raw",r.raw_text},{"left",NearbyContext(r.left_context,true)},
           {"right",NearbyContext(r.right_context,false)},{"field",r.field},{"phase",r.phase},{"limit",r.candidate_limit}};
    if (learning) {
      j["operation"] = "learn"; j["chosen"] = learning_.front().chosen;
      j["explicit"] = learning_.front().explicit_selection;
    }
    auto data = j.dump(); uint32_t size = static_cast<uint32_t>(data.size());
    std::string frame(reinterpret_cast<const char*>(&size), sizeof(size)); frame += data;
    DWORD written = 0;
    if (frame.size() > 16384 || !WriteFile(write_, frame.data(), static_cast<DWORD>(frame.size()), &written, nullptr) ||
        written != frame.size()) { Stop(); return false; }
    active_ = r; active_learning_ = learning;
    if (learning) learning_.pop_front(); else pending_.reset();
    deadline_ = GetTickCount64() + 30000;
  }
  DWORD available = 0;
  if (!PeekNamedPipe(read_, nullptr, 0, nullptr, &available, nullptr)) { Stop(); return false; }
  if (available) {
    char buffer[16384]; DWORD got = 0;
    if (!ReadFile(read_, buffer, (std::min)(available, static_cast<DWORD>(sizeof(buffer))), &got, nullptr)) {
      Stop(); return false;
    }
    received_.append(buffer, got);
  }
  if (received_.size() < sizeof(uint32_t)) return false;
  uint32_t size = 0; memcpy(&size, received_.data(), sizeof(size));
  if (size > 1024 * 1024) { Stop(); return false; }
  if (received_.size() < size + sizeof(size)) return false;
  try {
    auto j = json::parse(received_.substr(sizeof(size), size));
    if (active_learning_) {
      if (!j.is_object() || !j.contains("learned") || !j["learned"].is_boolean()) { Stop(); return false; }
      received_.clear(); active_.reset(); active_learning_ = false; return false;
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
    *request = *active_; *candidates = std::move(result);
    received_.clear(); active_.reset(); return true;
  } catch (...) { Stop(); return false; }
}
}
