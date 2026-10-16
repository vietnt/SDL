/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* SDL_UploadToGPUTexture must honour pixels_per_row and rows_per_layer. A
   3 x 2 x 2 RGBA8 texture is uploaded from rows 5 texels apart and slices 3
   rows apart, the padding filled with a byte no texel has, and downloaded
   tightly packed: every texel must come back as uploaded. A backend that
   took the rows as tightly packed reads padding into the texture, and one
   that took the slices as tightly packed gets the second slice wrong. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define W 3
#define H 2
#define D 2
#define PITCH 5
#define ROWS 3
#define PAD 0xEE

static Uint8 texel_byte(int x, int y, int z, int c)
{
    return (Uint8)(1 + c + 4 * (x + W * (y + H * z)));
}

int main(int argc, char *argv[])
{
    SDL_GPUDevice *device;
    SDL_GPUTexture *texture;
    SDL_GPUTransferBuffer *up, *down;
    SDL_GPUCommandBuffer *cmdbuf;
    SDL_GPUCopyPass *pass;
    SDL_GPUFence *fence;
    SDL_GPUTextureCreateInfo tci;
    SDL_GPUTransferBufferCreateInfo bci;
    SDL_GPUTextureTransferInfo src, dst;
    SDL_GPUTextureRegion region;
    const Uint32 up_size = PITCH * ROWS * D * 4, down_size = W * H * D * 4;
    Uint8 *bytes;
    int x, y, z, c, wrong = 0;

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

    SDL_zero(tci);
    tci.type = SDL_GPU_TEXTURETYPE_3D;
    tci.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    tci.width = W;
    tci.height = H;
    tci.layer_count_or_depth = D;
    tci.num_levels = 1;
    texture = SDL_CreateGPUTexture(device, &tci);

    SDL_zero(bci);
    bci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    bci.size = up_size;
    up = SDL_CreateGPUTransferBuffer(device, &bci);
    bci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    bci.size = down_size;
    down = SDL_CreateGPUTransferBuffer(device, &bci);
    if (!texture || !up || !down) {
        SDL_Log("FAIL: creating the texture or transfer buffers: %s", SDL_GetError());
        return 1;
    }

    bytes = (Uint8 *)SDL_MapGPUTransferBuffer(device, up, false);
    SDL_memset(bytes, PAD, up_size);
    for (z = 0; z < D; z += 1) {
        for (y = 0; y < H; y += 1) {
            for (x = 0; x < W; x += 1) {
                for (c = 0; c < 4; c += 1) {
                    bytes[((z * ROWS + y) * PITCH + x) * 4 + c] = texel_byte(x, y, z, c);
                }
            }
        }
    }
    SDL_UnmapGPUTransferBuffer(device, up);

    SDL_zero(region);
    region.texture = texture;
    region.w = W;
    region.h = H;
    region.d = D;
    cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    pass = SDL_BeginGPUCopyPass(cmdbuf);
    SDL_zero(src);
    src.transfer_buffer = up;
    src.pixels_per_row = PITCH;
    src.rows_per_layer = ROWS;
    SDL_UploadToGPUTexture(pass, &src, &region, false);
    SDL_zero(dst);
    dst.transfer_buffer = down;
    SDL_DownloadFromGPUTexture(pass, &region, &dst);
    SDL_EndGPUCopyPass(pass);
    fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdbuf);
    SDL_WaitForGPUFences(device, true, &fence, 1);
    SDL_ReleaseGPUFence(device, fence);

    bytes = (Uint8 *)SDL_MapGPUTransferBuffer(device, down, false);
    for (z = 0; z < D; z += 1) {
        for (y = 0; y < H; y += 1) {
            for (x = 0; x < W; x += 1) {
                for (c = 0; c < 4; c += 1) {
                    if (bytes[((z * H + y) * W + x) * 4 + c] != texel_byte(x, y, z, c)) {
                        wrong += 1;
                    }
                }
            }
        }
    }
    SDL_Log("%s: slice 1 row 1 read back %d,%d,%d,%d  %d,%d,%d,%d; %d of %d bytes wrong",
            SDL_GetGPUDeviceDriver(device), bytes[36], bytes[37], bytes[38], bytes[39],
            bytes[44], bytes[45], bytes[46], bytes[47], wrong, (int)down_size);
    SDL_UnmapGPUTransferBuffer(device, down);

    SDL_ReleaseGPUTransferBuffer(device, up);
    SDL_ReleaseGPUTransferBuffer(device, down);
    SDL_ReleaseGPUTexture(device, texture);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();
    if (wrong) {
        SDL_Log("FAIL: the upload did not keep to pixels_per_row and rows_per_layer");
        return 1;
    }
    return 0;
}
