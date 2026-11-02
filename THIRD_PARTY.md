# Component credits and terms

This project combines separately licensed components. The project license covers our integration changes only; it does not relicense third-party binaries.

| Component | Author / source | Notes |
| --- | --- | --- |
| Actual DLSS (No UI Blur) | [khanguyen111](https://www.nexusmods.com/residentevil42023/mods/7795) | DLL unchanged. Project-specific Nexus Mods publication permission granted on 2 November 2026, for a free release with Donation Points disabled. [Permission and thanks](docs/ACTUAL_DLSS_PERMISSION.md). |
| OptiScaler F5 beta.6 | [OptiScaler / F5 contributors](https://github.com/janblade/OptiScaler-F5-DLSSNR-Multipass) | GPL-3.0 and dependency notices included. Matching source commit `fde33d310b0749ef3bef7c44e919f8da9b7e2652` and corresponding source archive are included in the full release. |
| REFramework foundation | [praydog, TheRazerMD / PureDark](https://github.com/TheRazerMD/REFramework) | MIT and dependency notices included; pinned base `c663cf5a08ac467516aa9af49b1ef5c31fc15b5b`. |
| DLSS5-Autopilot design | [Kizzuwatnaa and contributors](https://github.com/Kizzuwatnaa/DLSS5-Autopilot) | Visual tokens adapted under MIT; notice included. |
| DLSS SR 310.9.16.0 | NVIDIA | NVIDIA-signed binary; unchanged. |
| DLSS NR 310.9.16.0 | NVIDIA runtime supplied by the user | Unsigned; authenticity is not claimed independently verified. Unchanged. |

Exact binary fingerprints, provenance and component notices are in the full release's `manifest.json` and `licenses/`. The bundle is not an official NVIDIA or Capcom product.
