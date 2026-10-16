/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* Releasing a fence after SDL_QueryGPUFence reports it signaled must not
   keep its command buffer alive. Each frame maps a cycled transfer buffer,
   uploads from it, submits with a fence, polls the fence until it is
   signaled and releases it. A transfer buffer that every finished command
   buffer gives back is reused, so only a few backings are ever mapped. A
   command buffer that is never cleaned up keeps its backing referenced, and
   every frame then maps a new one. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define FRAMES 100
#define MAX_BACKINGS 3

int main(int argc, char *argv[])
{
    void *backings[FRAMES];
    int backing_count = 0;
    SDL_GPUDevice *device;
    SDL_GPUBuffer *buffer;
    SDL_GPUTransferBuffer *transfer;
    int frame;

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

    {
        SDL_GPUBufferCreateInfo info = { SDL_GPU_BUFFERUSAGE_VERTEX, 64, 0 };
        SDL_GPUTransferBufferCreateInfo transfer_info = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, 64, 0 };
        buffer = SDL_CreateGPUBuffer(device, &info);
        transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    }

    for (frame = 0; frame < FRAMES; frame += 1) {
        SDL_GPUCommandBuffer *cmdbuf;
        SDL_GPUCopyPass *pass;
        SDL_GPUFence *fence;
        SDL_GPUTransferBufferLocation source;
        SDL_GPUBufferRegion destination;
        void *mapped = SDL_MapGPUTransferBuffer(device, transfer, true);
        int i;

        for (i = 0; i < backing_count && backings[i] != mapped; i += 1) {
        }
        if (i == backing_count) {
            backings[backing_count++] = mapped;
        }
        SDL_memset(mapped, frame, 64);
        SDL_UnmapGPUTransferBuffer(device, transfer);

        cmdbuf = SDL_AcquireGPUCommandBuffer(device);
        pass = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_zero(source);
        source.transfer_buffer = transfer;
        SDL_zero(destination);
        destination.buffer = buffer;
        destination.size = 64;
        SDL_UploadToGPUBuffer(pass, &source, &destination, false);
        SDL_EndGPUCopyPass(pass);
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
        while (!SDL_QueryGPUFence(device, fence)) {
            SDL_Delay(1);
        }
        SDL_ReleaseGPUFence(device, fence);
    }

    SDL_Log("%s: %d transfer buffer backings mapped over %d frames", SDL_GetGPUDeviceDriver(device), backing_count, FRAMES);

    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUBuffer(device, buffer);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();

    if (backing_count > MAX_BACKINGS) {
        SDL_Log("FAIL: finished command buffers were not cleaned up");
        return 1;
    }
    return 0;
}
