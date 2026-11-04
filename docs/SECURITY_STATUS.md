# Preview 1 security and distribution status

The original Preview 1 upload on [Nexus](https://www.nexusmods.com/residentevil42023/mods/7865?tab=files) is quarantined. A moderator review was requested. No malware clearance or confirmed false-positive finding is claimed.

The GitHub release uses a packaging-only revision: the same runtime files, with the F5 sources extracted rather than nested in another ZIP, as required by Nexus archive guidance. All 78 runtime catalog entries match the original package. This packaging correction does not resolve the antivirus finding.

## Findings

| File | Existing report | Scope |
| --- | --- | --- |
| `fg_kernels.dll` | Bkav Pro: `W32.Malware.406797BF` | Optional experimental RTX 20/30 adapter only |
| `nvngx.dll_dlssnr.dll` | Cynet: `Malicious (score: 100)` | NR forwarder |
| `re4dlss.dll` | Cynet: `Malicious (score: 100)` | Unchanged Actual DLSS 0.3.1 |
| `nvngx_dlssnr.dll` | Trapmine: `Malicious.moderate.ml.score` | User-supplied unsigned NR runtime |

These are observed single-engine findings on existing reports, not proof that a file is safe or malicious. The full original ZIP had the Bkav verdict. Its existing reanalysis still reported that verdict. Exact hashes and report links follow.

- Kernel pack: [F59A45A04EDDDA2F2EBAE4B8685B05AF4432A80CE87CC78D83E4B9414E079A20](https://www.virustotal.com/gui/file/f59a45a04eddda2f2ebae4b8685b05af4432a80ce87cc78d83e4b9414e079a20). Byte-identical to [sm86 0.4.1](https://github.com/sdli1995/dlssg_for_sm86/releases/tag/0.4.1); signer `DLSSG Throwaway Test`, certificate chain untrusted by Windows. It is not a trusted NVIDIA signature.
- NR forwarder: [D5B38754225126BD848DD49FB1A97861F8C9A995A25AA2B55A7B107459B2792B](https://www.virustotal.com/gui/file/d5b38754225126bd848dd49fb1a97861f8c9a995a25aa2b55a7b107459b2792b).
- Actual DLSS: [7753B4239B291232E21732D80C80257C443CFE18C3B8DB5FC1C52F175BFF6FAF](https://www.virustotal.com/gui/file/7753b4239b291232e21732d80c80257c443cfe18c3b8db5fc1c52f175bff6faf).
- NR: [CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED](https://www.virustotal.com/gui/file/cdfa6c1ec784867182164c7836f324f087b5c9f374d0c9fe46f54999feabf3ed).

The original quarantined Nexus ZIP is A4D4815649EC641DA7E10955D93B401D9F921208DAFEAD9638CE9A351B9F7CF7. The GitHub flat-source ZIP is 78247FF47C1ED79A12750611852FA4650A88296999686D287EE2613A6C08F219. All DLL bytes are unchanged between them. No certificate was installed and no antivirus exception is required by our installer.

References: [Nexus quarantine review and archive requirements](https://help.nexusmods.com/article/117-why-has-my-mod-been-quarantined). Checksums, source and third-party notices are supplied for inspection, not as a substitute for review.
