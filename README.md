# TinyTaskbar

A minimal native experiment for transparent Windows taskbar backgrounds.
Want extra features? Fork it and make it your own.

**Experimental source only. Binary release is blocked.** The r2 source passed
43 offline checks. One short Windows 11 trial did not produce an observed
Explorer crash, but the pixel test used an obscured reference and the captured
background had unexplained darkening. Full transparency, exact pixel restoration,
idle CPU/memory and second-start refusal remain unverified. An earlier r1 trial
crashed Explorer; its root cause is unresolved. This is not a daily-use release.
See [verification status](docs/TESTING.md).

## Design

A small controller and an Explorer-loaded DLL, adapted from TranslucentTB,
replace the taskbar background and border brushes. Icons, text, layout and
input are not intentionally modified. Idle operation waits for events; there
is no busy polling, webview, service or network client. Zero CPU or memory use
is not promised, and r2 idle resource measurements have not completed.

## Build and offline tests

Use an existing x64 Visual Studio C++ toolchain and Windows SDK with C++/WinRT
and XAML diagnostics headers. Development used MSVC 14.41 and SDK 10.0.22621.0.
The build does not download tools or change Windows settings.

In an **x64 Native Tools Command Prompt for Visual Studio**:

```bat
build.cmd
test.cmd
```

These commands build locally and run offline checks. They do not load the DLL
into Explorer. The native trial harness is excluded; its known measurement
defects must be corrected and reviewed before another separately approved trial.

`out` contains local experimental outputs. The build generates a matching DLL
hash for the controller. Keep `TinyTaskbar.exe` and `TinyTaskbarTap-v3.dll` from
the same build together. No binaries are included in this source publication.

## Manual controls after release blockers are resolved

Run without administrator privileges from a writable local folder. If
TranslucentTB is running, quit it normally first; TinyTaskbar refuses to compete
with it and does not change its startup entry. The tray icon offers
**Quit and restore**. The command-line equivalent is:

```bat
TinyTaskbar.exe --stop
```

Normal quit restores the saved brushes. The DLL remains loaded until Explorer
exits naturally; the utility does not force-unload it or restart Explorer.
As an interim safety restriction, any already-loaded TinyTaskbar DLL causes
another start to be refused, even for the same build. Only one diagnostics
initialization is allowed per Explorer lifetime. This is not a confirmed fix
for the earlier crash. A new natural Explorer session is required before a
separately approved re-test; no restart is performed automatically.

Results are logged beside the executable in `TinyTaskbar.log`. Logs may contain
local paths and are excluded from the repository.

## Optional login startup

Startup is **off by default**. A normal launch creates no startup entry.
These explicit commands manage only this user's owned `TinyTaskbar.lnk` in
the Windows Startup folder, then exit without loading the Explorer DLL:

```bat
TinyTaskbar.exe --startup-status
TinyTaskbar.exe --startup-enable
TinyTaskbar.exe --startup-disable
TinyTaskbar.exe --startup-remove
```

Disable and remove are equivalent and safe to repeat. Repeated enable for the
same location preserves the shortcut. Enabling from a new location updates
its target. An unmanaged file with the same name is preserved as a conflict.
The registration commands were tested, leaving startup off; actual next-login
execution is untested. Do not enable login startup for this experimental release.

To remove a future installed copy, remove its startup entry and quit normally,
then delete the files once Explorer exits naturally and releases the DLL.

## Compatibility

The observed machine ran Windows 11 Home 26H2, build 26300.9457, with one
2560x1600 monitor at 150% scaling. Taskbar internals can change with Windows
updates. Other builds, ARM64, taskbar replacements, multiple monitors, DPI
changes, hotplug, input interaction and taskbar recreation remain unverified.
If Explorer exits, the controller exits; it does not restart Explorer or itself.

## License

GPL-3.0-only; see [LICENSE](LICENSE) and [third-party credits](THIRD_PARTY.md).
The DLL adapts GPLv3 TranslucentTB source and cannot simply be relicensed as MIT.
Forks and changes are welcome under the applicable license. Any future binary
distribution must provide complete corresponding source and build/license notices.
