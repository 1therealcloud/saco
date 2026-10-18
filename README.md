# SA-MP

An open-source reimplementation of SA-MP 0.3.DL/0.3.7, written from scratch.

There's no TTMath, D3DX9, or DXUT here. Math uses DirectXMath, and rendering and UI go through Direct3D 9 and our own code.

## Dependencies

- [RakNet](https://github.com/1therealcloud/RakNet) - networking, with SA-MP compatibility changes.
- [MinHook](https://github.com/TsudaKageyu/minhook) - hooks into GTA.
- [DirectXMath](https://github.com/microsoft/DirectXMath) - vectors, matrices, and quaternions used by sync.
- [libcurl](https://github.com/curl/curl) - HTTP/HTTPS requests and custom model downloads. HTTPS uses Windows Schannel.
- [LZMA SDK](https://github.com/ip7z/7zip) - decoding compressed 0.3.DL model downloads.
- [BASS](https://www.un4seen.com/) - audio stream playback.
- [stb](https://github.com/nothings/stb) - PNG screenshot encoding.
- [Tracy](https://github.com/wolfpld/tracy) - optional profiling, disabled by default.

GTA and RenderWare bindings include code from plugin-sdk.

## Build

You'll need Windows, Visual Studio 2022 with the C++ tools and Windows SDK, and CMake 3.21 or newer.

```bash
git clone --recurse-submodules https://github.com/1therealcloud/saco.git
cd saco

cmake -S . -B build -A Win32
cmake --build build --config Release
```

The DLL ends up in `build/Release/samp.dll`.
Use `Debug` for development or `RelWithDebInfo` for an optimized build with debug information.

To enable Tracy, configure with `-DSAMP_TRACY=ON`.

With [NSIS](https://nsis.sourceforge.io/) installed, build the installer with:

```bash
cmake --build build --config Release --target installer
```

The installer is written to `build/installer/Release/sa-mp-0.3.DL-R1-install.exe`.
Resources and the original `samp.saa` come from `assets/payload`, and the separately built launcher comes from `assets/samp.exe`.
The DLL, BASS runtime, debug launcher, and RCON client are taken from the Release build. `assets/saa` is kept as reference source and isn't repacked.

GitHub Actions builds all three Win32 configurations and the Release installer. Successful builds of `master` update the `nightly` prerelease with the installer, `Release.zip`, `RelWithDebInfo.zip`, and `Debug.zip`. Each ZIP includes resources and tools; Debug and RelWithDebInfo also include symbols. Pull requests build the same files as artifacts without publishing a release.


## TODO

- Implement the `ClientCheck` RPC.
- Test everything in-game.

## License

The client code is licensed under [MPL 2.0](LICENSE).
Dependencies and borrowed SDK code keep their own licenses.
