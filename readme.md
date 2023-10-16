This MWE demonstrates an issue when using texture arrays with NV12 encoded textures. To build:

```sh
git clone --recursive https://github.com/crud89/d3d12-nv12-texture-array.git .
cmake.exe src/ --preset windows-x64-debug/
cmake.exe --build out/build/windows-x64-debug/
```