#include "../src/public_index.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace fs = std::filesystem;
using ime::PublicIndex;

namespace {
void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

struct Files {
  fs::path folder;
  Files() : folder(fs::temp_directory_path() /
      (L"YomitsuguPublicIndexTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()))) {
    Check(fs::create_directory(folder), "cannot create test directory");
  }
  ~Files() {
    std::error_code ec;
    for (auto name : {L"current.tsv", L"previous.tsv", L"generic.tsv"}) fs::remove(folder / name, ec);
    fs::remove(folder, ec);
  }
};

void Write(const fs::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  Check(out.good(), "cannot write fixture");
}

std::shared_ptr<PublicIndex> Open(const fs::path& p) {
  std::string error;
  auto index = PublicIndex::Open(p, &error);
  if (!index) throw std::runtime_error(error);
  return index;
}

void Test(const fs::path& dictionary) {
  Files files;
  const auto current = files.folder / L"current.tsv";
  const auto previous = files.folder / L"previous.tsv";
  const auto generic = files.folder / L"generic.tsv";
  fs::copy_file(dictionary, current);
  auto bundled = Open(current);
  HANDLE writer = CreateFileW(current.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (writer != INVALID_HANDLE_VALUE) {
    CloseHandle(writer);
    throw std::runtime_error("bundled mapping permits in-place edits after validation");
  }
  Check(GetLastError() == ERROR_SHARING_VIOLATION, "unexpected writer error");

  // A comment changes the digest while keeping both file length and entries.
  std::ifstream in(dictionary, std::ios::binary);
  std::string bytes((std::istreambuf_iterator<char>(in)), {});
  Check(bytes.size() > 2 && bytes[0] == '#' && bytes[1] != '\n', "missing source header");
  bytes[1] = bytes[1] == '?' ? '-' : '?';
  Write(generic, bytes);
  auto rebuilt = Open(generic);
  Check(bundled->size() == rebuilt->size() &&
        bundled->has_supplemental() == rebuilt->has_supplemental(), "dictionary summary mismatch");

  std::ifstream lines(dictionary, std::ios::binary);
  std::string line, last;
  size_t checked = 0;
  while (std::getline(lines, line)) {
    if (line.empty() || line.front() == '#' || line.front() == '!') continue;
    auto key = line.substr(0, line.find('\t'));
    if (key == last) continue;
    if (key.size() >= 6) {
      const auto extended = key + "additional input";
      Check(bundled->MaxEmbeddedLength(extended) >= key.size() &&
            rebuilt->MaxEmbeddedLength(extended) >= key.size(), "embedded dictionary bound lost a key");
    }
    Check(bundled->Flags(key) == rebuilt->Flags(key), "reading flags mismatch");
    for (bool supplemental : {true, false})
      Check(bundled->Words(key, supplemental) == rebuilt->Words(key, supplemental), "candidate order or filtering changed");
    key.push_back('\0');
    Check(!bundled->Flags(key) && bundled->Words(key, false).empty(), "false lookup hit");
    key.pop_back();
    last = std::move(key);
    ++checked;
  }
  Check(checked != 0, "empty dictionary");

  // Keep one live mapping while publishing another file at the same path.
  // An equal-size replacement with invalid UTF-8 must pass through validation.
  fs::rename(current, previous);
  size_t data = bytes.find('\n') + 1;
  Check(data < bytes.size(), "no data row");
  const std::string first_key = bytes.substr(data, bytes.find('\t', data) - data);
  bytes[data] = static_cast<char>(0xff);
  Write(current, bytes);
  std::string error;
  Check(!PublicIndex::Open(current, &error) && !error.empty(), "same-size invalid replacement accepted");
  Check(bundled->Flags(first_key) && bundled->Words(first_key, false) == rebuilt->Words(first_key, false),
        "old index lost its snapshot after path replacement");

  // The generic parser preserves the earliest kind for duplicate surfaces
  // even when non-contiguous readings force the input to be sorted.
  Write(current, std::string({char(0xef), char(0xbb), char(0xbf)}) + u8"z\tlast\tnoun\r\na\tfirst\t記号\r\nz\tlast\t補助語\r\na\tsecond\t補助語\r\n");
  auto changed = Open(current);
  Check(changed->size() == 3 && changed->Flags("z") == (ime::Present | ime::Priority) &&
        changed->Words("z", true).empty(), "duplicate changed flags or priority");
  Check(changed->Words("a", false) == std::vector<std::string>{"first", "second"} &&
        changed->Words("a", true) == std::vector<std::string>{"second"}, "generic word ordering changed");
}
}

int wmain(int argc, wchar_t** argv) {
  try {
    if (argc != 2) return 1;
    Test(argv[1]);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << ex.what() << '\n';
    return 2;
  }
}
