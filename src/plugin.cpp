/* clang-format off */
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <obs-output.h>
#include <obs-encoder.h>
#include <obs-source.h>

#include <string>

// Global plugin state
struct MultiCanvasState {
    obs_output_t* secondary_rtmp = nullptr;
    obs_encoder_t* video_encoder = nullptr;
    obs_encoder_t* audio_encoder = nullptr;
};

static MultiCanvasState g_state;

// UI Properties callback
static obs_properties_t* get_properties(void* data) {
    UNUSED_PARAMETER(data); // <-- FIX 1: Compiler ko bataya ki 'data' ka use nahi karna hai

    obs_properties_t* props = obs_properties_create();
    
    obs_properties_add_text(props, "rtmp_url", "Vertical RTMP URL", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "stream_key", "Vertical Stream Key", OBS_TEXT_PASSWORD);
    
    // <-- FIX 2: Button ke bracket mein 'void*' jod diya gaya hai
    obs_properties_add_button(props, "btn_start", "Start Vertical Stream", [](obs_properties_t*, obs_property_t*, void*) -> bool {
        // Implementation for starting
        return true;
    });
    
    return props;
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("multi-canvas-engine", "en-US")

extern "C" {
    bool obs_module_load(void) {
        // Register the custom source/output logic
        return true;
    }

    void obs_module_unload(void) {
        if (g_state.secondary_rtmp) {
            obs_output_stop(g_state.secondary_rtmp);
            obs_output_release(g_state.secondary_rtmp);
        }
    }
}
