/* clang-format off */
#include <obs-module.h>
#include <obs-output.h>
#include <obs-encoder.h>
#include <obs-source.h>
#include <obs-video.h>

#include <string>

// Plugin Instance State
struct FilterData {
    obs_source_t* context;
    obs_output_t* secondary_rtmp = nullptr;
    // Add encoder state handles here as needed
};

// ----------------------------------------------------------------------------
// Source Info Callbacks
// ----------------------------------------------------------------------------

static const char* filter_get_name(void* data) {
    UNUSED_PARAMETER(data);
    return "Multi-Canvas Vertical Crop Filter";
}

static void* filter_create(obs_data_t* settings, obs_source_t* context) {
    FilterData* data = (FilterData*)bzalloc(sizeof(FilterData));
    data->context = context;
    return data;
}

static void filter_destroy(void* data) {
    FilterData* filter = (FilterData*)data;
    if (filter->secondary_rtmp) {
        obs_output_stop(filter->secondary_rtmp);
        obs_output_release(filter->secondary_rtmp);
    }
    bfree(filter);
}

// THIS IS WHERE YOU INTERCEPT FRAMES
static obs_source_frame* filter_video_process(void* data, obs_source_frame* frame) {
    FilterData* filter = (FilterData*)data;
    
    // 1. Frame Interception:
    // frame->data[0] contains the video buffer.
    // frame->width/height are the input dimensions (16:9).
    
    // 2. Center-Crop Logic:
    // Determine the 9:16 rectangle in the center of the frame.
    // e.g., for 1920x1080 -> 608x1080
    
    // 3. Encoder Forwarding:
    // Pass this frame to a separate encoder instance managed by this filter.
    
    return frame; // Pass through to the main 16:9 output
}

static obs_properties_t* get_properties(void* data) {
    obs_properties_t* props = obs_properties_create();
    obs_properties_add_text(props, "rtmp_url", "Vertical RTMP URL", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "stream_key", "Vertical Stream Key", OBS_TEXT_PASSWORD);
    return props;
}

// ----------------------------------------------------------------------------
// Module Life Cycle
// ----------------------------------------------------------------------------

static obs_source_info filter_info = {};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("multi-canvas-engine", "en-US")

extern "C" {
    bool obs_module_load(void) {
        filter_info.id = "multi_canvas_filter";
        filter_info.type = OBS_SOURCE_TYPE_FILTER;
        filter_info.output_flags = OBS_SOURCE_VIDEO;
        filter_info.get_name = filter_get_name;
        filter_info.create = filter_create;
        filter_info.destroy = filter_destroy;
        filter_info.get_properties = get_properties;
        filter_info.video_filter_process = filter_video_process;

        obs_register_source(&filter_info);
        return true;
    }

    void obs_module_unload(void) {
        // Source unregistration is handled automatically by OBS
    }
}