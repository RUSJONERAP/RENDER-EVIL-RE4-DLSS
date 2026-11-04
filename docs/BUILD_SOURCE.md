# Corresponding integration source

Base: https://github.com/TheRazerMD/REFramework, commit `c663cf5a08ac467516aa9af49b1ef5c31fc15b5b`.

1. Clone that repository, check out the pinned commit and initialize its submodules.
2. Apply `source/REFramework.patch` with `git apply`, then copy the contents of `source/REFramework-overlay/` into the checkout.
3. Use an x64 MSVC developer prompt with Windows SDK, Git, Ninja and CMake. Configure with `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMKR_SKIP_GENERATION=ON`, then `cmake --build build --target RE4`.
4. The result is `build/bin/RE4/dinput8.dll`. This dedicated build defines `RE4_UNIFIED_BUILD` and does not activate the historical PD / native-research path if the data marker is missing.

The overlay contains every added header required by the patch, including inactive historical research headers referenced by the retained base. The integration source is supplied; portable compiler binaries and personal workspace data are not included.

Tests are in `source/tests/`. The NGX ABI test consumes the upstream SDK declaration from the OptiScaler source; that declaration is not embedded into our adapter. The timer test creates its marker only under its own test executable directory, never the game.

The bundled, unchanged OptiScaler binary matches `stage_alt/OptiScaler.dll` from OptiScaler v2.3.0-beta.6, source commit `fde33d310b0749ef3bef7c44e919f8da9b7e2652`. In the new full release, `source/OptiScaler-fde33d31/` contains the extracted corresponding sources supplied with that release; the older RC.2 package used `source/OptiScaler-fde33d31.zip`. Its `SUBMODULES.txt` records dependency repositories and exact commits needed for rebuilding. Original component licenses remain applicable.

The Actual DLSS DLL is closed source and unmodified. No source for it is claimed. NVIDIA runtimes are binary dependencies, not authored by this project.

Preview 1 adds `UnifiedFrameGen.hpp` and updates the panel/runtime, Present pacing, Streamline-hook coexistence and explicit SR quality integration. The patch and overlay in this repository are updated together. `source/tests/unified_framegen_test.cpp` exercises provider status, the command mailbox and main-view selection; `unified_pacing_runtime_test.cpp` checks the limiter yields to an external FG provider.

`installer/` contains the player-facing installer sources. The complete release also supplies its component catalog, defaults and runtime files. `source/installer/` contains offline tests and the release assembly scripts; rebuilding the full distribution requires the pinned upstream component inputs, not only the text repository.
