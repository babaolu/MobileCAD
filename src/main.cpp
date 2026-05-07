#include <android_native_app_glue.h>
#include <android/log.h>
#include "app/CADApp.h"
#include "rendering/Pipeline.h"

#define LOG_TAG "MobileCAD::main"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static app::CADApp* g_cadApp = nullptr;
static volatile bool g_running = false;

static void handleCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        LOGI("APP_CMD_INIT_WINDOW");
        if (app->window && !g_cadApp) {
            rendering::Pipeline::setAssetManager(app->activity->assetManager);
            g_cadApp = new app::CADApp(app->window);
            g_running = g_cadApp->isInitialized();
        }
        break;

    case APP_CMD_TERM_WINDOW:
        LOGI("APP_CMD_TERM_WINDOW");
        if (g_cadApp) {
            g_cadApp->cleanup();
            delete g_cadApp;
            g_cadApp = nullptr;
        }
        g_running = false;
        break;

    case APP_CMD_WINDOW_RESIZED:
        LOGI("APP_CMD_WINDOW_RESIZED");
        if (g_cadApp && app->window) {
            int32_t w = ANativeWindow_getWidth(app->window);
            int32_t h = ANativeWindow_getHeight(app->window);
            g_cadApp->onWindowResized(app->window, w, h);
        }
        break;

    case APP_CMD_PAUSE:
        LOGI("APP_CMD_PAUSE");
        break;

    case APP_CMD_STOP:
        LOGI("APP_CMD_STOP");
        break;

    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY");
        if (g_cadApp) {
            g_cadApp->cleanup();
            delete g_cadApp;
            g_cadApp = nullptr;
        }
        g_running = false;
        break;

    default:
        break;
    }
}

static int32_t handleInput(android_app* app, AInputEvent* event) {
    (void)app;
    int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_KEY) {
        int32_t keycode = AKeyEvent_getKeyCode(event);
        int32_t action = AKeyEvent_getAction(event);
        if (keycode == AKEYCODE_BACK && action == AKEY_EVENT_ACTION_DOWN) {
            g_running = false;
            return 1;
        }
    }
    return 0;
}

void android_main(struct android_app* state) {
    LOGI("android_main starting");

    state->onAppCmd = handleCmd;
    state->onInputEvent = handleInput;

    while (!state->destroyRequested) {
        int ident;
        int events;
        android_poll_source* source = nullptr;
        int timeoutMillis = g_running ? 0 : -1;

        do {
            ident = ALooper_pollOnce(timeoutMillis, nullptr, &events, nullptr);
            if (ident >= 0) {
            }
        } while (ident < 0);

        // Check if there's an app command pending
        if (state->pendingCommand) {
            android_poll_source source;
            source.app = state;
            source.cmd = state->pendingCommand;
            source.process(state, &source);
            // Clear pending command
            android_app_clear_pending_cmd(state);
        }

        if (g_running && g_cadApp) {
            g_cadApp->drawFrame();
        }
    }

    LOGI("android_main exiting");
}
