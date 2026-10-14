# SA-MP

An open-source reimplementation of SA-MP 0.3.DL/0.3.7, written from scratch.

There's no TTMath, D3DX9, or DXUT here. Math uses DirectXMath, and rendering and UI go through Direct3D 9 and our own code.

## Dependencies

- [RakNet](https://github.com/1therealcloud/RakNet) — networking, with SA-MP compatibility changes.
- [MinHook](https://github.com/TsudaKageyu/minhook) — hooks into GTA.
- [DirectXMath](https://github.com/microsoft/DirectXMath) — vectors, matrices, and quaternions used by sync.
- [libcurl](https://github.com/curl/curl) — HTTP/HTTPS requests and custom model downloads. HTTPS uses Windows Schannel.
- [LZMA SDK](https://github.com/ip7z/7zip) — decoding compressed 0.3.DL model downloads.
- [BASS](https://www.un4seen.com/) — audio stream playback.
- [stb](https://github.com/nothings/stb) — PNG screenshot encoding.
- [Tracy](https://github.com/wolfpld/tracy) — optional profiling, disabled by default.

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

## TODO

- Implement the `ClientCheck` RPC.
- Test everything in-game.

## License

The client code is licensed under [MPL 2.0](LICENSE).
Dependencies and borrowed SDK code keep their own licenses.
