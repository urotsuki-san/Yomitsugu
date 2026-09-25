#pragma once
#include <windows.h>
#include <filesystem>

void ShowDictionaryEditor(HWND owner, const std::filesystem::path& user_directory,
                          void (*on_saved)());
