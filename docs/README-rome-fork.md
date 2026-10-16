# The rome-3.4 branch of vietnt/SDL

`rome-3.4` is upstream SDL `release-3.4.18` plus the commits Rome and
goose_gfx need on top of it:

- fork features: `SDL_CopyGPUBufferToTexture`, the GPU command buffer
  timing API (`SDL_AcquireGPUTimedCommandBuffer` and the rest), the Metal
  backend handles and interop contract, and bounded Metal cycled pools;
- `SDL_GetGPUMemoryInfo` and the corrected `ID3D12QueryHeap` IID, which Rome
  used to apply as build-time patches. Unlike Rome's patch, Metal reports
  unified or dedicated memory, from the device, with its recommended working
  set as the budget (`test/testgpu_memory_info.c`);
- fixes for SDL bugs that upstream does not have yet: no Vulkan
  defragmentation while another command buffer records
  (`test/testgpu_defrag_recording.c`), and a fence released after a query
  no longer leaking its command buffer on Vulkan and Direct3D 12
  (`test/testgpu_fence_release.c`);
- a fix for the fork's own timing code: destroying a Vulkan device with
  timings not yet polled no longer corrupts the heap
  (`test/testgpu_timing_destroy.c`).

## Never rewrite this branch

goose_gfx pins this branch's commits as its SDL submodule, and Goose
packages fetch that submodule by commit hash, as does any build that pins
an SDL commit. A commit that stops being reachable breaks every build that
pins it. So `rome-3.4` only moves forward: no rebase, no amend, no force
push, no deleted commits.

Moving to a newer upstream release means a new branch, for example
`rome-3.4.20` cut from `release-3.4.20` with these commits cherry-picked
onto it, and then moving the pins over to it. `rome-3.4` stays as it is.
