This MWE demonstrates an issue when mixing D3D12 render passes with the new enhanced barriers feature. To build:

```sh
git clone --recursive https://github.com/crud89/d3d12-renderpass-barrier-mwe.git .
cmake.exe src/ --preset windows-x64-debug/
cmake.exe --build out/build/windows-x64-debug/
```

The debug layer will report the following error on the last barrier transitioning the back buffer from `D3D12_BARRIER_LAYOUT_RENDER_TARGET` to `D3D12_BARRIER_LAYOUT_PRESENT`:

> D3D12 ERROR: ID3D12CommandList::Barrier: Resource last transitioned by legecy barrier must be in common state before switching to enhanced barriers. [ STATE_SETTING ERROR #1351: BARRIER_INTEROP_INVALID_STATE]

This only happens in suspend/resume render pass scenarios - simple render passes work just fine. Also this only happens on swap chain back buffers. If another render target is used and resolved into the back buffer, no error is reported.

As a (hopefully temporary) workaround, the error can be silenced by surpressing `D3D12_MESSAGE_ID_BARRIER_INTEROP_INVALID_STATE` on the info queue.

## Update

Issue is fixed with Agility SDK version 1.610.2. 
