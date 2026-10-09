# TinyTaskbar

A small native Windows utility for transparent taskbar backgrounds. v4 replaces
the background and border brushes while preserving icons, text, layout and input.
It waits for events: no busy polling, webview, service, network client or updater.

**Source only; no binary release or installer is provided here.** On one Windows
11 machine, a 30.9-second basic test passed visible transparency, normal restore
and reopening the same broker with its initialization count remaining 1. During
a 30-second idle sample, neither the controller nor the whole Explorer process
showed a measurable CPU-time increase. Controller working set was about **9.6 MiB**
and private memory about **1.6 MiB**. These are observations, not a promise of zero
resources, universal compatibility or long-term stability. See [testing](docs/TESTING.md).

## Build and offline checks

Use an existing **x64 Native Tools Command Prompt for Visual Studio**, a Windows
SDK with C++/WinRT and XAML diagnostics headers, and **Python 3** available as
`python`. The accepted build used MSVC 14.41 and SDK 10.0.22621.0.

```bat
build.cmd
test.cmd
```

Build creates the controller, paired DLL and offline checks under `out`. Python
generates the source identity before compiling; the DLL hash is generated before
building its controller. Keep `TinyTaskbar.exe` and `TinyTaskbarTap-v4.dll` from
the same build together. Build and test do not start the GUI or load the DLL into
Explorer, download a toolchain, or change Windows settings. The accepted source
passed 27 offline groups; the build uses `/W4 /WX`.

## Run and restore

Run `TinyTaskbar.exe` without administrator privileges from a writable local
folder. It writes `TinyTaskbar.log` beside itself. The tray menu offers
**Quit and restore**; the command-line equivalent is:

```bat
TinyTaskbar.exe --stop
```

The first start uses a temporary taskbar-thread hook to load the paired DLL and
initialize XAML diagnostics once. The hook is removed after startup. Normal quit
restores the original brushes; an inactive broker and DLL remain in Explorer
until Explorer exits naturally. A later matching v4 controller reconnects to that
broker without another hook, local DLL load or diagnostics initialization.

The controller checks DLL hash, source revision, protocol, Explorer birth,
user/session and ownership. An older/different TinyTaskbar or TranslucentTB
diagnostics image is refused. If TranslucentTB is running, quit it normally first;
its DLL may remain resident until Explorer exits naturally. TinyTaskbar does not
change TranslucentTB startup or force-unload an image. A poisoned or incomplete
restore is refused for reconnect. Only one controller owns the session.

The utility never restarts Explorer. It listens for taskbar recreation and, if
Explorer ends naturally, waits up to 30 seconds for a new taskbar. This recovery
path is implemented but was not exercised in the basic native test.

## Optional login startup

Startup is **off by default**; ordinary launch creates no entry. These explicit
commands manage only the user's owned `TinyTaskbar.lnk` in the Startup folder and
then exit without initializing the Explorer DLL:

```bat
TinyTaskbar.exe --startup-status
TinyTaskbar.exe --startup-enable
TinyTaskbar.exe --startup-disable
TinyTaskbar.exe --startup-remove
```

Disable and remove are equivalent. Same-location enable is idempotent; moving
the program and enabling updates an owned entry. A foreign same-name file is
preserved as a conflict. After the local basic test passed, shortcut creation,
target and ownership were checked. Actual next-login execution remains untested;
Windows may separately disable startup. No registry policy or global settings
are changed, and login startup runs the native executable directly.

To remove a local copy, disable its startup entry and quit normally. Delete its
files after Explorer exits naturally and releases the DLL.

## Compatibility and limits

The basic test used Windows 11 Home 26H2, build **26300.9457**, one 2560x1600
monitor and 150% scaling. It checked ordinary desktop images, not exact alpha or
pixel equality. The broker was already resident, so its full memory cost was not
isolated from Explorer. Working set and private memory are different measures
and should not be added together.

Other Windows builds, ARM64, long sessions, multiple monitors, DPI changes,
hotplug, taskbar replacements, Explorer recovery and actual login startup remain
unverified. Taskbar internals can change with updates. Earlier prototypes caused
an Explorer crash and inconclusive image tests; that history is retained and the
short v4 success does not establish the earlier crash's root cause.

## License

**GPL-3.0-only**; see [LICENSE](LICENSE) and [third-party credits](THIRD_PARTY.md).
The DLL adapts TranslucentTB GPLv3 source. The C++/WinRT MIT notice is preserved
separately. Forks are welcome under the applicable licenses. Any binary
distribution must supply complete corresponding source and build/license notices.
