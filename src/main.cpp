#include <android_native_app_glue.h>
#include <android/log.h>

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Minimal — no lambdas, no std::string or heavy includes in this translation unit
static void handleCmd(android_app* app, int32_t cmd) {
    LOGI("APP_CMD: %d", cmd);
    if (cmd == APP_CMD_INIT_WINDOW && app->window) {
        LOGI("Window size: %dx%d",
            ANativeWindow_getWidth(app->window),
            ANativeWindow_getHeight(app->window));
    }
    if (cmd == APP_CMD_DESTROY) {
        LOGI("APP_CMD_DESTROY — exiting");
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
    // Absolute first instruction — logcat only, no file I/O, no heap
    __android_log_print(ANDROID_LOG_INFO, "MobileCAD", "android_main ENTERED");

    state->onAppCmd  = handleCmd;
    state->onInputEvent = handleInput;

    while (!state->destroyRequested) {
        int ident, events;
        android_poll_source* source = nullptr;
        ident = ALooper_pollOnce(0, nullptr, &events, (void**)&source);
        if (ident >= 0 && source) {
            source->process(state, source);
        }
    }
    LOGI("android_main EXITING");
}
