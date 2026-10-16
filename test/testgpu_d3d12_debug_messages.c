/*
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/

/* With debug mode on Direct3D 12, messages from the debug layer must reach
   SDL's log. Clearing a render target to a colour other than the one it was
   created with makes the debug layer warn (CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE).
   Skips unless the driver is Direct3D 12 with its validation layers. */

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

static SDL_LogOutputFunction default_output;
static void *default_userdata;
static int debug_layer_messages;
static bool validation_enabled;

static void SDLCALL capture(void *userdata, int category, SDL_LogPriority priority, const char *message)
{
    if (category == SDL_LOG_CATEGORY_GPU) {
        if (SDL_strncmp(message, "D3D12 WARNING", 13) == 0 || SDL_strncmp(message, "D3D12 ERROR", 11) == 0) {
            debug_layer_messages += 1;
        }
        if (SDL_strstr(message, "Validation layers enabled")) {
            validation_enabled = true;
        }
    }
    default_output(default_userdata, category, priority, message);
}

int main(int argc, char *argv[])
{
    SDL_GPUDevice *device;
    SDL_GPUTexture *target;
    SDL_GPUCommandBuffer *cmdbuf;
    SDL_GPUColorTargetInfo color;
    SDL_PropertiesID props;

    SDL_GetLogOutputFunction(&default_output, &default_userdata);
    SDL_SetLogOutputFunction(capture, NULL);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_GPU, SDL_LOG_PRIORITY_INFO);

#ifndef SDL_PLATFORM_APPLE
    /* No window is needed; Metal needs a video driver that makes Metal views, which offscreen doesn't. */
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
#endif
    /* The driver hint, from the environment too, would override the name property below. */
    SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, "direct3d12", SDL_HINT_OVERRIDE);
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, "direct3d12");
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, true);
    device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!device) {
        SDL_Log("No Direct3D 12 device, skipping: %s", SDL_GetError());
        SDL_Quit();
        return 0;
    }
    if (!validation_enabled) {
        SDL_Log("No Direct3D 12 validation layers, skipping");
        SDL_DestroyGPUDevice(device);
        SDL_Quit();
        return 0;
    }

    {
        SDL_GPUTextureCreateInfo info;
        SDL_zero(info);
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width = 4;
        info.height = 4;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        target = SDL_CreateGPUTexture(device, &info);
    }

    cmdbuf = SDL_AcquireGPUCommandBuffer(device);
    SDL_zero(color);
    color.texture = target;
    color.clear_color.r = 1.0f;
    color.clear_color.a = 1.0f;
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_EndGPURenderPass(SDL_BeginGPURenderPass(cmdbuf, &color, 1, NULL));
    SDL_SubmitGPUCommandBuffer(cmdbuf);
    SDL_WaitForGPUIdle(device);

    SDL_Log("%d debug layer messages reached the log", debug_layer_messages);

    SDL_ReleaseGPUTexture(device, target);
    SDL_DestroyGPUDevice(device);
    SDL_Quit();

    if (debug_layer_messages == 0) {
        SDL_Log("FAIL: the debug layer's messages did not reach the log");
        return 1;
    }
    return 0;
}
