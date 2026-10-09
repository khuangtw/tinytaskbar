# Verification status

This source-only publication contains the accepted v4 runtime, build scripts
and offline checks. It excludes binaries, installers, native trial tools, raw
logs, screenshots, personal paths and private review artifacts.

## Accepted offline source

On October 8, 2026, the accepted source compiled with MSVC 14.41 `/W4 /WX` and
passed **27 groups**: 17 broker lifecycle/callback groups, 6 protocol groups and
4 kernel-mutex groups in the test process's own namespace. These results were
recorded before publication; no additional native test was run to publish it.

The checks use production lifecycle and identity helpers. They cover single
initialization, ownership/generation fencing, pending work, restore/drain,
callback admission synchronized with the transition to Idle, poison/refusal,
protocol/source/Explorer-birth checks and RAII mutex release. They do not prove
native COM/XAML threading or long-term Explorer stability. `test.cmd` does not
launch a controller or access Explorer.

## Basic v4 native observation

On October 8, 2026, a separately authorized basic test ran for **30.869 seconds**,
exiting 0 on Windows 11 Home 26H2 **26300.9457**, one **2560x1600** display at
**150%** scale. It used the original controller, ordinary desktop captures,
OS process/resource queries and read-only observation of the broker's own IPC.
There was no color-pattern or foreground-stability gate.

- Two controller starts reached the primary fill/stroke applied acknowledgment.
- Both ordinary `--stop` calls restored 2/2 backgrounds, error 0, Idle, no owner,
  no pending work and no retained cleanup.
- Generation advanced on reconnect while diagnostics initialization stayed **1**.
- The original Explorer process remained alive. No Explorer restart or forced
  termination was requested.
- Both active images showed the desktop behind the taskbar with sharp icons/text;
  both normal-stop images returned to the stock background.

The idle interval was **30.013 seconds**:

| Measure | Observation |
|---|---:|
| Controller accumulated CPU increase | 0 ms |
| Controller working set at interval end | 10,022,912 bytes (about 9.6 MiB) |
| Controller private memory at interval end | 1,626,112 bytes (about 1.6 MiB) |
| Whole Explorer accumulated CPU increase | 0 ms |

Zero increase means no increase at the OS timing precision, not permanent zero
CPU. Working set and private memory are different measures, not additive. The
broker was already resident and inactive before this test; its entire memory
cost cannot be isolated from the whole Explorer process by this sample.

After basic success, an owned current-user Startup shortcut was created and its
target, empty arguments and ownership description were verified. A normal launch
was left active, still using one diagnostics initialization. **Actual next-login
startup was not exercised.** Configuration success is not login execution evidence.

## Earlier failures and limits

An earlier r1 prototype crashed Explorer during a repeated initialization trial
in Windows.UI.Xaml.dll with exception 0xc000027b. Its root cause remains unresolved.
v4 retains one broker per Explorer lifetime and reconnects without initialization;
short successful reuse does not establish a root-cause fix.

An r2 image trial had an obscured reference and unexplained darkening and remained
inconclusive. Earlier v4 trial tooling had a read-only mapping accessed with a
write-capable interlocked helper, then a combined window-order/focus gate failure
before launching the controller. Those tooling failures were not reclassified
as runtime passes. The basic observation above is a distinct later test. The old
native trial tools were removed from the current source tree; history is retained.

Exact pixel/alpha equality, long sessions, other Windows builds, ARM64, multiple
monitors, DPI/hotplug changes, taskbar replacements, Explorer/taskbar recovery,
input interaction and actual login startup remain unverified. Windows taskbar
internals may change with updates. No binary release or broad stability claim
is made by this publication.
