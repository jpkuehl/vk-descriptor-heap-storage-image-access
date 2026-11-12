# Reproducer of device loss during storage image access through `VK_EXT_descriptor_heap`

This standalone Vulkan reproducer demonstrates a device loss during a compute shader write to a storage image. On the tested system, the failure occurs with a higher reported base address for the image's backing `VkDeviceMemory` and `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT` enabled for that allocation. The test passes when either the flag is disabled or the allocation lands at the lower address shown below.

I first encountered the issue while migrating my renderer from classic descriptor sets to `VK_EXT_descriptor_heap`. The renderer also exhibited rendering artifacts with the capture/replay flag disabled for image memory. The full bug report covers those observations and the renderer's allocation tests.

<!-- TODO: Add a link to the NVIDIA bug report once available. -->

The program creates a 1280 × 720 `VK_FORMAT_R32_UINT` image and binds it through a `VK_DESCRIPTOR_TYPE_STORAGE_IMAGE` descriptor in the resource heap. A compute dispatch queries the image dimensions and writes their product to each texel. The expected readback at texel `(0, 0)` is **921600** after `vkDeviceWaitIdle()` returns `VK_SUCCESS`.

## Requirements

- CMake 4.2 or newer and a C++ toolchain.
- Vulkan SDK 1.4.341 or newer
- DXC available on `PATH` (installed alongside Vulkan SDK).
- A Vulkan 1.4 device and driver that support `VK_EXT_descriptor_heap`. Debug builds also use `VK_EXT_device_address_binding_report` to log device address bindings. The program checks its runtime requirements.

## Build

From the repository root, configure and build with a multi-config generator such as Visual Studio.

```text
cmake -S . -B build
cmake --build build --config Debug
```

Use `--config Release` for a Release build. For a single-config generator, set `-DCMAKE_BUILD_TYPE=Debug` or `-DCMAKE_BUILD_TYPE=Release` when configuring, then run `cmake --build build`.

The build compiles the HLSL shader with DXC. To embed shader debug information, add `-DSHADER_DEBUG_INFO=ON` when configuring. This option defaults to `OFF` and is independent of the build configuration. The Debug build enables Vulkan validation and logs address bindings to the console.

## Run

From the directory containing `Repro.exe`, run these commands in PowerShell.

```powershell
.\Repro.exe --image-mem-alloc-mib 512
.\Repro.exe --image-mem-alloc-mib 512 --image-mem-no-capture-replay
.\Repro.exe --image-mem-alloc-mib 256
```

- `--image-mem-alloc-mib <MiB>` requests a minimum size for the image's backing `VkDeviceMemory` allocation. The program takes the larger of that size and the image's memory requirement reported by Vulkan.
- `--image-mem-no-capture-replay` disables `VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT` for the image's backing allocation. The flag is enabled by default.

Both arguments are optional.

## Test results

| Image backing allocation | Capture/replay flag | Reported backing memory base | Observed result |
| --- | --- | --- | --- |
| 512 MiB | On | `0x260200000` | `vkDeviceWaitIdle()` returned `VK_ERROR_DEVICE_LOST`. |
| 512 MiB | Off | `0x260200000` | The wait succeeded and readback returned `921600`. |
| 256 MiB | On | `0x007000000` | The wait succeeded and readback returned `921600`. |

The two 512 MiB runs had the same reported memory base and differed in the capture/replay flag. The 256 MiB run kept the flag enabled and landed at a lower base address. The failing runs in my tests used backing allocations with reported bases above 4 GiB. The exact cutoff is unknown, and allocation placement and results may vary with the GPU and driver.

## Tested environment

- GPU: NVIDIA GeForce RTX 2060 Mobile, 6 GiB VRAM
- OS: Windows 11
- Vulkan SDK versions tested: 1.4.341, 1.4.357, 1.4.363
- NVIDIA driver versions tested: 610.74, 616.92, 617.14, 621.01, 621.14
- Shader toolchain: HLSL compiled to SPIR-V with DXC

The issue reproduced with and without Vulkan validation layers enabled.
