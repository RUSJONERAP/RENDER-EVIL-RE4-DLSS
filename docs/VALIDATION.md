# Validation notes

Tested hardware: RTX 5070 Ti, Ryzen 5 7500F, 2560×1440, Windows 11, NVIDIA 621.01. Game: Steam RE4 1.5.9.0; exact executable SHA256 is in the release manifest.

| Check | Evidence |
| --- | --- |
| SR and independent NR | DLL loading and runtime calls observed; user confirmed image changes |
| Clean HUD / menus | User confirmed the Actual DLSS route removed the old TemporalUpscaler UI damage |
| SR quality modes | DLAA, Quality and Balanced changes observed; Performance and Ultra Performance available through the same engine quality selector |
| NR stability | Latest rendering test: 28,943 completed NR frames, zero model failures; preceding GPU-lifetime revision exceeded 115,000 frames |
| DLC cutscene | User reported good playback; not a controlled before/after benchmark |
| Frame limiter | User confirmed enabled cap; target 70, recorded 69–70 FPS |
| Normal exit | User confirmed no error |
| Alt+F4 | Known E_ACCESSDENIED shutdown error; exact cause unconfirmed |
| RC.2 branding | C++ build, all 12 RU/EN panel previews and offline regression tests passed; no new RC.2 gameplay test claimed |

Offline checks cover INI settings, isolated IAT routing, GPU-fenced context retirement, explicit Lua quality requests, NGX ABI / auxiliary-view classification and real timer pacing. The actual timer test reached approximately 90 FPS at a 90 FPS target. These tests do not replace gameplay testing.

Auxiliary-view NR bypass is implemented and unit-tested. Runtime activation has not been observed in the DLC scene tested so far. Coverage of main-campaign cutscenes remains incomplete. Frame generation is disabled because the attempted integration produced severe visual corruption.
