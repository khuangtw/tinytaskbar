// SPDX-License-Identifier: GPL-3.0-only
// Optional current-user login startup. Manual launch never writes a shortcut.
#pragma once
#include <string>

namespace startup {
enum class Action { None, Enable, Disable, Status };
enum class Status { Off, Present, MissingTarget, OtherLocation, Foreign };
Action parse(wchar_t const* option) noexcept;
std::wstring userFolder();
// Folder is injectable only for isolated filesystem tests; it is not a CLI flag.
Status inspect(std::wstring const& folder,std::wstring const& executable);
void enable(std::wstring const& folder,std::wstring const& executable);
void disable(std::wstring const& folder);
std::string describe(Status status);
#ifdef TINY_TASKBAR_STARTUP_TESTS
namespace test {void setMutationHook(void (*hook)(wchar_t const*)) noexcept;}
#endif
}
