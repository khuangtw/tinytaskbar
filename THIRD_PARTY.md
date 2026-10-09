# Source provenance and licenses

TinyTaskbar is a modified work distributed under GNU GPL version 3. Its complete
corresponding source and build scripts are included. See [LICENSE](LICENSE).

The Explorer DLL is adapted from **TranslucentTB 2026.2**, pinned commit
`d4636e439865df0a1a1419db408e055740ce5c74`. Credit: Charles Milette, Ethan Smith
and the TranslucentTB contributors. Modifications were authored on October 7
and 8, 2026. No upstream executable or DLL is redistributed.

Upstream GPLv3 license:
https://github.com/TranslucentTB/TranslucentTB/blob/d4636e439865df0a1a1419db408e055740ce5c74/LICENSE.md

Relevant upstream source:

- [ExplorerTAP/tapsite.cpp](https://github.com/TranslucentTB/TranslucentTB/blob/d4636e439865df0a1a1419db408e055740ce5c74/ExplorerTAP/tapsite.cpp): diagnostics initialization and COM site.
- [ExplorerTAP/visualtreewatcher.cpp](https://github.com/TranslucentTB/TranslucentTB/blob/d4636e439865df0a1a1419db408e055740ce5c74/ExplorerTAP/visualtreewatcher.cpp): visual-tree identification of the taskbar background.
- [ExplorerTAP/taskbarappearanceservice.cpp](https://github.com/TranslucentTB/TranslucentTB/blob/d4636e439865df0a1a1419db408e055740ce5c74/ExplorerTAP/taskbarappearanceservice.cpp): saving and replacing background brushes.

TinyTaskbar narrows these mechanisms to transparent background and border
brushes. It adds a controller, owner lifetime checks, reversible restoration,
separate brush tracking, startup acknowledgments, frozen-build identity checking,
opt-in current-user login startup, and a persistent broker for reconnect without
reinitialization. The accepted v4 source passed one local basic observation;
the earlier prototype crash's root cause remains unresolved. See
[verification status](docs/TESTING.md) for the scope and limits.

**C++/WinRT** headers from the installed Windows SDK are used to build the DLL.
The build used header version 2.0.220110.5. They are copyright Microsoft
Corporation and licensed under MIT. The full MIT notice is preserved in
[licenses/CPPWINRT-MIT.txt](licenses/CPPWINRT-MIT.txt); it does not change
TinyTaskbar's GPLv3 license. Upstream:
https://github.com/microsoft/cppwinrt/blob/master/LICENSE

Windows system APIs and the stock Windows application icon are used. No Windows
SDK or Visual Studio binaries are redistributed. Detours, WIL, custom COM proxies
and the TranslucentTB configuration framework are not included.
