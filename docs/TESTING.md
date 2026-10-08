# Verification status

**Experimental source only; binary release is blocked.** Offline checks do not
prove native XAML threading, long-term Explorer stability or taskbar transparency.
This publication retains the reviewed r2 runtime source. It contains no binaries,
raw logs, screenshots, local user paths or private event reports.

## Offline r2 evidence

On October 8, 2026, MSVC `/W4 /WX` compilation succeeded and all 43 cases/groups
passed: 9 lifecycle, 11 readiness, 5 identity/controller, 10 startup and 8 session
safety. `build.cmd` and `test.cmd` do not load a DLL into Explorer.

- Lifecycle covers cleanup ownership, late callbacks, retained failure,
  subscription failure and owner death using deterministic fixtures.
- Readiness covers child exit, deadlines and applied acknowledgment of a live
  primary fill/stroke pair; it does not use input-idle as proof of application.
- Identity/controller covers changed or absent DLL files, cancellation, errors
  and version refusal without loading them.
- Startup fixtures cover default off, explicit commands, spaced/moved/stale
  paths, idempotence, unmanaged entries and actual ownership-race regressions.
- Session safety uses production helpers with mock objects. It covers callback
  drain through exception reporting, nested brush operations, release affinity,
  one initialization per process, and explicit E_NOINTERFACE independent of
  Win32 last-error. It establishes no native COM/XAML threading guarantee.

The current-user startup configuration was also tested with a DLL-free controller
copy in a spaced path. Eight status/enable/disable/remove commands exited 0;
repeated enable preserved the entry and the final managed shortcut was absent.
No logout/login was performed. Actual next-login execution is untested.

The r2 review fixes keep callbacks alive through their error handlers, reject
unsupported interfaces with explicit E_NOINTERFACE, and parse/update/delete
owned startup entries through the same locked handle. Missing entries use
CREATE_NEW. These fixes passed independent source review and offline checks.

## Earlier r1 Explorer crash

Observed platform: Windows 11 Home 26H2, kernel/registry build 26300.9457,
one 2560x1600 monitor at 150% scale and a 72-pixel taskbar. Explorer.exe and
Windows.UI.Xaml.dll fixed file versions were both 10.0.26100.9444.

An r1 session acknowledged a live fill/stroke pair and normal quit with hr=0,
two saved brushes restored and no retained cleanup. Its pixels and idle
resources were not independently verified.

A subsequent bounded trial reused the same r1 DLL in that Explorer and crashed
before application acknowledgment at 2026-10-08 00:58:22.701 UTC. Application
Error reported Windows.UI.Xaml.dll, exception 0xc000027b and offset 0x328f23.
Windows recovered Explorer automatically; the utility requested no restart.
Repeated diagnostics initialization is a suspected trigger, not a proven cause.

The r2 controller refuses any already-loaded TinyTaskbar DLL before installing
a hook. A DLL-wide gate permits at most one InitializeXamlDiagnosticsEx call
per Explorer lifetime, including a failed call. Normal quit does not reset it.
This restriction avoids re-entry, but is not a confirmed root-cause fix.

## One short native r2 trial

After independent review, direct user authorization and a fresh scoped preflight,
the matching r2 binaries ran once on October 8, 2026, from 03:10:08.769 to
03:10:10.492 UTC (about 1.7 seconds). No transparency utility or TinyTaskbar DLL
was present in that Explorer beforehand, and startup remained off.

The controller acknowledged application with hr=0 and one live primary fill plus
one stroke. Stroke geometry was 1706.667 by 1.333 DIP at raster scale 1.5,
equivalent to a two-physical-pixel strip. The temporary loading hook was removed.
Captured stripes became sharp and icons/text appeared unchanged. This supports
a visible background change, not a claim of complete transparency or correct input.

The harness exited 1 at its pixel gate. Its supposedly unobscured reference row
above the taskbar was actually covered by other application windows: 0/2560
pixels matched the known stripes. A whole-width eight-row check also included
normal selected taskbar-button artwork. These are measurement defects; a failed
reference comparison cannot identify a leftover taskbar border.

Independent comparisons against the known painted colors also differed: 0/17920
sampled empty-background pixels matched exactly. The stripes had unexplained
vertical darkening; expected green (40,220,40) was (23,124,23) at the taskbar top
and (37,204,37) at its bottom. Other-window shadow/composition and remaining
taskbar composition cannot be separated from these captures. The actual trial
remains failed/inconclusive and has not been reclassified as passing.

On failure, the harness used normal --stop. The controller logged hr=0,
backgrounds=2, restored=2, cleanupComplete=1, cleanupRetained=0 and pending=0,
then exited 0. A later scoped check found the same Explorer creation time,
no controller or active state mapping, and startup still off. No Application
1000/1001/1002 event occurred from trial start through 03:13:51.939 UTC.
This records no observed crash/hang in that interval; it proves no longer-term
stability. The unchanged r2 DLL remained safely pinned until Explorer exits.

The harness failed before same-pattern restoration captures and the 30-second
idle section. Brush/protocol restoration is recorded; exact pixel restoration,
r2 idle CPU/working set/private memory and second-start refusal are unverified.
There was no second start, retry, forced unload or Explorer restart.

## Remaining validation

The retained native harness (`tests/manual_trial.cpp` and `tests/evidence.hpp`)
documents the original trial and is deliberately excluded from build/test scripts.
Its reference visibility, UI sampling and failure-path restoration capture must
be corrected and independently reviewed before another separately authorized
native trial. Do not run it as shipped here.

A future fixture should place only its own nonactivating window immediately
below the taskbar, verify reference visibility before loading a controller,
sample the actual two-pixel stroke and empty background outside normal UI,
require stable frames, and record normal-stop restoration while its own pattern
still exists on every feasible failure path.

The current Explorer already contains the safety-pinned DLL. Another native
initialization requires a new natural Explorer lifetime, such as a user-chosen
normal sign-out or reboot. No restart is requested or performed by the utility.
Such a session is a prerequisite for a separately reviewed/authorized trial,
not permission to retry automatically. Passing one bounded trial would still
not establish a root-cause fix, successful re-initialization or daily-use reliability.

Multi-monitor behavior, hotplug, scaling changes, input interaction, taskbar
recreation/restart and real login startup remain untested. There is no r2 resource
measurement or zero-resource claim, and no binary release is approved.
