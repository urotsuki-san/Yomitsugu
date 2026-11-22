#pragma once
#include <windows.h>
#include <filesystem>

bool ShowInputPreferences(HWND owner, const std::filesystem::path& profile);
