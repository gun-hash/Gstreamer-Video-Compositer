#include <gst/gst.h>
#include <gst/video/video.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

typedef struct {
    int id;
    char *video_file;
    GstElement *source;
    GstElement *decodebin;
    GstElement *queue_video;
    GstElement *videoconvert;
    GstElement *videoscale;
    GstElement *capsfilter;
    GstElement *queue_audio;
    GstElement *audioconvert;
    GstElement *audioresample;
    GstElement *clocksync;
    GstPad *video_sink_pad;
    GstPad *audio_sink_pad;
    int xpos;
    int ypos;
    gboolean active;
} VideoSource;

typedef struct {
    GMainLoop *loop;
    GstElement *pipeline;
    GstElement *videomixer;
    GstElement *audiomixer;
    GstElement *video_sink;
    GstElement *audio_sink;
    GList *sources;
    int next_source_id;
    gboolean pipeline_playing;
} AppData;

static AppData app_data;

static void on_bus_message(GstBus *bus, GstMessage *msg, AppData *data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug_info;
            gst_message_parse_error(msg, &err, &debug_info);
            g_print("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_print("Debugging information: %s\n", debug_info ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            g_main_loop_quit(data->loop);
            break;
        }
        case GST_MESSAGE_EOS:
            g_print("End-Of-Stream reached.\n");
            g_main_loop_quit(data->loop);
            break;
        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            g_print("Element %s state changed from %s to %s\n",
                   GST_OBJECT_NAME(msg->src),
                   gst_element_state_get_name(old_state),
                   gst_element_state_get_name(new_state));
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(data->pipeline)) {
                data->pipeline_playing = (new_state == GST_STATE_PLAYING);
            }
            break;
        }
        case GST_MESSAGE_WARNING: {
            GError *err;
            gchar *debug_info;
            gst_message_parse_warning(msg, &err, &debug_info);
            g_print("Warning received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_clear_error(&err);
            g_free(debug_info);
            break;
        }
        default:
            break;
    }
}

// Forward declaration
static void on_pad_added(GstElement *element, GstPad *pad, gpointer data);

static VideoSource* create_video_source_struct(int id, const char *video_file, int xpos, int ypos) {
    VideoSource *source = g_malloc0(sizeof(VideoSource));
    source->id = id;
    source->video_file = g_strdup(video_file);
    source->xpos = xpos;
    source->ypos = ypos;
    source->active = FALSE;
    return source;
}

static void free_video_source(VideoSource *source) {
    if (source) {
        g_free(source->video_file);
        g_free(source);
    }
}

