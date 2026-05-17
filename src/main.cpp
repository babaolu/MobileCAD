#include <android_native_app_glue.h>
#include <android/log.h>
#include "rendering/Pipeline.h"
#include "app/CADApp.h"

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static app::CADApp* g_app = nullptr;

static void handleCmd(android_app* app, int32_t cmd) {
    LOGI("APP_CMD: %d", cmd);
    switch (cmd) {
    case APP_CMD_INIT_WINDOW: {
        if (!app->window) {
            LOGI("APP_CMD_INIT_WINDOW: no window yet");
            break;
        }
        rendering::Pipeline::setAssetManager(app->activity->assetManager);
        LOGI("Creating CADApp (window %dx%d)",
            ANativeWindow_getWidth(app->window),
            ANativeWindow_getHeight(app->window));
        g_app = new app::CADApp(app->window);
        LOGI("CADApp created: initialized=%d", g_app->isInitialized());
        break;
    }
    case APP_CMD_WINDOW_RESIZED: {
        if (g_app && app->window) {
            LOGI("Window resized: %dx%d",
                ANativeWindow_getWidth(app->window),
                ANativeWindow_getHeight(app->window));
            g_app->onWindowResized(app->window,
                ANativeWindow_getWidth(app->window),
                ANativeWindow_getHeight(app->window));
        }
        break;
    }
    case APP_CMD_TERM_WINDOW: {
        if (g_app) {
            LOGI("Terminating CADApp");
            delete g_app;
            g_app = nullptr;
        }
        break;
    }
    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY — exiting");
        break;
    }
}

static int32_t handleInput(android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getKeyCode(event) == AKEYCODE_BACK &&
            AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN) {
            app->destroyRequested = 1;
            return 1;
        }
    }
    return 0;
}

extern "C" __attribute__((visibility("default")))
void android_main(struct android_app* state) {
    __android_log_print(ANDROID_LOG_INFO, "MobileCAD", "android_main ENTERED");

    state->onAppCmd  = handleCmd;
    state->onInputEvent = handleInput;

    while (!state->destroyRequested) {
        int events;
        android_poll_source* source = nullptr;
        ALooper_pollOnce(0, nullptr, &events, (void**)&source);

        if (events >= 0 && source) {
            source->process(state, source);
        }

        if (g_app && g_app->isInitialized()) {
            try {
                g_app->drawFrame();
            }
            catch (const std::exception& e) {
                LOGE("drawFrame error: %s", e.what());
            }
        }
    }

    if (g_app) {
        delete g_app;
        g_app = nullptr;
    }

    LOGI("android_main EXITING");
}
