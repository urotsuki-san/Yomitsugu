#pragma once
#include <windows.h>
#include <filesystem>

namespace ime {
enum class ShortcutMode { Default, Disabled, Custom };
struct ModeShortcut {
  ShortcutMode mode = ShortcutMode::Default;
  UINT virtual_key = 0;
  UINT modifiers = 0;
};
struct InputPreferences {
  ModeShortcut mode_shortcut;
  bool tab_candidates = true;
};
inline constexpr wchar_t kInputPreferencesChanged[] = L"Yomitsugu.ModeShortcutChanged.1";
bool IsAllowedModeShortcut(UINT virtual_key, UINT modifiers);
InputPreferences ReadInputPreferences(const std::filesystem::path& directory);
// Merge the fields edited by this dialog into the latest profile under the settings writer lock.
bool SaveInputPreferences(const std::filesystem::path& directory, const InputPreferences& before,
                          const InputPreferences& after);
}
