#pragma once

#include <windows.h>
#include <cstdint>

// Counts only lifecycle and routing outcomes. Never stores key codes or input.
struct TipDiagnostics {
  std::uint32_t size;
  std::uint32_t activate_calls;
  std::uint32_t activate_success;
  std::uint32_t activate_flags;
  std::int32_t advise_result;
  std::uint32_t test_key_down;
  std::uint32_t key_down;
  std::uint32_t test_eaten;
  std::uint32_t gated_no_context;
  std::uint32_t gated_restricted;
  std::uint32_t gated_background;
  std::uint32_t gated_compartment;
  std::uint32_t gated_readonly;
  std::uint32_t gated_no_character;
  std::uint32_t state_success;
  std::uint32_t state_failure;
  std::int32_t langbar_manager_result;
  std::int32_t langbar_add_result;
};

using ImeTipGetDiagnosticsFn = BOOL(WINAPI*)(TipDiagnostics*);
