#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace ime {
inline constexpr char kReleaseTag[] = "v0.2.9-preview.1";
struct AppRelease {
  std::string tag, name, url, sha256;
  std::uint64_t size = 0;
};
bool NewerRelease(const std::string& candidate, const std::string& current);
bool TrustedUpdateUrl(const std::string& url, bool redirect = false);
std::optional<AppRelease> ParseAppReleases(const std::string& json, const std::string& current);
std::optional<AppRelease> CheckAppUpdate(const std::string& current = kReleaseTag);
bool VerifyInstaller(const std::filesystem::path& file, const AppRelease& release);
std::filesystem::path DownloadAppUpdate(const AppRelease& release, const std::filesystem::path& directory);
bool LaunchAppUpdate(const std::filesystem::path& file, const AppRelease& release, bool quiet = false);
}
