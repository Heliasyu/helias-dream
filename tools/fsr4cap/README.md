# fsr4cap: build FSR 4.1.1 for heliasdream-vk from your own DLL

From **bbport** ([deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc),
`tools/fsr4cap` at 3c57da4, GPL-2.0-or-later; the AMD FidelityFX API headers in `ffx/` are MIT).
Copied with one change: `build_assets.sh` writes the set to where Helia's Dream reads it.
Credit for the capture and replay approach goes to bbport's authors.

FSR 4.1.1 cannot be shipped. You build it once from AMD's
`amd_fidelityfx_upscaler_dx12.dll` 4.1.x, for example the one in an OptiScaler release. Nothing
of AMD's is committed or bundled. The script:

1. checks the DLL (`dll_info.py`);
2. builds dxil-spirv and `fsr4cap.exe` (MinGW);
3. runs the DLL once under Proton (GE-Proton 10+, Proton-CachyOS or Proton Experimental) for
   every output size class and quality ratio, recording its D3D12 passes;
4. translates them to SPIR-V with the model weights (`extract.py`).

```sh
tools/fsr4cap/build_assets.sh /path/to/amd_fidelityfx_upscaler_dx12.dll
# the set goes to $HELIAS_FSR411_DIR, else ~/.local/share/heliasdream/fsr4_411
```

It needs `x86_64-w64-mingw32-gcc`, cmake, ninja, python3, spirv-dis/spirv-as, git and network
(for dxil-spirv), and a Proton that runs the DLL's FSR 4.1 on your GPU. Helia's Dream then uses
FSR 4.1.1 when Upscaler is FSR 4.1.1. It picks the FP8 variant on RDNA4 (VK_EXT_shader_float8)
and INT8 elsewhere; `HELIAS_FSR411_VARIANT=int8|fp8|fp8emu` forces one. Without the set, or on a
GPU that lacks the features, it uses FSR 3.1 and the log (`fsr4:`) says why. `verify.sh` needs
bbport's fsr4-bench, which heliasdream-vk has not ported.
