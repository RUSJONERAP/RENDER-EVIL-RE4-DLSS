# Validation notes

For the rendering architecture and how both SR and NR stay before interface composition, see [Keeping the HUD outside Super Resolution and Neural Rendering](HUD_PIPELINE.md).

Tested hardware: RTX 5070 Ti, Ryzen 5 7500F, 2560×1440, Windows 11, NVIDIA 621.01. Game: Steam RE4 1.5.9.0; exact executable SHA256 is in the release manifest.

| Check | Evidence |
| --- | --- |
| SR and independent NR | SR and NR toggled independently during gameplay; image changes and runtime calls verified |
| Clean HUD / menus | Menus and HUD checked for text distortion and blur; clean in the tested scenes |
| SR quality modes | DLAA, Quality and Balanced changes observed; Performance and Ultra Performance available through the same engine quality selector |
| NR stability | Latest rendering test: 28,943 completed NR frames, zero model failures; preceding GPU-lifetime revision exceeded 115,000 frames |
| DLC cutscene | DLC cutscene played smoothly in the tested scene; no before/after performance benchmark was taken |
| Frame limiter | Cap enabled at 70 FPS during play; recorded 69–70 FPS |
| Normal exit | Exited through the game menu without an error |
| Alt+F4 | Known E_ACCESSDENIED shutdown error; exact cause unconfirmed |
| RC.2 branding | Build, all 12 RU/EN panel previews, installer roundtrip and offline regression tests passed; gameplay coverage comes from the unchanged RC.1 rendering path |

Offline checks cover INI settings, isolated IAT routing, GPU-fenced context retirement, explicit Lua quality requests, NGX ABI / auxiliary-view classification and real timer pacing. The actual timer test reached approximately 90 FPS at a 90 FPS target. These tests do not replace gameplay testing.

Auxiliary-view NR bypass is implemented and unit-tested. Runtime activation has not been observed in the DLC scene tested so far. Coverage of main-campaign cutscenes remains incomplete. Frame generation is disabled because the attempted integration produced severe visual corruption.
# Preview 1 update — 4 November 2026

The earlier SR/NR results below are retained as historical evidence. For the new optional FG build, see [Frame Generation](FRAME_GENERATION.md), the [installer/packaging reports](validation-preview1/) and the separate [security status](SECURITY_STATUS.md).

Publication checks also confirmed that the updated integration patch applies to the pinned upstream base and its overlay matches the development source. The published FG test passed 21 status/mailbox/view checks. The real timer test measured 89.98 FPS at a 90 FPS target; the external-FG bypass did not apply that cap. These offline checks did not launch RE4 or load game/mod DLLs and do not establish antivirus clearance.
