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
// Helper Functions (The Real Engine)
// ----------------------------------------------------------------------------

static void start_vertical_stream(FilterData* filter) {
    if (filter->is_live) return;
    if (filter->rtmp_url.empty() || filter->stream_key.empty()) return;

    // 1. Create RTMP Output
    filter->secondary_rtmp = obs_output_create("rtmp_custom", "vertical_output", nullptr, nullptr);
    obs_data_t* out_settings = obs_data_create();
    obs_data_set_string(out_settings, "server", filter->rtmp_url.c_str());
    obs_data_set_string(out_settings, "key", filter->stream_key.c_str());
    obs_output_update(filter->secondary_rtmp, out_settings);
    obs_data_release(out_settings);

    // 2. Setup Video Encoder (x264 Vertical)
    video_t* video = obs_get_video();
    const struct video_output_info* voi = video_output_get_info(video);
    uint32_t height = voi->height;
    uint32_t crop_w = (height * 9) / 16; // 9:16 Aspect Ratio

    obs_data_t* v_settings = obs_data_create();
    obs_data_set_int(v_settings, "bitrate", 2500); // 2.5 Mbps defaults
    obs_data_set_string(v_settings, "rate_control", "CBR");
    obs_data_set_int(v_settings, "width", crop_w);
    obs_data_set_int(v_settings, "height", height);
    obs_data_set_int(v_settings, "fps_num", voi->fps_num);
    obs_data_set_int(v_settings, "fps_den", voi->fps_den);
    
    filter->video_encoder = obs_video_encoder_create("obs_x264", "vertical_video", v_settings, nullptr);
    obs_data_release(v_settings);

    // 3. Setup Audio Encoder & Hook to Master Mix
    obs_data_t* a_settings = obs_data_create();
    obs_data_set_int(a_settings, "bitrate", 160);
    filter->audio_encoder = obs_audio_encoder_create("ffmpeg_aac", "vertical_audio", a_settings, 0, nullptr);
    obs_data_release(a_settings);
    
    obs_encoder_set_audio(filter->audio_encoder, obs_get_audio()); // Magic: Auto-feeds master audio!

    // Connect to Output
    obs_output_set_video_encoder(filter->secondary_rtmp, filter->video_encoder);
    obs_output_set_audio_encoder(filter->secondary_rtmp, filter->audio_encoder, 0);

    // 4. Start the engine
    filter->is_live = obs_output_start(filter->secondary_rtmp);
}

static void stop_vertical_stream(FilterData* filter) {
    if (!filter->is_live) return;
    
    obs_output_stop(filter->secondary_rtmp);
    obs_output_release(filter->secondary_rtmp);
    obs_encoder_release(filter->video_encoder);
    obs_encoder_release(filter->audio_encoder);
    
    filter->secondary_rtmp = nullptr;
    filter->video_encoder = nullptr;
    filter->audio_encoder = nullptr;
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
    data->rtmp_url = obs_data_get_string(settings, "rtmp_url");
    data->stream_key = obs_data_get_string(settings, "stream_key");
    data->show_safe_zones = obs_data_get_bool(settings, "show_safe_zones");
    return data;
}

static void filter_update(void* data, obs_data_t* settings) {
    FilterData* filter = (FilterData*)data;
    filter->rtmp_url = obs_data_get_string(settings, "rtmp_url");
    filter->stream_key = obs_data_get_string(settings, "stream_key");
    filter->show_safe_zones = obs_data_get_bool(settings, "show_safe_zones");
}

static void filter_destroy(void* data) {
    FilterData* filter = (FilterData*)data;
    stop_vertical_stream(filter);
    bfree(filter);
}

static obs_source_frame* filter_video_process(void* data, obs_source_frame* frame) {
    FilterData* filter = (FilterData*)data;
    
    uint32_t width = frame->width;
    uint32_t height = frame->height;
    uint32_t crop_w = (height * 9) / 16;
    uint32_t left_offset = (width - crop_w) / 2;
    uint32_t right_offset = left_offset + crop_w;

    // 1. Draw Safe Zones (Red Lines for Guide)
    if (filter->show_safe_zones && frame->format == VIDEO_FORMAT_BGRA) {
        for (uint32_t y = 0; y < height; ++y) {
            uint32_t* pixels = (uint32_t*)(frame->data[0] + y * frame->linesize[0]);
            if (left_offset > 1) pixels[left_offset] = pixels[left_offset - 1] = 0xFF0000FF;
            if (right_offset < width - 2) pixels[right_offset] = pixels[right_offset + 1] = 0xFF0000FF;
        }
    }

    // 2. Feed Encoded Frames to RTMP
    if (filter->is_live && filter->video_encoder) {
        struct encoder_frame enc_frame;
        memset(&enc_frame, 0, sizeof(enc_frame));
        enc_frame.type = frame->format;
        enc_frame.pts = frame->timestamp;
        
        // Zero-copy pointer shifting for instantaneous cropping
        if (frame->format == VIDEO_FORMAT_NV12) {
            enc_frame.data[0] = frame->data[0] + left_offset;
            enc_frame.linesize[0] = frame->linesize[0];
            enc_frame.data[1] = frame->data[1] + (left_offset / 2) * 2;
            enc_frame.linesize[1] = frame->linesize[1];
        } else if (frame->format == VIDEO_FORMAT_BGRA || frame->format == VIDEO_FORMAT_RGBA) {
            enc_frame.data[0] = frame->data[0] + left_offset * 4;
            enc_frame.linesize[0] = frame->linesize[0];
        } else {
            // Fallback safety
            enc_frame.data[0] = frame->data[0];
            enc_frame.linesize[0] = frame->linesize[0];
        }

        obs_encoder_encode_video(filter->video_encoder, &enc_frame);
    }
    
    return frame;
}

static obs_properties_t* get_properties(void* data) {
    FilterData* filter = (FilterData*)data;
    obs_properties_t* props = obs_properties_create();
    
    // UI Elements
    obs_properties_add_text(props, "status", "Stream Status", OBS_TEXT_INFO);
    obs_property_t* status = obs_properties_get(props, "status");
    obs_property_set_long_description(status, filter && filter->is_live ? "🟢 LIVE (Streaming)" : "🔴 OFFLINE");
    obs_property_set_enabled(status, false);

    obs_properties_add_text(props, "rtmp_url", "Vertical RTMP URL", OBS_TEXT_DEFAULT);
    obs_properties_add_text(props, "stream_key", "Vertical Stream Key", OBS_TEXT_PASSWORD);
    obs_properties_add_bool(props, "show_safe_zones", "Show Vertical Safe Zones (Red Lines)");
    
    obs_properties_add_button(props, "btn_toggle", "Start / Stop Vertical Stream", 
        [](obs_properties_t*, obs_property_t*, void* d) -> bool {
            FilterData* f = (FilterData*)d;
            if (f->is_live) stop_vertical_stream(f);
            else start_vertical_stream(f);
            return true; // Auto-refreshes the UI to show LIVE/OFFLINE
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
        filter_info.update = filter_update; // IMPORTANT: Added so settings are saved!
        filter_info.destroy = filter_destroy;
        filter_info.get_properties = get_properties;
        filter_info.filter_video = filter_video_process;

        obs_register_source(&filter_info);
        return true;
    }

    void obs_module_unload(void) {}
}