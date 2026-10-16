/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* SDL_GetGPUMemoryInfo must report what the backend knows. On every driver
   the fields hold one of their documented values and an amount comes with
   its kind. Metal always knows: unified memory, with its recommended working
   set as the budget, which can't exceed system memory. Vulkan reports its
   device-local heaps as a capacity. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char *argv[])
{
    SDL_GPUDevice *device;
    SDL_GPUMemoryInfo info;
    const char *driver;
    const Uint64 system_bytes = (Uint64)SDL_GetSystemRAM() << 20;
    bool ok;
    bool pass = true;

#ifndef SDL_PLATFORM_APPLE
    /* No window is needed; Metal needs a video driver that makes Metal views, which offscreen doesn't. */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, NULL);
    if (!device) {
        SDL_Log("No GPU device, skipping: %s", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    driver = SDL_GetGPUDeviceDriver(device);

    ok = SDL_GetGPUMemoryInfo(device, &info);
    SDL_Log("%s: returned %s, architecture %u, kind %u, %" SDL_PRIu64 " bytes (system memory %" SDL_PRIu64 ")",
            driver, ok ? "true" : "false", (unsigned)info.architecture, (unsigned)info.kind, info.bytes, system_bytes);

    if (ok != (info.architecture != 0 || info.kind != 0)) {
        SDL_Log("FAIL: the return value doesn't say whether anything was reported");
        pass = false;
    }
    if (info.architecture > 2 || info.kind > 2) {
        SDL_Log("FAIL: architecture or kind is outside its documented values");
        pass = false;
    }
    if ((info.kind != 0) != (info.bytes > 0)) {
        SDL_Log("FAIL: an amount came without its kind, or a kind without an amount");
        pass = false;
    }
    if (SDL_strcmp(driver, "metal") == 0) {
        if (info.architecture != 1 || info.kind != 1 || info.bytes == 0 || info.bytes > system_bytes) {
            SDL_Log("FAIL: Metal should report unified memory with a budget no larger than system memory");
            pass = false;
        }
    } else if (SDL_strcmp(driver, "vulkan") == 0) {
        if (info.kind != 2) {
            SDL_Log("FAIL: Vulkan should report its device-local heaps as a capacity");
            pass = false;
        }
    }

    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    return pass ? 0 : 1;
}
