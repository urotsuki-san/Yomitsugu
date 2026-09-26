#pragma once
#include <windows.h>
#include <filesystem>
#include <optional>

namespace ime {
struct FileRevision {
  DWORD volume, index_high, index_low, size_high, size_low, time_high, time_low;
  bool operator==(const FileRevision& b) const {
    return volume == b.volume && index_high == b.index_high && index_low == b.index_low &&
        size_high == b.size_high && size_low == b.size_low && time_high == b.time_high && time_low == b.time_low;
  }
  bool operator!=(const FileRevision& b) const { return !(*this == b); }
};
inline std::optional<FileRevision> ReadFileRevision(const std::filesystem::path& path) {
  auto file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  BY_HANDLE_FILE_INFORMATION info{};
  const bool ok = GetFileInformationByHandle(file, &info) != FALSE;
  CloseHandle(file);
  if (!ok) return {};
  return FileRevision{info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow,
      info.nFileSizeHigh, info.nFileSizeLow, info.ftLastWriteTime.dwHighDateTime, info.ftLastWriteTime.dwLowDateTime};
}
}
