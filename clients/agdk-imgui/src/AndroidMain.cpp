#include <game-activity/GameActivity.h>
#include <game-activity/GameActivity_events.h>
#include <game-text-input/gametextinput.h>
#include <android/log.h>
#include <android/native_window.h>
#include <memory>

#include "GameClient.hpp"

extern "C" {
    void android_main(struct android_app* app) {
        JNIEnv* env = nullptr;
        app->activity->vm->AttachCurrentThread(&env, nullptr);
        
        // Initialize client
        auto client = std::make_unique<GameClient>();
        
        // Initialize window
        ANativeWindow* window = nullptr;
        int width = 0, height = 0;
        
        while (!app->destroyRequested) {
            // Process events
            int events;
            struct android_poll_source* source;
            
            while (ALooper_pollAll(window ? 0 : -1, nullptr, &events, (void**)&source) >= 0) {
                if (source != nullptr) {
                    source->process(app, source);
                }
                
                if (app->destroyRequested) {
                    break;
                }
            }
            
            if (!window) {
                window = ANativeWindow_fromSurface(env, app->surface);
                if (window) {
                    width = ANativeWindow_getWidth(window);
                    height = ANativeWindow_getHeight(window);
                    client->Initialize(window, width, height);
                }
            }
            
            if (window) {
                client->Update();
                client->Render();
            }
        }
        
        client->Shutdown();
        app->activity->vm->DetachCurrentThread();
    }
}