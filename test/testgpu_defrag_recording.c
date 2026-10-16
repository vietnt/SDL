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

   Released textures leave holes in a memory block, and a small texture is
   placed in that block with known contents. A recording command buffer
   uploads new contents into it. A large buffer that fits in no existing
   block is then created, an empty command buffer is submitted, and only then
   is the recording one submitted. The texture must read back the new
   contents. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define WIDTH 4

static const Uint8 old_contents[WIDTH * 4] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const Uint8 new_contents[WIDTH * 4] = { 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215 };

static void upload(SDL_GPUCommandBuffer *cmdbuf, SDL_GPUTransferBuffer *transfer, Uint32 offset, SDL_GPUTexture *texture)
{
    SDL_GPUCopyPass *pass = SDL_BeginGPUCopyPass(cmdbuf);
    SDL_GPUTextureTransferInfo source;
    SDL_GPUTextureRegion destination;
    SDL_zero(source);
    source.transfer_buffer = transfer;
    source.offset = offset;
    SDL_zero(destination);
    destination.texture = texture;
    destination.w = WIDTH;
    destination.h = 1;
    destination.d = 1;
    SDL_UploadToGPUTexture(pass, &source, &destination, false);
    SDL_EndGPUCopyPass(pass);
}

int main(int argc, char *argv[])
{
    SDL_GPUDevice *device;
    SDL_GPUTexture *filler[8];
    SDL_GPUTexture *field;
    SDL_GPUBuffer *big;
    SDL_GPUTransferBuffer *transfer, *readback;
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
        SDL_GPUTransferBufferCreateInfo upload_info = { SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, sizeof(old_contents) + sizeof(new_contents), 0 };
        SDL_GPUTransferBufferCreateInfo download_info = { SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, sizeof(result), 0 };
        transfer = SDL_CreateGPUTransferBuffer(device, &upload_info);
        readback = SDL_CreateGPUTransferBuffer(device, &download_info);
        mapped = (Uint8 *)SDL_MapGPUTransferBuffer(device, transfer, false);
        SDL_memcpy(mapped, old_contents, sizeof(old_contents));
        SDL_memcpy(mapped + sizeof(old_contents), new_contents, sizeof(new_contents));
        SDL_UnmapGPUTransferBuffer(device, transfer);
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

        info.width = WIDTH;
        info.height = 1;
        field = SDL_CreateGPUTexture(device, &info);
    }

    cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    upload(cmdbuf, transfer, 0, field);
    SDL_SubmitGPUCommandBuffer(cmdbuf);
    SDL_WaitForGPUIdle(device);

    recording = SDL_AcquireGPUCommandBuffer(device);
    upload(recording, transfer, sizeof(old_contents), field);

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
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    SDL_ReleaseGPUTransferBuffer(device, readback);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();

    if (SDL_memcmp(result, new_contents, sizeof(result)) != 0) {
        SDL_Log("FAIL: the upload recorded before the other submit was lost");
        return 1;
    }
    return 0;
}