static gboolean add_source_idle(gpointer user_data) {
    VideoSource *source = (VideoSource*)user_data;
    char element_name[64];
    
    g_print("Adding source %d: %s at position (%d, %d)\n", source->id, source->video_file, source->xpos, source->ypos);
    
    // Check if file exists
    if (g_file_test(source->video_file, G_FILE_TEST_EXISTS) == FALSE) {
        g_print("Error: File %s does not exist\n", source->video_file);
        return G_SOURCE_REMOVE;
    }
    
    // File exists check is sufficient for now
    
    // Create source elements
    sprintf(element_name, "source_%d", source->id);
    source->source = gst_element_factory_make("filesrc", element_name);
    if (!source->source) {
        g_print("Failed to create filesrc element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    g_object_set(source->source, "location", source->video_file, NULL);
    
    sprintf(element_name, "decodebin_%d", source->id);
    source->decodebin = gst_element_factory_make("decodebin", element_name);
    if (!source->decodebin) {
        g_print("Failed to create decodebin element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    sprintf(element_name, "queue_video_%d", source->id);
    source->queue_video = gst_element_factory_make("queue", element_name);
    if (!source->queue_video) {
        g_print("Failed to create queue element for video %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    // Set queue properties for smooth playback
    g_object_set(source->queue_video, "max-size-buffers", 100, "max-size-bytes", 0, "max-size-time", 0, NULL);
    
    sprintf(element_name, "videoconvert_%d", source->id);
    source->videoconvert = gst_element_factory_make("videoconvert", element_name);
    if (!source->videoconvert) {
        g_print("Failed to create videoconvert element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    sprintf(element_name, "videoscale_%d", source->id);
    source->videoscale = gst_element_factory_make("videoscale", element_name);
    if (!source->videoscale) {
        g_print("Failed to create videoscale element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    sprintf(element_name, "capsfilter_%d", source->id);
    source->capsfilter = gst_element_factory_make("capsfilter", element_name);
    if (!source->capsfilter) {
        g_print("Failed to create capsfilter element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    sprintf(element_name, "clocksync_%d", source->id);
    source->clocksync = gst_element_factory_make("clocksync", element_name);
    if (!source->clocksync) {
        g_print("Failed to create clocksync element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    // Set video caps for consistent format (320x240)
    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                       "width", G_TYPE_INT, 320,
                                       "height", G_TYPE_INT, 240,
                                       NULL);
    g_object_set(source->capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);
    
    // Create audio elements
    sprintf(element_name, "queue_audio_%d", source->id);
    source->queue_audio = gst_element_factory_make("queue", element_name);
    if (!source->queue_audio) {
        g_print("Failed to create audio queue element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    // Set queue properties for smooth playback
    g_object_set(source->queue_audio, "max-size-buffers", 100, "max-size-bytes", 0, "max-size-time", 0, NULL);
    
    sprintf(element_name, "audioconvert_%d", source->id);
    source->audioconvert = gst_element_factory_make("audioconvert", element_name);
    if (!source->audioconvert) {
        g_print("Failed to create audioconvert element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    sprintf(element_name, "audioresample_%d", source->id);
    source->audioresample = gst_element_factory_make("audioresample", element_name);
    if (!source->audioresample) {
        g_print("Failed to create audioresample element for source %d\n", source->id);
        return G_SOURCE_REMOVE;
    }
    
    // Add elements to pipeline
    gst_bin_add_many(GST_BIN(app_data.pipeline), 
                     source->source, source->decodebin, 
                     source->queue_video, source->videoconvert, source->videoscale, source->capsfilter, source->clocksync,
                     source->queue_audio, source->audioconvert, source->audioresample, NULL);
    
    // Link elements
    gst_element_link(source->source, source->decodebin);
    gst_element_link(source->queue_video, source->videoconvert);
    gst_element_link(source->videoconvert, source->videoscale);
    gst_element_link(source->videoscale, source->capsfilter);
    gst_element_link(source->capsfilter, source->clocksync);
    gst_element_link(source->queue_audio, source->audioconvert);
    gst_element_link(source->audioconvert, source->audioresample);
    
    // Connect decodebin to queues - pass the source struct
    g_signal_connect(source->decodebin, "pad-added", G_CALLBACK(on_pad_added), source);
    
    // Let the pipeline handle state changes automatically
    // The elements will be set to PLAYING when the pipeline is set to PLAYING
    
    // Connect to videomixer - use unique pad names
    char pad_name[32];
    sprintf(pad_name, "sink_%d", source->id);
    GstPad *src_pad = gst_element_get_static_pad(source->clocksync, "src");
    source->video_sink_pad = gst_element_request_pad_simple(app_data.videomixer, pad_name);
    if (source->video_sink_pad) {
        gst_pad_link(src_pad, source->video_sink_pad);
        // Set position for this video instance
        g_object_set(source->video_sink_pad, "xpos", source->xpos, "ypos", source->ypos, NULL);
        
        g_print("Video pad linked successfully\n");
    } else {
        g_print("Failed to get video sink pad from videomixer\n");
    }
    gst_object_unref(src_pad);
    
    // Connect to audiomixer - use unique pad names
    sprintf(pad_name, "sink_%d", source->id);
    src_pad = gst_element_get_static_pad(source->audioresample, "src");
    source->audio_sink_pad = gst_element_request_pad_simple(app_data.audiomixer, pad_name);
    if (source->audio_sink_pad) {
        gst_pad_link(src_pad, source->audio_sink_pad);
        g_print("Audio pad linked successfully\n");
    } else {
        g_print("Failed to get audio sink pad from audiomixer\n");
    }
    gst_object_unref(src_pad);
    
    // Sync all elements with the pipeline state
    gst_element_sync_state_with_parent(source->source);
    gst_element_sync_state_with_parent(source->decodebin);
    gst_element_sync_state_with_parent(source->queue_video);
    gst_element_sync_state_with_parent(source->videoconvert);
    gst_element_sync_state_with_parent(source->videoscale);
    gst_element_sync_state_with_parent(source->capsfilter);
    gst_element_sync_state_with_parent(source->clocksync);
    gst_element_sync_state_with_parent(source->queue_audio);
    gst_element_sync_state_with_parent(source->audioconvert);
    gst_element_sync_state_with_parent(source->audioresample);
    

    
    source->active = TRUE;
    g_print("Source %d added successfully\n", source->id);
    g_print("  Video pad linked: %s\n", source->video_sink_pad ? "YES" : "NO");
    g_print("  Audio pad linked: %s\n", source->audio_sink_pad ? "YES" : "NO");
    

    
    return G_SOURCE_REMOVE;
}

static gboolean remove_source_idle(gpointer user_data) {
    int source_id = GPOINTER_TO_INT(user_data);
    VideoSource *source = NULL;
    GList *iter;
    
    // Find the source
    for (iter = app_data.sources; iter != NULL; iter = iter->next) {
        VideoSource *s = (VideoSource*)iter->data;
        if (s->id == source_id) {
            source = s;
            break;
        }
    }
    
    if (!source || !source->active) {
        g_print("Source %d not found or not active\n", source_id);
        return G_SOURCE_REMOVE;
    }
    
    g_print("Removing source %d\n", source_id);
    
    // Unlink pads
    if (source->video_sink_pad) {
        gst_pad_unlink(gst_element_get_static_pad(source->clocksync, "src"), source->video_sink_pad);
        gst_element_release_request_pad(app_data.videomixer, source->video_sink_pad);
        gst_object_unref(source->video_sink_pad);
        source->video_sink_pad = NULL;
    }
    
    if (source->audio_sink_pad) {
        gst_pad_unlink(gst_element_get_static_pad(source->audioresample, "src"), source->audio_sink_pad);
        gst_element_release_request_pad(app_data.audiomixer, source->audio_sink_pad);
        gst_object_unref(source->audio_sink_pad);
        source->audio_sink_pad = NULL;
    }
    
    // Remove elements from pipeline
    gst_bin_remove_many(GST_BIN(app_data.pipeline), 
                        source->source, source->decodebin, 
                        source->queue_video, source->videoconvert, source->videoscale, source->capsfilter, source->clocksync,
                        source->queue_audio, source->audioconvert, source->audioresample, NULL);
    
    // Free element references
    source->source = NULL;
    source->decodebin = NULL;
    source->queue_video = NULL;
    source->videoconvert = NULL;
    source->videoscale = NULL;
    source->capsfilter = NULL;
    source->clocksync = NULL;
    source->queue_audio = NULL;
    source->audioconvert = NULL;
    source->audioresample = NULL;
    
    source->active = FALSE;
    g_print("Source %d removed successfully\n", source_id);
    
    return G_SOURCE_REMOVE;
}

// Define the move data structure
typedef struct {
    int source_id;
    int xpos;
    int ypos;
} MoveData;

static gboolean move_source_idle(gpointer user_data) {
    MoveData *move_data = (MoveData*)user_data;
    
    VideoSource *source = NULL;
    GList *iter;
    
    // Find the source
    for (iter = app_data.sources; iter != NULL; iter = iter->next) {
        VideoSource *s = (VideoSource*)iter->data;
        if (s->id == move_data->source_id) {
            source = s;
            break;
        }
    }
    
    if (!source || !source->active || !source->video_sink_pad) {
        g_print("Source %d not found, not active, or no video pad\n", move_data->source_id);
        g_free(move_data);
        return G_SOURCE_REMOVE;
    }
    
    g_print("Moving source %d to position (%d, %d)\n", move_data->source_id, move_data->xpos, move_data->ypos);
    
    // Update position
    source->xpos = move_data->xpos;
    source->ypos = move_data->ypos;
    g_object_set(source->video_sink_pad, "xpos", source->xpos, "ypos", source->ypos, NULL);
    
    g_free(move_data);
    return G_SOURCE_REMOVE;
}

// API Functions
int add_video_source(const char *video_file, int xpos, int ypos) {
    VideoSource *source = create_video_source_struct(app_data.next_source_id++, video_file, xpos, ypos);
    app_data.sources = g_list_append(app_data.sources, source);
    
    if (app_data.pipeline_playing) {
        g_idle_add(add_source_idle, source);
    } else {
        add_source_idle(source);
    }
    
    return source->id;
}

void remove_video_source(int source_id) {
    if (app_data.pipeline_playing) {
        g_idle_add(remove_source_idle, GINT_TO_POINTER(source_id));
    } else {
        remove_source_idle(GINT_TO_POINTER(source_id));
    }
}

void move_video_source(int source_id, int xpos, int ypos) {
    MoveData *move_data = g_malloc(sizeof(MoveData));
    move_data->source_id = source_id;
    move_data->xpos = xpos;
    move_data->ypos = ypos;
    
    if (app_data.pipeline_playing) {
        g_idle_add(move_source_idle, move_data);
    } else {
        move_source_idle(move_data);
    }
}

void list_sources() {
    GList *iter;
    g_print("Active sources:\n");
    for (iter = app_data.sources; iter != NULL; iter = iter->next) {
        VideoSource *source = (VideoSource*)iter->data;
        g_print("  Source %d: %s at (%d, %d) - %s\n", 
               source->id, source->video_file, source->xpos, source->ypos,
               source->active ? "ACTIVE" : "INACTIVE");
    }
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
    VideoSource *source = (VideoSource *)data;
    const gchar *media_type = NULL;
    
    // Get the pad template to determine media type
    GstPadTemplate *pad_template = gst_pad_get_pad_template(pad);
    if (pad_template) {
        GstCaps *caps = gst_pad_template_get_caps(pad_template);
        if (caps && gst_caps_get_size(caps) > 0) {
            GstStructure *str = gst_caps_get_structure(caps, 0);
            if (str) {
                media_type = gst_structure_get_name(str);
            }
            gst_caps_unref(caps);
        }
        gst_object_unref(pad_template);
    }
    
    g_print("Pad added: %s\n", GST_PAD_NAME(pad));
    
    // Determine media type from pad name if not available from caps
    if (!media_type) {
        const gchar *pad_name = GST_PAD_NAME(pad);
        if (g_str_has_suffix(pad_name, "_0")) {
            media_type = "video/";
        } else if (g_str_has_suffix(pad_name, "_1")) {
            media_type = "audio/";
        }
    }
    
    if (media_type && g_str_has_prefix(media_type, "video/")) {
        // Connect to video queue
        GstPad *sink_pad = gst_element_get_static_pad(source->queue_video, "sink");
        if (!gst_pad_is_linked(sink_pad)) {
            GstPadLinkReturn link_ret = gst_pad_link(pad, sink_pad);
            if (link_ret == GST_PAD_LINK_OK) {
                g_print("Linked video pad to queue successfully\n");
            } else {
                g_print("Failed to link video pad to queue: %d\n", link_ret);
            }
        } else {
            g_print("Video pad already linked\n");
        }
        gst_object_unref(sink_pad);
    } else if (media_type && g_str_has_prefix(media_type, "audio/")) {
        // Connect to audio queue
        GstPad *sink_pad = gst_element_get_static_pad(source->queue_audio, "sink");
        if (!gst_pad_is_linked(sink_pad)) {
            GstPadLinkReturn link_ret = gst_pad_link(pad, sink_pad);
            if (link_ret == GST_PAD_LINK_OK) {
                g_print("Linked audio pad to queue successfully\n");
            } else {
                g_print("Failed to link audio pad to queue: %d\n", link_ret);
            }
        } else {
            g_print("Audio pad already linked\n");
        }
        gst_object_unref(sink_pad);
    } else {
        g_print("Unknown media type, cannot link pad: %s\n", GST_PAD_NAME(pad));
    }
}

// Simple command interface
void process_command(const char *command) {
    char cmd[256];
    char video_file[256];
    int source_id, xpos, ypos;
    
    if (sscanf(command, "add %s %d %d", video_file, &xpos, &ypos) == 3) {
        int id = add_video_source(video_file, xpos, ypos);
        g_print("Added source %d\n", id);
    }
    else if (sscanf(command, "remove %d", &source_id) == 1) {
        remove_video_source(source_id);
        g_print("Removed source %d\n", source_id);
    }
    else if (sscanf(command, "move %d %d %d", &source_id, &xpos, &ypos) == 3) {
        move_video_source(source_id, xpos, ypos);
        g_print("Moved source %d to (%d, %d)\n", source_id, xpos, ypos);
    }
    else if (strcmp(command, "list") == 0) {
        list_sources();
    }
    else if (strcmp(command, "help") == 0) {
        g_print("Available commands:\n");
        g_print("  add <video_file> <xpos> <ypos> - Add a video source\n");
        g_print("  remove <source_id> - Remove a video source\n");
        g_print("  move <source_id> <xpos> <ypos> - Move a video source\n");
        g_print("  list - List all sources\n");
        g_print("  help - Show this help\n");
        g_print("  quit - Exit the application\n");
    }
    else if (strcmp(command, "quit") == 0) {
        g_main_loop_quit(app_data.loop);
    }
    else {
        g_print("Unknown command. Type 'help' for available commands.\n");
    }
}

int main(int argc, char *argv[]) {
    GstBus *bus;
    char command[256];
    
    // Initialize GStreamer
    gst_init(&argc, &argv);
    
    // Initialize app data
    memset(&app_data, 0, sizeof(AppData));
    app_data.next_source_id = 0;
    
    // Create main pipeline
    app_data.pipeline = gst_pipeline_new("video-compositor-pipeline");
    
    // Create videomixer element
    app_data.videomixer = gst_element_factory_make("videomixer", "videomixer");
    if (!app_data.videomixer) {
        g_print("Failed to create videomixer element\n");
        return -1;
    }
    
    // Set videomixer output format
    g_object_set(app_data.videomixer, "background", 1, NULL); // Black background
    
    // Create audiomixer element
    app_data.audiomixer = gst_element_factory_make("audiomixer", "audiomixer");
    if (!app_data.audiomixer) {
        g_print("Failed to create audiomixer element\n");
        return -1;
    }
    
    // Create video sink for live display - try different sinks
    app_data.video_sink = gst_element_factory_make("xvimagesink", "video_sink");
    if (!app_data.video_sink) {
        g_print("Failed to create xvimagesink, trying ximagesink\n");
        app_data.video_sink = gst_element_factory_make("ximagesink", "video_sink");
    }
    if (!app_data.video_sink) {
        g_print("Failed to create ximagesink, trying autovideosink\n");
        app_data.video_sink = gst_element_factory_make("autovideosink", "video_sink");
    }
    if (!app_data.video_sink) {
        g_print("Failed to create any video sink\n");
        return -1;
    }
    g_print("Created video sink: %s\n", GST_OBJECT_NAME(app_data.video_sink));
    
    // Create capsfilter for videomixer output
    GstElement *mixer_caps = gst_element_factory_make("capsfilter", "mixer_caps");
    if (!mixer_caps) {
        g_print("Failed to create mixer capsfilter\n");
        return -1;
    }
    
    // Set output caps for videomixer
    GstCaps *output_caps = gst_caps_new_simple("video/x-raw",
                                              "width", G_TYPE_INT, 1280,
                                              "height", G_TYPE_INT, 720,
                                              NULL);
    g_object_set(mixer_caps, "caps", output_caps, NULL);
    gst_caps_unref(output_caps);
    
    // Add debug output for pipeline state
    g_print("Pipeline elements created:\n");
    g_print("  Videomixer: %s\n", app_data.videomixer ? "OK" : "FAILED");
    g_print("  Audiomixer: %s\n", app_data.audiomixer ? "OK" : "FAILED");
    g_print("  Video sink: %s\n", app_data.video_sink ? "OK" : "FAILED");
    g_print("  Audio sink: %s\n", app_data.audio_sink ? "OK" : "FAILED");
    
    // Create audio sink
    app_data.audio_sink = gst_element_factory_make("autoaudiosink", "audio_sink");
    if (!app_data.audio_sink) {
        g_print("Warning: Failed to create autoaudiosink element, continuing without audio\n");
        app_data.audio_sink = NULL;
    }
    
    // Add main elements to pipeline
    if (app_data.audio_sink) {
        gst_bin_add_many(GST_BIN(app_data.pipeline), app_data.videomixer, mixer_caps, app_data.audiomixer, 
                         app_data.video_sink, app_data.audio_sink, NULL);
        gst_element_link(app_data.audiomixer, app_data.audio_sink);
    } else {
        gst_bin_add_many(GST_BIN(app_data.pipeline), app_data.videomixer, mixer_caps, app_data.audiomixer, 
                         app_data.video_sink, NULL);
    }
    
    // Link main elements
    gst_element_link(app_data.videomixer, mixer_caps);
    gst_element_link(mixer_caps, app_data.video_sink);
    
    // Test pattern removed - not needed anymore
    
    // Set up bus monitoring
    bus = gst_element_get_bus(app_data.pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)on_bus_message, &app_data);
    gst_object_unref(bus);
    
    // Create main loop
    app_data.loop = g_main_loop_new(NULL, FALSE);
    
    g_print("Setting pipeline to playing state...\n");
    // Set pipeline to playing state
    GstStateChangeReturn ret = gst_element_set_state(app_data.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        g_print("Failed to set pipeline to playing state\n");
        return -1;
    }
    g_print("Pipeline set to playing state successfully\n");
    
    // Wait a bit for pipeline to stabilize
    g_usleep(100000); // 100ms
    
    // Add initial sources if provided
    for (int i = 1; i < argc; i++) {
        int xpos = ((i-1) % 4) * 320;
        int ypos = ((i-1) / 4) * 240;
        add_video_source(argv[i], xpos, ypos);
    }
    
    g_print("Video compositor ready! Type 'help' for commands.\n");
    
    // Simple command interface
    while (1) {
        g_print("> ");
        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;
        }
        
        // Remove newline
        command[strcspn(command, "\n")] = 0;
        
        if (strlen(command) > 0) {
            process_command(command);
        }
    }
    
    // Clean up
    gst_element_set_state(app_data.pipeline, GST_STATE_NULL);
    gst_object_unref(app_data.pipeline);
    g_main_loop_unref(app_data.loop);
    
    // Free sources
    g_list_free_full(app_data.sources, (GDestroyNotify)free_video_source);
    
    g_print("Video compositing completed.\n");
    
    return 0;
} 