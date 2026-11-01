# Source layout

- `REFramework.patch`: changes against the pinned upstream commit in `docs/BUILD_SOURCE.md`.
- `REFramework-overlay/`: added integration headers. Some inactive research headers are retained because the base source references them; the dedicated `RE4_UNIFIED_BUILD` disables their historical rendering path.
- `unified_native.lua`: explicit SR quality requests to the game's native FSR2 route.
- `tests/`: source for offline regression tests. They do not launch RE4 or load mod DLLs.

The complete release also includes `source/OptiScaler-fde33d31.zip`, the matching source archive for the unchanged OptiScaler binary, with pinned submodule information. Actual DLSS and NVIDIA runtimes are binary dependencies; no source for them is claimed.

`installer/` in this repository is source only. Players should use the complete release ZIP, which contains its required manifest, defaults and payload.
