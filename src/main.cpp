#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>

#include <memory>

#include "app/CADApp.h"
#include "imgui.h"
#include "imgui_impl_android.h"
#include "rendering/Pipeline.h"

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global app instance — created on APP_CMD_INIT_WINDOW, destroyed on APP_CMD_TERM_WINDOW
static std::unique_ptr<app::CADApp> g_cadApp;
static bool g_running = true;

// Track drawFrame call count for diagnostic logging
static uint32_t g_drawCount = 0;

static void handleCmd(android_app* app, int32_t cmd)
{
    LOGI("APP_CMD: %d  window=%p", cmd, (void*)app->window);

    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
    {
        if (!app->window)
        {
            LOGE("APP_CMD_INIT_WINDOW: no window");
            break;
        }
        LOGI("Window size: %dx%d", ANativeWindow_getWidth(app->window),
             ANativeWindow_getHeight(app->window));

        rendering::Pipeline::setAssetManager(app->activity->assetManager);

        try
        {
            g_cadApp = std::make_unique<app::CADApp>(app->window);
            LOGI("CADApp created successfully");
        }
        catch (const std::exception& e)
        {
            LOGE("Failed to create CADApp: %s", e.what());
            g_running = false;
        }
        break;
    }

    case APP_CMD_TERM_WINDOW:
    {
        LOGI("APP_CMD_TERM_WINDOW — destroying CADApp");
        g_cadApp.reset();
        break;
    }

    case APP_CMD_WINDOW_RESIZED:
    {
        LOGI("APP_CMD_WINDOW_RESIZED  window=%p  size=%dx%d", (void*)app->window,
             ANativeWindow_getWidth(app->window), ANativeWindow_getHeight(app->window));
        if (g_cadApp && app->window)
        {
            int32_t w = ANativeWindow_getWidth(app->window);
            int32_t h = ANativeWindow_getHeight(app->window);
            LOGI("onWindowResized entry: %dx%d  vkCtx=%p swap=%p", w, h,
                 (void*)g_cadApp->getVkContextPtr(), (void*)g_cadApp->getSwapchainPtr());
            try
            {
                g_cadApp->onWindowResized(app->window, w, h);
                LOGI("onWindowResized exit: OK  swapchain=%dx%d", g_cadApp->getSwapchainExtentW(),
                     g_cadApp->getSwapchainExtentH());
            }
            catch (const std::exception& e)
            {
                LOGE("onWindowResized EXCEPTION: %s", e.what());
            }
            LOGI("onWindowResized FINISHED  vkCtx=%p swap=%p", (void*)g_cadApp->getVkContextPtr(),
                 (void*)g_cadApp->getSwapchainPtr());
        }
        break;
    }

    case APP_CMD_DESTROY:
    {
        LOGI("APP_CMD_DESTROY — shutting down");
        g_cadApp.reset();
        g_running = false;
        break;
    }

    default:
        break;
    }
}

static int32_t handleInput(android_app* /*app*/, AInputEvent* event)
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGui_ImplAndroid_HandleInputEvent(event);
    }

    int32_t eventType = AInputEvent_getType(event);
    if (eventType == AINPUT_EVENT_TYPE_KEY)
    {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN)
        {
            LOGI("Back button — exiting");
            g_running = false;
            return 1;
        }
    }
    else if (eventType == AINPUT_EVENT_TYPE_MOTION)
    {
        if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse)
        {
            return 1;
        }

        if (g_cadApp)
        {
            int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
            int32_t pointerCount = static_cast<int32_t>(AMotionEvent_getPointerCount(event));
            float x0 = AMotionEvent_getX(event, 0);
            float y0 = AMotionEvent_getY(event, 0);
            float x1 = 0.0f;
            float y1 = 0.0f;
            if (pointerCount >= 2)
            {
                x1 = AMotionEvent_getX(event, 1);
                y1 = AMotionEvent_getY(event, 1);
            }
            g_cadApp->onTouchEvent(action, pointerCount, x0, y0, x1, y1);
            return 1;
        }
    }
    return 0;
}

extern "C" __attribute__((visibility("default"))) void android_main(struct android_app* state)
{
    __android_log_print(ANDROID_LOG_INFO, "MobileCAD", "android_main ENTERED");

    state->onAppCmd = handleCmd;
    state->onInputEvent = handleInput;

    while (g_running && !state->destroyRequested)
    {
        int ident, events;
        android_poll_source* source = nullptr;

        // Block before first frame (no window yet), poll at 0 ms after init
        int timeoutMs = g_cadApp ? 0 : -1;
        ident = ALooper_pollOnce(timeoutMs, nullptr, &events, reinterpret_cast<void**>(&source));

        if (ident >= 0 && source)
        {
            source->process(state, source);
        }

        if (g_cadApp)
        {
            if (!g_cadApp->drawFrame())
            {
                // vkQueueSubmit or vkQueuePresentKHR returned an error
                LOGE("drawFrame: fatal error — exiting");
                g_running = false;
            }
            else
            {
                g_drawCount++;
            }
        }
    }

    g_cadApp.reset();
    LOGI("android_main EXITING  (drawFrame ran %u times)", g_drawCount);
}
