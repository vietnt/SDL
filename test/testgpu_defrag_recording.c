/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* A texture written by a command buffer that is still being recorded must
   keep that write when another command buffer is submitted in between. The
   Vulkan backend compacts fragmented memory during a submit by copying each
   resource into new memory; it must not move a texture that a recording
   command buffer has already written, or the copy is taken before the write
   lands and every later read sees the old contents.

   Released textures leave holes in a memory block, and a small render target
   is placed in that block and cleared to a known colour. A recording command
   buffer clears it to another colour in a render pass, which, unlike a copy,
   is not tracked as a pending transfer. A large buffer that fits in no
   existing block is then created, an empty command buffer is submitted, and
   only then is the recording one submitted. The texture must read back the
   second colour. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define WIDTH 4

/* Multiples of 0.2, which 8-bit unorm stores exactly. */
static const SDL_FColor old_colour = { 0.2f, 0.4f, 0.6f, 0.8f };
static const SDL_FColor new_colour = { 0.8f, 0.6f, 0.4f, 0.2f };
static const Uint8 new_contents[4] = { 204, 153, 102, 51 };

static void clear(SDL_GPUCommandBuffer *cmdbuf, SDL_GPUTexture *texture, SDL_FColor colour)
{
    SDL_GPUColorTargetInfo target;
    SDL_zero(target);
    target.texture = texture;
    target.clear_color = colour;
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmdbuf, &target, 1, NULL));
}

int main(int argc, char *argv[])
{
    SDL_GPUDevice *device;
    SDL_GPUTexture *filler[8];
    SDL_GPUTexture *field;
    SDL_GPUBuffer *big;
    SDL_GPUTransferBuffer *readback;
    SDL_GPUCommandBuffer *recording, *cmdbuf;
    Uint8 result[WIDTH * 4];
    Uint8 *mapped;
    int i;

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
        SDL_GPUTransferBufferCreateInfo download_info = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, sizeof(result), 0 };
        readback = SDL_CreateGPUTransferBuffer(device, &download_info);
    }

    {
        SDL_GPUTextureCreateInfo info;
        SDL_zero(info);
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = 256;
        info.height = 256;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        for (i = 0; i < 8; i += 1) {
            filler[i] = SDL_CreateGPUTexture(device, &info);
        }
        for (i = 1; i < 8; i += 2) {
            SDL_ReleaseGPUTexture(device, filler[i]);
        }
        SDL_SubmitGPUCommandBuffer(SDL_AcquireGPUCommandBuffer(device));
        SDL_WaitForGPUIdle(device);

        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = WIDTH;
        info.height = 1;
        field = SDL_CreateGPUTexture(device, &info);
    }

    cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    clear(cmdbuf, field, old_colour);
    SDL_SubmitGPUCommandBuffer(cmdbuf);
    SDL_WaitForGPUIdle(device);

    recording = SDL_AcquireGPUCommandBuffer(device);
    clear(recording, field, new_colour);

    {
        SDL_GPUBufferCreateInfo info = { SDL_GPU_BUFFERUSAGE_VERTEX, 4 << 20, 0 };
        big = SDL_CreateGPUBuffer(device, &info);
    }
    SDL_SubmitGPUCommandBuffer(SDL_AcquireGPUCommandBuffer(device));

    SDL_SubmitGPUCommandBuffer(recording);
    SDL_WaitForGPUIdle(device);

    cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    {
        SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmdbuf);
        SDL_GPUTextureRegion source;
        SDL_GPUTextureTransferInfo destination;
        SDL_zero(source);
        source.texture = field;
        source.w = WIDTH;
        source.h = 1;
        source.d = 1;
        SDL_zero(destination);
        destination.transfer_buffer = readback;
        SDL_DownloadFromGPUTexture(pass, &source, &destination);
        SDL_EndGPUCopyPass(pass);
    }
    SDL_SubmitGPUCommandBuffer(cmdbuf);
    SDL_WaitForGPUIdle(device);
    mapped = (Uint8 *)SDL_MapGPUTransferBuffer(device, readback, false);
    SDL_memcpy(result, mapped, sizeof(result));
    SDL_UnmapGPUTransferBuffer(device, readback);

    SDL_Log("%s: read back %d,%d,%d,%d  %d,%d,%d,%d", SDL_GetGPUDeviceDriver(device),
            result[0], result[1], result[2], result[3], result[12], result[13], result[14], result[15]);

    for (i = 0; i < 8; i += 2) {
        SDL_ReleaseGPUTexture(device, filler[i]);
    }
    SDL_ReleaseGPUTexture(device, field);
    SDL_ReleaseGPUBuffer(device, big);
    SDL_ReleaseGPUTransferBuffer(device, readback);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();

    for (i = 0; i < WIDTH * 4; i += 1) {
        if (result[i] != new_contents[i % 4]) {
            SDL_Log("FAIL: the clear recorded before the other submit was lost");
            return 1;
        }
    }
    return 0;
}
