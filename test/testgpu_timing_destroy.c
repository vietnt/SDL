/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* Destroying a device with timed command buffers whose timings were never
   polled must give their fences back before the fence pool goes. Each
   round submits timed command buffers, polls none of them, and destroys the
   device; a timing sample that kept its fence past the pool would return it
   to freed memory, which corrupts the heap or, where it doesn't, leaks the
   fence. SDL's allocations are counted, and every round must end with as
   many live as the first. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define ROUNDS 8
#define TIMED 4

static SDL_malloc_func real_malloc;
static SDL_calloc_func real_calloc;
static SDL_realloc_func real_realloc;
static SDL_free_func real_free;
static SDL_AtomicInt live;

static void *SDLCALL counted_malloc(size_t size)
{
    void *p = real_malloc(size);
    if (p) {
        SDL_AtomicIncRef(&live);
    }
    return p;
}

static void *SDLCALL counted_calloc(size_t n, size_t size)
{
    void *p = real_calloc(n, size);
    if (p) {
        SDL_AtomicIncRef(&live);
    }
    return p;
}

static void *SDLCALL counted_realloc(void *old, size_t size)
{
    void *p = real_realloc(old, size);
    if (p && !old) {
        SDL_AtomicIncRef(&live);
    }
    return p;
}

static void SDLCALL counted_free(void *p)
{
    if (p) {
        (void)SDL_AtomicDecRef(&live);
    }
    real_free(p);
}

int main(int argc, char *argv[])
{
    int before = -1;
    int round;

    SDL_GetOriginalMemoryFunctions(&real_malloc, &real_calloc, &real_realloc, &real_free);
    SDL_SetMemoryFunctions(counted_malloc, counted_calloc, counted_realloc, counted_free);

#ifndef SDL_PLATFORM_APPLE
    /* No window is needed; Metal needs a video driver that makes Metal views, which offscreen doesn't. */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    for (round = 0; round < ROUNDS; round += 1) {
        SDL_GPUDevice *device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, false, NULL);
        SDL_GPUCommandBufferTimingCapabilities caps;
        int i;

        if (!device) {
            SDL_Log("No GPU device, skipping: %s", SDL_GetError());
            SDL_Quit();
            return 0;
        }
        SDL_zero(caps);
        if (!SDL_GetGPUCommandBufferTimingCapabilities(device, &caps) || !caps.supports_timing) {
            SDL_Log("%s has no command buffer timing, skipping", SDL_GetGPUDeviceDriver(device));
            SDL_DestroyGPUDevice(device);
            SDL_Quit();
            return 0;
        }
        for (i = 0; i < TIMED; i += 1) {
            Uint64 sample_id = 0;
            SDL_GPUCommandBuffer *cmdbuf = SDL_AcquireGPUTimedCommandBuffer(device, &sample_id);
            SDL_SubmitGPUCommandBuffer(cmdbuf);
        }
        SDL_WaitForGPUIdle(device);
        if (round == ROUNDS - 1) {
            SDL_Log("%s: %d rounds of %d unpolled timed command buffers", SDL_GetGPUDeviceDriver(device), ROUNDS, TIMED);
        }
        SDL_DestroyGPUDevice(device);
        if (round == 0) {
            before = SDL_GetAtomicInt(&live);
        }
    }

    {
        int after = SDL_GetAtomicInt(&live);
        SDL_Log("live allocations after the first round: %d, after the last: %d", before, after);
        SDL_Quit();
        if (after > before) {
            SDL_Log("FAIL: destroying a device left timing samples' fences behind");
            return 1;
        }
    }
    return 0;
}
