#include <android_native_app_glue.h>
#include <android/log.h>
#include <memory>

#include "app/CADApp.h"
#include "rendering/Pipeline.h"

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Global app instance — created on APP_CMD_INIT_WINDOW, destroyed on APP_CMD_TERM_WINDOW
static std::unique_ptr<app::CADApp> g_cadApp;
static bool g_running = true;

static void handleCmd(android_app* app, int32_t cmd)
{
    LOGI("APP_CMD: %d", cmd);

    switch (cmd)
    {
        case APP_CMD_INIT_WINDOW:
        {
            if (!app->window)
            {
                LOGE("APP_CMD_INIT_WINDOW: window is null");
                break;
            }
            LOGI("Window size: %dx%d",
                 ANativeWindow_getWidth(app->window),
                 ANativeWindow_getHeight(app->window));

            // Set asset manager BEFORE constructing CADApp so Pipeline::loadShader
            // can read .spv files from the APK rather than the filesystem.
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
            if (g_cadApp && app->window)
            {
                int32_t w = ANativeWindow_getWidth(app->window);
                int32_t h = ANativeWindow_getHeight(app->window);
                LOGI("Window resized: %dx%d", w, h);
                try
                {
                    g_cadApp->onWindowResized(app->window, w, h);
                }
                catch (const std::exception& e)
                {
                    LOGE("onWindowResized failed: %s", e.what());
                }
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
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY)
    {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN)
        {
            LOGI("Back button — requesting exit");
            g_running = false;
            return 1;
        }
    }
    return 0;
}

extern "C" __attribute__((visibility("default")))
void android_main(struct android_app* state)
{
    __android_log_print(ANDROID_LOG_INFO, "MobileCAD", "android_main ENTERED");

    state->onAppCmd     = handleCmd;
    state->onInputEvent = handleInput;

    while (g_running && !state->destroyRequested)
    {
        int ident, events;
        android_poll_source* source = nullptr;

        // Block when no CADApp (no window yet), poll immediately when rendering
        int timeoutMs = g_cadApp ? 0 : -1;
        ident = ALooper_pollOnce(timeoutMs, nullptr, &events,
                                 reinterpret_cast<void**>(&source));

        if (ident >= 0 && source)
        {
            source->process(state, source);
        }

        if (g_cadApp)
        {
            try
            {
                g_cadApp->drawFrame();
            }
            catch (const std::exception& e)
            {
                LOGE("Exception in drawFrame: %s", e.what());
                g_running = false;
            }
        }
    }

    g_cadApp.reset();
    LOGI("android_main EXITING");
}
