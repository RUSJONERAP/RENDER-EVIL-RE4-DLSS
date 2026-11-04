# Frame Generation — Preview 1

The full installer adds FG to the before-HUD SR/NR integration. Actual DLSS 0.3.1 owns the early Streamline path; the unified panel sends FG multiplier/Reflex commands through a mailbox and reads the current session's status. F5 stays on NoFG input/output. Our limiter yields while Streamline is loaded.

## Setup

Keep game FSR 2 on and FSR 1 off. Close RTSS/MSI Afterburner before FG testing. Use Insert → Frame Generation. Start at 2x with Reflex On. Changing a multiplier is live; loading/unloading FG requires a restart. 1x disables interpolation while the FG provider remains loaded.

| GPU | Installer choice |
| --- | --- |
| RTX 40/50 | Native FG selected automatically if HAGS is not known disabled |
| RTX 20/30 | FG off automatically; explicit experimental sm86 profile available, R580+ required |
| Unrecognized GPU | FG off; runtime support still needs checking |

The RTX 20/30 combination is untested with our complete build and has an unresolved antivirus finding in its upstream kernel pack. It is never installed by the native RTX 40/50 or FG-off profiles. See [security status](SECURITY_STATUS.md).

NR is always installed. Setup's NR dropdown controls startup state only; Keep current setting preserves previous preferences, or starts off on a fresh install. NR can always be toggled later through Insert.

## What was verified

An RTX 50 game session ran SR + NR + FG 2x together. Measured generation counters also distinguished 2x and 3x despite a 120 FPS driver cap. That cap explains why changing the multiplier did not raise the displayed FPS. Windows/NVIDIA driver settings were not changed.

RTX 40 hardware, full RTX 20/30 integration, long playthroughs, all cutscenes, higher multipliers, ReShade and non-native-resolution combinations have not completed broad game testing. Normal menu exit is preferred; the old Alt+F4 error is not declared fixed.

Installer checks passed separately: 242 assertions, genuine runtime payload installation/restoration in an isolated synthetic game folder, both GUI languages and all three NR startup choices. These checks do not establish antivirus clearance or runtime compatibility on untested GPUs.
