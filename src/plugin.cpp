/* clang-format off */
#include <obs-module.h>
#include <obs-output.h>
#include <obs-encoder.h>
#include <obs-source.h>
#include <obs-data.h>
#include <util/bmem.h>
#include <util/threading.h>
#include <string>

// Plugin Instance State
struct FilterData {
    obs_source_t* context;
    obs_output_t* secondary_rtmp = nullptr;
    obs_encoder_t* video_encoder = nullptr;
    obs_encoder_t* audio_encoder = nullptr;
    
    // Configuration Settings
    std::string rtmp_url;
    std::string stream_key;
    bool show_safe_zones = false;
    bool is_live = false;
};

// ----------------------------------------------------------------------------
// Helper Functions
// ----------------------------------------------------------------------------

static void start_vertical_stream(FilterData* filter) {
    if (filter->is_live) return;

    // 1. Create RTMP Output
    filter->secondary_rtmp = obs_output_create("rtmp_custom", "vertical_output", nullptr, nullptr);
    
    obs_data_t* settings = obs_data_create();
    obs_data_set_string(settings, "route", filter->rtmp_url.c_str());
    obs_data_set_string(settings, "key", filter->stream_key.c_str());
    obs_output_update(filter->secondary_rtmp, settings);
    obs_data_release(settings);

    // 2. Setup Encoders (Minimalist example - requires actual encoder config)
    filter->video_encoder = obs_video_encoder_create("obs_x264", "vertical_video", nullptr, nullptr);
    filter->audio_encoder = obs_audio_encoder_create("ffmpeg_aac", "vertical_audio", nullptr, 0, nullptr);
    
    obs_output_set_video_encoder(filter->secondary_rtmp, filter->video_encoder);
    obs_output_set_audio_encoder(filter->secondary_rtmp, filter->audio_encoder, 0);

    // 3. Start
    obs_output_start(filter->secondary_rtmp);
    filter->is_live = true;
}

static void stop_vertical_stream(FilterData* filter) {
    if (!filter->is_live) return;
    
    obs_output_stop(filter->secondary_rtmp);
    obs_output_release(filter->secondary_rtmp);
    obs_encoder_release(filter->video_encoder);
    obs_encoder_release(filter->audio_encoder);
    
    filter->secondary_rtmp = nullptr;
    filter->is_live = false;
}

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
    
    // Load existing settings
    data->rtmp_url = obs_data_get_string(settings, "rtmp_url");
    data->stream_key = obs_data_get_string(settings, "stream_key");
    data->show_safe_zones = obs_data_get_bool(settings, "show_safe_zones");
    
    return data;
}

static void filter_destroy(void* data) {
    FilterData* filter = (FilterData*)data;
    stop_vertical_stream(filter);
    bfree(filter);
}

static obs_source_frame* filter_video_process(void* data, obs_source_frame* frame) {
    FilterData* filter = (FilterData*)data;
    
    // 1. Draw Safe Zones (Visual Guide Lines)
    if (filter->show_safe_zones && frame->format == VIDEO_FORMAT_BGRA) {
        uint32_t width = frame->width;
        uint32_t height = frame->height;
        uint32_t crop_w = (height * 9) / 16;
        uint32_t left = (width - crop_w) / 2;
        uint32_t right = left + crop_w;

        for (uint32_t y = 0; y < height; ++y) {
            uint32_t* pixels = (uint32_t*)(frame->data[0] + y * frame->linesize[0]);
            // Draw 2 pixel wide red lines
            if (left > 1) {
                pixels[left] = pixels[left - 1] = 0xFF0000FF; // BGRA Red
            }
            if (right < width - 2) {
                pixels[right] = pixels[right + 1] = 0xFF0000FF; // BGRA Red
            }
        }
    }

    // 2. Encoding Flow
    if (filter->is_live) {
        // [Complex implementation]
        // You would perform center cropping/scaling here, 
        // then call: obs_encoder_video_encode(filter->video_encoder, frame, ...);
    }
    
    return frame;
}

static obs_properties_t* get_properties(void* data) {
    FilterData* filter = (FilterData*)data;
    obs_properties_t* props = obs_properties_create();
    
    // Status text
    obs_properties_add_text(props, "status", "Stream Status", OBS_TEXT_INFO);
    obs_property_t* status = obs_properties_get(props, "status");
    obs_property_set_long_description(status, filter && filter->is_live ? "LIVE" : "OFFLINE");
    obs_property_set_enabled(status, false);

    obs_properties_add_text(props, "rtmp_url", "Vertical RTMP URL", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "stream_key", "Vertical Stream Key", OBS_TEXT_PASSWORD);
    obs_properties_add_bool(props, "show_safe_zones", "Show Vertical Safe Zones (Red Lines)");
    
    obs_properties_add_button(props, "btn_toggle", "Start/Stop Vertical Stream", 
        [](obs_properties_t*, obs_property_t*, void* d) -> bool {
            FilterData* f = (FilterData*)d;
            if (f->is_live) stop_vertical_stream(f);
            else start_vertical_stream(f);
            return true;
        });
    
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
        filter_info.filter_video = filter_video_process;

        obs_register_source(&filter_info);
        return true;
    }

    void obs_module_unload(void) {}
}
