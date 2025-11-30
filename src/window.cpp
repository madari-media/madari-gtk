#include "window.hpp"
#include "detail_view.hpp"
#include "stremio/stremio.hpp"
#include "trakt/trakt.hpp"
#include "watch_history.hpp"
#include <libsoup/soup.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

struct _MadariWindow {
    AdwApplicationWindow parent_instance;

    // UI widgets
    AdwNavigationView *navigation_view;
    AdwHeaderBar *header_bar;
    GtkStack *root_stack;           // Top-level stack: browse vs player
    GtkStack *main_stack;           // Content stack: empty, loading, content
    GtkBox *catalogs_box;
    GtkSpinner *loading_spinner;
    GtkToggleButton *search_button;
    GtkSearchBar *search_bar;
    GtkSearchEntry *search_entry;
    GtkScrolledWindow *content_scroll;
    
    // Filter buttons
    GtkToggleButton *filter_all;
    GtkToggleButton *filter_movies;
    GtkToggleButton *filter_series;
    GtkToggleButton *filter_channels;
    
    // Player widgets
    GtkWidget *player_page;
    GtkOverlay *player_overlay;
    GtkGLArea *video_area;
    GtkRevealer *player_controls_revealer;
    GtkRevealer *player_header_revealer;
    GtkScale *player_progress;
    GtkLabel *player_time_label;
    GtkLabel *player_duration_label;
    GtkButton *player_play_btn;
    GtkButton *player_back_btn;
    GtkLabel *player_title_label;
    GtkMenuButton *audio_track_btn;
    GtkMenuButton *subtitle_track_btn;
    GtkButton *player_fullscreen_btn;
    GtkButton *player_episodes_btn;
    GtkButton *player_mute_btn;
    GtkScale *player_volume;
    GtkWidget *player_loading;
    gboolean player_is_muted;
    double player_volume_before_mute;
    
    // MPV
    mpv_handle *mpv;
    mpv_render_context *mpv_gl;
    
    // Player state
    gboolean player_is_playing;
    gboolean player_is_fullscreen;
    gboolean player_seeking;
    double player_duration;
    double player_position;
    guint player_hide_controls_id;
    guint inhibit_cookie;  // For preventing system sleep during playback
    std::string *player_current_title;
    std::vector<std::pair<int, std::string>> *audio_tracks;
    std::vector<std::pair<int, std::string>> *subtitle_tracks;
    
    // Series episode context
    std::string *current_meta_id;
    std::string *current_meta_type;
    std::string *current_video_id;  // Current episode video ID
    std::string *current_binge_group;  // For auto-selecting same quality stream
    std::string *current_series_title;  // Series name for title formatting
    int current_season;  // Current season number
    
    // Episode list for navigation - stores (video_id, title, episode_number)
    struct EpisodeInfo {
        std::string video_id;
        std::string title;
        int episode;
    };
    std::vector<EpisodeInfo> *episode_list;
    int current_episode_index;
    
    // Next/Previous episode buttons
    GtkButton *player_prev_btn;
    GtkButton *player_next_btn;
    
    // Enhanced player controls
    GtkButton *player_skip_back_btn;    // -10s button
    GtkButton *player_skip_fwd_btn;     // +10s button
    GtkButton *player_screenshot_btn;   // Screenshot
    GtkButton *player_loop_btn;         // Loop toggle
    GtkButton *player_ontop_btn;        // Always on top
    GtkMenuButton *player_settings_btn; // Settings menu
    
    // Enhanced player state
    double player_speed;                // Current playback speed (1.0 = normal)
    int player_aspect_mode;             // 0=fit, 1=fill, 2=16:9, 3=4:3
    gboolean player_loop;               // Loop current video
    gboolean player_always_on_top;      // Always on top
    gboolean player_show_remaining;     // Show remaining time instead of duration
    double player_brightness;           // -100 to 100
    double player_contrast;             // -100 to 100
    
    // Application reference
    MadariApplication *app;
    
    // HTTP session for image loading
    SoupSession *soup_session;
    
    // Track pending catalog loads
    int pending_catalogs;
    
    // Current filter
    std::string *current_filter;
    
    // Search state
    std::string *current_search_query;
    gboolean is_searching;
    
    // Watch history tracking
    std::string *current_poster_url;    // Poster URL for watch history
    int current_episode_number;          // Current episode number for watch history
    guint history_save_timeout_id;       // Timer for periodic history saves
    gboolean history_needs_save;         // Flag to indicate pending save
    
    // Trakt scrobbling state
    gboolean scrobble_started;           // Has scrobble_start been sent for current playback?
    int64_t last_scrobble_time;          // Unix timestamp of last scrobble call (for debouncing)
};

G_DEFINE_TYPE(MadariWindow, madari_window, ADW_TYPE_APPLICATION_WINDOW)

// Forward declarations
static void load_catalogs(MadariWindow *self);
static void clear_catalogs_box(MadariWindow *self);
static GtkWidget* create_catalog_section(const std::string& title, const std::string& addon_id, 
                                          const std::string& catalog_id, const std::string& type);
static void load_catalog_content(MadariWindow *self, GtkBox *items_box,
                                  const std::string& addon_id, const std::string& type,
                                  const std::string& catalog_id);
static GtkWidget* create_poster_item(const Stremio::MetaPreview& meta);

// ============ Trakt Scrobbling ============

/**
 * Trigger a scrobble action (start, pause, stop)
 * This function is debounced to avoid excessive API calls
 * 
 * @param self Window instance
 * @param action "start", "pause", or "stop"
 */
static void trigger_scrobble(MadariWindow *self, const char* action) {
    // Check if we have valid video context
    if (!self->current_video_id || !self->current_meta_type) {
        return;
    }
    
    // Check if Trakt is enabled and configured
    Trakt::TraktService *trakt = madari_application_get_trakt_service(self->app);
    if (!trakt || !trakt->is_authenticated() || !trakt->get_config().sync_progress) {
        return;
    }
    
    // Debounce: Don't scrobble more than once per 5 seconds for start/pause
    // For stop, always allow to ensure progress is saved
    int64_t now = g_get_real_time() / 1000000;  // Convert microseconds to seconds
    if (strcmp(action, "stop") != 0 && now - self->last_scrobble_time < 5) {
        return;
    }
    self->last_scrobble_time = now;
    
    // Parse the video ID to get content IDs
    Trakt::ContentIds ids = Trakt::parse_stremio_id(*self->current_video_id);
    if (!ids.has_id()) {
        g_warning("[Trakt] Cannot scrobble: No valid ID found in video_id: %s", 
                  self->current_video_id->c_str());
        return;
    }
    
    // Calculate progress percentage (0-100)
    double progress = 0.0;
    if (self->player_duration > 0) {
        progress = (self->player_position / self->player_duration) * 100.0;
        progress = std::min(100.0, std::max(0.0, progress));
    }
    
    // Get content type - use meta_type to determine movie vs series
    std::string content_type = *self->current_meta_type;
    
    // Call appropriate scrobble method
    if (strcmp(action, "start") == 0) {
        trakt->scrobble_start(content_type, ids, progress, 
            [](bool success, const std::string& error) {
                // Silent callback - errors are logged in the service
            });
    } else if (strcmp(action, "pause") == 0) {
        trakt->scrobble_pause(content_type, ids, progress,
            [](bool success, const std::string& error) {
                // Silent callback
            });
    } else if (strcmp(action, "stop") == 0) {
        trakt->scrobble_stop(content_type, ids, progress,
            [](bool success, const std::string& error) {
                // Silent callback
            });
    }
}

static void clear_catalogs_box(MadariWindow *self) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self->catalogs_box))) != nullptr) {
        gtk_box_remove(self->catalogs_box, child);
    }
}

// Global soup session for image loading with limited concurrent requests
static SoupSession* get_image_session() {
    static SoupSession *session = nullptr;
    if (!session) {
        // max-conns and max-conns-per-host are construct-only properties
        session = SOUP_SESSION(g_object_new(SOUP_TYPE_SESSION,
                     "timeout", 30,
                     "max-conns", 8,
                     "max-conns-per-host", 4,
                     nullptr));
    }
    return session;
}

static void do_load_image(GtkPicture *picture, const char *url) {
    SoupMessage *msg = soup_message_new("GET", url);
    if (!msg) return;
    
    // prevent picture from being destroyed while loading
    g_object_ref(picture);
    
    soup_session_send_and_read_async(
        get_image_session(),
        msg,
        G_PRIORITY_LOW,  // Use low priority to not block UI
        nullptr,
        [](GObject *source, GAsyncResult *result, gpointer user_data) {
            GtkPicture *picture = GTK_PICTURE(user_data);
            g_autoptr(GError) error = nullptr;
            
            GBytes *bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
            
            if (bytes && !error) {
                gsize size;
                const guchar *data = static_cast<const guchar*>(g_bytes_get_data(bytes, &size));
                
                if (data && size > 0) {
                    g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_data(
                        g_memdup2(data, size), size, g_free);
                    
                    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(
                        stream, 160, 240, TRUE, nullptr, nullptr);
                    
                    if (pixbuf) {
                        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
                        GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
                        G_GNUC_END_IGNORE_DEPRECATIONS
                        gtk_picture_set_paintable(picture, GDK_PAINTABLE(texture));
                        g_object_unref(texture);
                        g_object_unref(pixbuf);
                    }
                }
                g_bytes_unref(bytes);
            }
            
            g_object_unref(picture);
        },
        picture
    );
    
    g_object_unref(msg);
}

// Lazy load - only load when widget becomes visible
static void on_picture_map(GtkWidget *widget, [[maybe_unused]] gpointer user_data) {
    const char *url = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "image-url"));
    gboolean loaded = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "image-loaded"));
    
    if (url && !loaded) {
        g_object_set_data(G_OBJECT(widget), "image-loaded", GINT_TO_POINTER(TRUE));
        // Small delay to batch requests and prevent UI stutter
        g_timeout_add(50, [](gpointer data) -> gboolean {
            GtkWidget *widget = GTK_WIDGET(data);
            if (!gtk_widget_get_mapped(widget)) {
                g_object_unref(widget);
                return G_SOURCE_REMOVE;
            }
            const char *url = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "image-url"));
            if (url) {
                do_load_image(GTK_PICTURE(widget), url);
            }
            g_object_unref(widget);
            return G_SOURCE_REMOVE;
        }, g_object_ref(widget));
    }
}

static void load_image_async(GtkPicture *picture, const std::string& url) {
    // Store URL and connect to map signal for lazy loading
    char *url_copy = g_strdup(url.c_str());
    g_object_set_data_full(G_OBJECT(picture), "image-url", url_copy, g_free);
    g_object_set_data(G_OBJECT(picture), "image-loaded", GINT_TO_POINTER(FALSE));
    
    // If widget is already mapped, load immediately
    if (gtk_widget_get_mapped(GTK_WIDGET(picture))) {
        g_object_set_data(G_OBJECT(picture), "image-loaded", GINT_TO_POINTER(TRUE));
        do_load_image(picture, url.c_str());
    } else {
        // Otherwise, wait for map signal
        g_signal_connect(picture, "map", G_CALLBACK(on_picture_map), nullptr);
    }
}

static GtkWidget* create_poster_item(const Stremio::MetaPreview& meta) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(box, 160, -1);
    
    // Frame with rounded corners
    GtkWidget *frame = gtk_frame_new(nullptr);
    gtk_widget_add_css_class(frame, "card");
    gtk_widget_set_overflow(frame, GTK_OVERFLOW_HIDDEN);  // Clip children to rounded corners
    gtk_widget_set_size_request(frame, 160, 240);
    
    // Overlay for placeholder + image
    GtkWidget *overlay = gtk_overlay_new();
    
    // Background placeholder
    GtkWidget *placeholder_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(placeholder_box, 160, 240);
    
    GtkWidget *placeholder_icon = gtk_image_new_from_icon_name("video-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(placeholder_icon), 48);
    gtk_widget_add_css_class(placeholder_icon, "dim-label");
    gtk_widget_set_valign(placeholder_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(placeholder_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(placeholder_icon, TRUE);
    gtk_box_append(GTK_BOX(placeholder_box), placeholder_icon);
    
    gtk_overlay_set_child(GTK_OVERLAY(overlay), placeholder_box);
    
    // Actual poster image (loads over placeholder)
    if (meta.poster.has_value() && !meta.poster->empty()) {
        GtkWidget *picture = gtk_picture_new();
        gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
        gtk_widget_set_size_request(picture, 160, 240);
        
        // Load image asynchronously with lazy loading
        load_image_async(GTK_PICTURE(picture), *meta.poster);
        
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), picture);
    }
    
    gtk_frame_set_child(GTK_FRAME(frame), overlay);
    gtk_box_append(GTK_BOX(box), frame);
    
    // Title
    GtkWidget *title_label = gtk_label_new(meta.name.c_str());
    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 16);
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines(GTK_LABEL(title_label), 1);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(title_label, "caption");
    gtk_box_append(GTK_BOX(box), title_label);
    
    // Year/rating info
    std::string info;
    if (meta.release_info.has_value() && !meta.release_info->empty()) {
        info = *meta.release_info;
    }
    if (meta.imdb_rating.has_value() && !meta.imdb_rating->empty()) {
        if (!info.empty()) info += " • ";
        info += "★ " + *meta.imdb_rating;
    }
    if (!info.empty()) {
        GtkWidget *info_label = gtk_label_new(info.c_str());
        gtk_widget_add_css_class(info_label, "dim-label");
        gtk_widget_add_css_class(info_label, "caption");
        gtk_label_set_ellipsize(GTK_LABEL(info_label), PANGO_ELLIPSIZE_END);
        gtk_widget_set_halign(info_label, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(box), info_label);
    }
    
    // Store metadata for click handling
    std::string *meta_id = new std::string(meta.id);
    std::string *meta_type = new std::string(meta.type);
    g_object_set_data_full(G_OBJECT(box), "meta-id", meta_id,
                           [](gpointer data) { delete static_cast<std::string*>(data); });
    g_object_set_data_full(G_OBJECT(box), "meta-type", meta_type,
                           [](gpointer data) { delete static_cast<std::string*>(data); });
    
    // Make clickable
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer user_data) {
        GtkWidget *box = GTK_WIDGET(user_data);
        const std::string *meta_id = static_cast<const std::string*>(g_object_get_data(G_OBJECT(box), "meta-id"));
        const std::string *meta_type = static_cast<const std::string*>(g_object_get_data(G_OBJECT(box), "meta-type"));
        
        if (meta_id && meta_type) {
            // Find the window
            GtkWidget *widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
            GtkRoot *root = gtk_widget_get_root(widget);
            if (MADARI_IS_WINDOW(root)) {
                madari_window_show_detail(MADARI_WINDOW(root), meta_id->c_str(), meta_type->c_str());
            }
        }
    }), box);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));
    
    // Hover effect
    gtk_widget_set_cursor_from_name(box, "pointer");
    
    return box;
}

static GtkWidget* create_catalog_section(const std::string& title, 
                                          [[maybe_unused]] const std::string& addon_id,
                                          [[maybe_unused]] const std::string& catalog_id, 
                                          [[maybe_unused]] const std::string& type) {
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    
    // Header with title
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    GtkWidget *title_label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_label, "title-3");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(header), title_label);
    
    // "See All" button (for future navigation)
    GtkWidget *see_all = gtk_button_new_with_label("See All");
    gtk_widget_add_css_class(see_all, "flat");
    gtk_box_append(GTK_BOX(header), see_all);
    
    gtk_box_append(GTK_BOX(section), header);
    
    // Horizontal scroll for posters - Netflix style
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(scroll, -1, 310);
    
    // Enable kinetic scrolling for smooth scrolling
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll), TRUE);
    
    // Use a simple horizontal box instead of flow box for Netflix-style scrolling
    GtkWidget *items_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_end(items_box, 24);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), items_box);
    gtk_box_append(GTK_BOX(section), scroll);
    
    // Store items_box reference for loading content
    g_object_set_data(G_OBJECT(section), "items-box", items_box);
    
    // Add loading spinner initially
    GtkWidget *spinner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spinner_box, 150, 225);
    gtk_widget_set_halign(spinner_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(spinner_box, GTK_ALIGN_CENTER);
    
    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(spinner_box), spinner);
    gtk_box_append(GTK_BOX(items_box), spinner_box);
    
    return section;
}

static void load_catalog_content(MadariWindow *self, GtkBox *items_box,
                                  const std::string& addon_id, const std::string& type,
                                  const std::string& catalog_id) {
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (!service) return;
    
    Stremio::ExtraArgs extra;
    
    // Store references for the callback
    struct LoadData {
        MadariWindow *window;
        GtkBox *items_box;
    };
    LoadData *data = new LoadData{self, items_box};
    
    service->fetch_catalog(addon_id, type, catalog_id, extra,
        [data](std::optional<Stremio::CatalogResponse> response, const std::string& error) {
            // Clear the loading spinner
            GtkWidget *child;
            while ((child = gtk_widget_get_first_child(GTK_WIDGET(data->items_box))) != nullptr) {
                gtk_box_remove(data->items_box, child);
            }
            
            if (response && !response->metas.empty()) {
                // Add poster items (limit to first 25 for performance)
                int count = 0;
                for (const auto& meta : response->metas) {
                    if (count >= 25) break;
                    GtkWidget *item = create_poster_item(meta);
                    gtk_box_append(data->items_box, item);
                    count++;
                }
            } else {
                // Show error or empty state
                GtkWidget *label = gtk_label_new(error.empty() ? "No content available" : error.c_str());
                gtk_widget_add_css_class(label, "dim-label");
                gtk_widget_set_margin_start(label, 24);
                gtk_box_append(data->items_box, label);
            }
            
            data->window->pending_catalogs--;
            delete data;
        });
}

// Forward declarations
static void show_resume_dialog(MadariWindow *self, const Madari::WatchHistoryEntry& entry);
static void fetch_poster_for_entry(MadariWindow *self, const Madari::WatchHistoryEntry& entry, GtkPicture *picture);

static GtkWidget* create_continue_watching_item(MadariWindow *self, const Madari::WatchHistoryEntry& entry) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(box, 160, -1);
    
    // Frame with rounded corners
    GtkWidget *frame = gtk_frame_new(nullptr);
    gtk_widget_add_css_class(frame, "card");
    gtk_widget_set_overflow(frame, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_size_request(frame, 160, 240);
    
    // Overlay for image + progress bar
    GtkWidget *overlay = gtk_overlay_new();
    
    // Background placeholder
    GtkWidget *placeholder_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_size_request(placeholder_box, 160, 240);
    
    GtkWidget *placeholder_icon = gtk_image_new_from_icon_name("video-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(placeholder_icon), 48);
    gtk_widget_add_css_class(placeholder_icon, "dim-label");
    gtk_widget_set_valign(placeholder_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(placeholder_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(placeholder_icon, TRUE);
    gtk_box_append(GTK_BOX(placeholder_box), placeholder_icon);
    
    gtk_overlay_set_child(GTK_OVERLAY(overlay), placeholder_box);
    
    // Poster image
    GtkWidget *picture = gtk_picture_new();
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_COVER);
    gtk_widget_set_size_request(picture, 160, 240);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), picture);
    
    if (!entry.poster_url.empty()) {
        // Load from cached URL
        load_image_async(GTK_PICTURE(picture), entry.poster_url);
    } else if (!entry.meta_id.empty()) {
        // Fetch poster from Stremio for items without cached poster (e.g., Trakt items)
        fetch_poster_for_entry(self, entry, GTK_PICTURE(picture));
    }
    
    // Progress bar at bottom
    GtkWidget *progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(progress_box, GTK_ALIGN_END);
    gtk_widget_set_hexpand(progress_box, TRUE);
    
    GtkWidget *progress = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress), entry.get_progress());
    gtk_widget_add_css_class(progress, "osd");
    gtk_box_append(GTK_BOX(progress_box), progress);
    
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), progress_box);
    
    // Play icon overlay (centered)
    GtkWidget *play_icon_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(play_icon_box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(play_icon_box, GTK_ALIGN_CENTER);
    
    GtkWidget *play_icon = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(play_icon), 48);
    gtk_widget_add_css_class(play_icon, "osd");
    gtk_widget_set_opacity(play_icon, 0.8);
    gtk_box_append(GTK_BOX(play_icon_box), play_icon);
    
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), play_icon_box);
    
    gtk_frame_set_child(GTK_FRAME(frame), overlay);
    gtk_box_append(GTK_BOX(box), frame);
    
    // Title
    std::string display_title = entry.title;
    // For series, show series title + episode info
    if (entry.series_title.has_value() && entry.meta_type == "series") {
        display_title = *entry.series_title;
    }
    
    GtkWidget *title_label = gtk_label_new(display_title.c_str());
    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 16);
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines(GTK_LABEL(title_label), 1);
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_add_css_class(title_label, "caption");
    gtk_box_append(GTK_BOX(box), title_label);
    
    // Episode info or remaining time
    std::string info_text;
    if (entry.meta_type == "series" && entry.season.has_value() && entry.episode.has_value()) {
        info_text = "S" + std::to_string(*entry.season) + "E" + std::to_string(*entry.episode);
        info_text += " • " + entry.get_remaining_string();
    } else {
        info_text = entry.get_remaining_string();
    }
    
    GtkWidget *info_label = gtk_label_new(info_text.c_str());
    gtk_widget_add_css_class(info_label, "dim-label");
    gtk_widget_add_css_class(info_label, "caption");
    gtk_label_set_ellipsize(GTK_LABEL(info_label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_halign(info_label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(box), info_label);
    
    // Store entry data for click handling
    Madari::WatchHistoryEntry *entry_copy = new Madari::WatchHistoryEntry(entry);
    g_object_set_data_full(G_OBJECT(box), "history-entry", entry_copy,
                           [](gpointer data) { delete static_cast<Madari::WatchHistoryEntry*>(data); });
    
    // Make clickable - show resume dialog
    GtkGesture *click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
    g_signal_connect(click, "pressed", G_CALLBACK(+[](GtkGestureClick *gesture, gint, gdouble, gdouble, gpointer user_data) {
        GtkWidget *box = GTK_WIDGET(user_data);
        Madari::WatchHistoryEntry *entry = static_cast<Madari::WatchHistoryEntry*>(
            g_object_get_data(G_OBJECT(box), "history-entry"));
        
        if (entry) {
            GtkWidget *widget = GTK_WIDGET(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture)));
            GtkRoot *root = gtk_widget_get_root(widget);
            if (MADARI_IS_WINDOW(root)) {
                show_resume_dialog(MADARI_WINDOW(root), *entry);
            }
        }
    }), box);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(click));
    
    gtk_widget_set_cursor_from_name(box, "pointer");
    
    return box;
}

// Parse ISO 8601 timestamp to Unix timestamp
static int64_t parse_iso8601(const std::string& timestamp) {
    if (timestamp.empty()) return 0;
    
    struct tm tm = {};
    // Format: 2024-01-15T10:30:00.000Z
    if (strptime(timestamp.c_str(), "%Y-%m-%dT%H:%M:%S", &tm) != nullptr) {
        return timegm(&tm);
    }
    return 0;
}

// Helper to convert Trakt PlaybackProgress to WatchHistoryEntry
static Madari::WatchHistoryEntry trakt_playback_to_entry(const Trakt::PlaybackProgress& playback) {
    Madari::WatchHistoryEntry entry;
    
    // Calculate position and duration from progress percentage
    // Progress is 0-100, we use 100 as a normalized duration
    entry.duration = 100.0;
    entry.position = playback.progress;
    
    // Parse paused_at timestamp for proper sorting
    entry.last_watched = parse_iso8601(playback.paused_at);
    if (entry.last_watched == 0) {
        entry.last_watched = std::time(nullptr);
    }
    
    if (playback.type == "movie" && playback.movie.has_value()) {
        const auto& movie = *playback.movie;
        entry.meta_id = movie.ids.imdb.value_or("");
        entry.video_id = entry.meta_id;  // For movies, video_id = meta_id
        entry.meta_type = "movie";
        entry.title = movie.title;
        entry.poster_url = "";  // Will be fetched via Stremio
    } else if (playback.type == "episode" && playback.episode.has_value()) {
        const auto& ep = *playback.episode;
        entry.meta_type = "series";
        entry.title = ep.title;
        entry.season = ep.season;
        entry.episode = ep.number;
        
        if (playback.show.has_value()) {
            const auto& show = *playback.show;
            entry.meta_id = show.ids.imdb.value_or("");
            entry.series_title = show.title;
        }
        
        // For episodes, video_id format is {imdb}:{season}:{episode}
        // This is the Stremio standard format for episode streams
        entry.video_id = entry.meta_id + ":" + std::to_string(ep.season) + ":" + std::to_string(ep.number);
        entry.poster_url = "";  // Will be fetched via Stremio
    }
    
    return entry;
}

// Async helper to fetch poster for an entry and update the widget
static void fetch_poster_for_entry(MadariWindow *self, const Madari::WatchHistoryEntry& entry, GtkPicture *picture) {
    if (!entry.meta_id.empty()) {
        Stremio::AddonService *addon_service = madari_application_get_addon_service(self->app);
        if (addon_service) {
            // Add ref to picture to keep it alive during async fetch
            g_object_ref(picture);
            
            std::string meta_type = entry.meta_type;
            std::string meta_id = entry.meta_id;
            
            // Fetch meta to get poster
            addon_service->fetch_meta(meta_type, meta_id,
                [picture](std::optional<Stremio::MetaResponse> response, const std::string& error) {
                    if (!error.empty() || !response) {
                        g_object_unref(picture);
                        return;
                    }
                    if (!GTK_IS_PICTURE(picture)) {
                        g_object_unref(picture);
                        return;
                    }
                    
                    const auto& meta = response->meta;
                    if (meta.poster.has_value() && !meta.poster->empty()) {
                        load_image_async(picture, *meta.poster);
                    }
                    g_object_unref(picture);
                });
        }
    }
}

static GtkWidget* create_continue_watching_section(MadariWindow *self) {
    Madari::WatchHistoryService *history = madari_application_get_watch_history(self->app);
    
    // Get local items
    std::vector<Madari::WatchHistoryEntry> local_items;
    if (history) {
        local_items = history->get_continue_watching(50);  // Get more to merge with Trakt
    }
    
    // Check if Trakt is available
    Trakt::TraktService *trakt = madari_application_get_trakt_service(self->app);
    bool trakt_available = trakt && trakt->is_authenticated() && trakt->get_config().sync_progress;
    
    // If no local items and no Trakt, return null
    if (local_items.empty() && !trakt_available) {
        return nullptr;
    }
    
    // Create section
    GtkWidget *section = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    g_object_set_data(G_OBJECT(section), "window", self);
    
    // Header
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    GtkWidget *title_label = gtk_label_new("Continue Watching");
    gtk_widget_add_css_class(title_label, "title-3");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(header), title_label);
    
    gtk_box_append(GTK_BOX(section), header);
    
    // Horizontal scroll for items
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(scroll, -1, 310);
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(scroll), TRUE);
    
    GtkWidget *items_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_end(items_box, 24);
    g_object_set_data(G_OBJECT(section), "items-box", items_box);
    g_object_set_data(G_OBJECT(section), "local-items", new std::vector<Madari::WatchHistoryEntry>(local_items));
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), items_box);
    gtk_box_append(GTK_BOX(section), scroll);
    
    // If Trakt is available, fetch playback and merge with local items
    if (trakt_available) {
        g_object_ref(section);
        
        trakt->get_playback([section](std::optional<std::vector<Trakt::PlaybackProgress>> playback, 
                                       const std::string& error) {
            if (!GTK_IS_WIDGET(section)) {
                g_object_unref(section);
                return;
            }
            
            MadariWindow *win = static_cast<MadariWindow*>(g_object_get_data(G_OBJECT(section), "window"));
            GtkWidget *items_box = static_cast<GtkWidget*>(g_object_get_data(G_OBJECT(section), "items-box"));
            auto *local_items_ptr = static_cast<std::vector<Madari::WatchHistoryEntry>*>(
                g_object_get_data(G_OBJECT(section), "local-items"));
            
            if (!GTK_IS_BOX(items_box) || !MADARI_IS_WINDOW(win)) {
                if (local_items_ptr) delete local_items_ptr;
                g_object_unref(section);
                return;
            }
            
            // Build merged list
            std::vector<Madari::WatchHistoryEntry> merged;
            
            // Add local items
            if (local_items_ptr) {
                for (const auto& entry : *local_items_ptr) {
                    merged.push_back(entry);
                }
                delete local_items_ptr;
                g_object_set_data(G_OBJECT(section), "local-items", nullptr);
            }
            
            // Add Trakt items (convert and merge)
            if (!error.empty()) {
                g_warning("[Trakt] Failed to fetch playback: %s", error.c_str());
            } else if (playback && !playback->empty()) {
                // Create a set of existing video_ids to avoid duplicates
                std::set<std::string> existing_ids;
                for (const auto& entry : merged) {
                    existing_ids.insert(entry.video_id);
                }
                
                for (const auto& item : *playback) {
                    Madari::WatchHistoryEntry entry = trakt_playback_to_entry(item);
                    // Only add if not already in local history
                    if (existing_ids.find(entry.video_id) == existing_ids.end()) {
                        merged.push_back(entry);
                        existing_ids.insert(entry.video_id);
                    }
                }
            }
            
            // Sort by last_watched (most recent first)
            std::sort(merged.begin(), merged.end(), 
                      [](const Madari::WatchHistoryEntry& a, const Madari::WatchHistoryEntry& b) {
                          return a.last_watched > b.last_watched;
                      });
            
            // Clear existing items
            GtkWidget *child = gtk_widget_get_first_child(items_box);
            while (child) {
                GtkWidget *next = gtk_widget_get_next_sibling(child);
                gtk_box_remove(GTK_BOX(items_box), child);
                child = next;
            }
            
            // Add sorted items (limit to 15)
            int count = 0;
            for (const auto& entry : merged) {
                if (count >= 15) break;
                GtkWidget *item = create_continue_watching_item(win, entry);
                gtk_box_append(GTK_BOX(items_box), item);
                count++;
            }
            
            g_object_unref(section);
        });
    } else {
        // No Trakt - just show local items sorted
        std::sort(local_items.begin(), local_items.end(), 
                  [](const Madari::WatchHistoryEntry& a, const Madari::WatchHistoryEntry& b) {
                      return a.last_watched > b.last_watched;
                  });
        
        int count = 0;
        for (const auto& entry : local_items) {
            if (count >= 15) break;
            GtkWidget *item = create_continue_watching_item(self, entry);
            gtk_box_append(GTK_BOX(items_box), item);
            count++;
        }
        
        // Clean up local items data
        g_object_set_data(G_OBJECT(section), "local-items", nullptr);
    }
    
    return section;
}

static void load_catalogs(MadariWindow *self) {
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (!service) {
        gtk_stack_set_visible_child_name(self->main_stack, "empty");
        return;
    }
    
    std::vector<std::pair<Stremio::Manifest, Stremio::CatalogDefinition>> catalogs;
    
    // Get catalogs based on current filter
    if (self->current_filter && !self->current_filter->empty()) {
        catalogs = service->get_catalogs_by_type(*self->current_filter);
    } else {
        catalogs = service->get_all_catalogs();
    }
    
    if (catalogs.empty()) {
        // Check if we have any addons at all
        if (service->get_installed_addons().empty()) {
            gtk_stack_set_visible_child_name(self->main_stack, "empty");
        } else {
            // Show content but with no results
            clear_catalogs_box(self);
            GtkWidget *label = gtk_label_new("No catalogs available for this filter");
            gtk_widget_add_css_class(label, "dim-label");
            gtk_widget_add_css_class(label, "title-2");
            gtk_widget_set_margin_top(label, 48);
            gtk_box_append(self->catalogs_box, label);
            gtk_stack_set_visible_child_name(self->main_stack, "content");
        }
        return;
    }
    
    // Clear existing catalogs
    clear_catalogs_box(self);
    
    // Add Continue Watching section at the top (only when no filter is active)
    if (!self->current_filter || self->current_filter->empty()) {
        GtkWidget *continue_section = create_continue_watching_section(self);
        if (continue_section) {
            gtk_box_append(self->catalogs_box, continue_section);
        }
    }
    
    self->pending_catalogs = static_cast<int>(catalogs.size());
    
    // Create sections for each catalog
    for (const auto& [manifest, catalog] : catalogs) {
        std::string title = catalog.name.empty() ? 
            (manifest.name + " - " + catalog.type) : 
            (manifest.name + " - " + catalog.name);
        
        GtkWidget *section = create_catalog_section(title, manifest.id, catalog.id, catalog.type);
        gtk_box_append(self->catalogs_box, section);
        
        // Get the items box and load content
        GtkBox *items_box = GTK_BOX(g_object_get_data(G_OBJECT(section), "items-box"));
        load_catalog_content(self, items_box, manifest.id, catalog.type, catalog.id);
    }
    
    // Switch to content view
    gtk_stack_set_visible_child_name(self->main_stack, "content");
}

void madari_window_refresh_catalogs(MadariWindow *self) {
    g_return_if_fail(MADARI_IS_WINDOW(self));
    load_catalogs(self);
}

void madari_window_show_detail(MadariWindow *self, const char *meta_id, const char *meta_type) {
    g_return_if_fail(MADARI_IS_WINDOW(self));
    
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (!service) return;
    
    MadariDetailView *detail = madari_detail_view_new(service, meta_id, meta_type);
    adw_navigation_view_push(self->navigation_view, ADW_NAVIGATION_PAGE(detail));
}

static void on_search_changed(GtkSearchEntry *entry, MadariWindow *self);
static void on_search_activated(GtkSearchEntry *entry, MadariWindow *self);
static void on_filter_toggled(GtkToggleButton *button, MadariWindow *self);

static void madari_window_class_init(MadariWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/media/madari/app/window.ui"
    );

    gtk_widget_class_bind_template_child(widget_class, MadariWindow, navigation_view);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, header_bar);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, root_stack);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, main_stack);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, catalogs_box);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, loading_spinner);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, search_button);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, search_bar);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, search_entry);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, content_scroll);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, filter_all);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, filter_movies);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, filter_series);
    gtk_widget_class_bind_template_child(widget_class, MadariWindow, filter_channels);
}

static void perform_search(MadariWindow *self, const char *query);
static void clear_search(MadariWindow *self);

static void on_search_changed([[maybe_unused]] GtkSearchEntry *entry, [[maybe_unused]] MadariWindow *self) {
    // Live search as user types - with debounce could be added here
}

static void on_search_activated(GtkSearchEntry *entry, MadariWindow *self) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!text || strlen(text) == 0) {
        // Clear search and show catalogs
        clear_search(self);
        return;
    }
    
    perform_search(self, text);
}

static void on_search_stopped([[maybe_unused]] GtkSearchEntry *entry, MadariWindow *self) {
    // When search is cancelled (Escape pressed)
    clear_search(self);
}

static void clear_search(MadariWindow *self) {
    self->is_searching = FALSE;
    delete self->current_search_query;
    self->current_search_query = nullptr;
    load_catalogs(self);
}

static void perform_search(MadariWindow *self, const char *query) {
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (!service) return;
    
    // Store search query
    delete self->current_search_query;
    self->current_search_query = new std::string(query);
    self->is_searching = TRUE;
    
    // Clear existing content
    clear_catalogs_box(self);
    
    // Show loading
    gtk_stack_set_visible_child_name(self->main_stack, "loading");
    
    // Create search results header
    GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_bottom(header_box, 16);
    
    GtkWidget *back_btn = gtk_button_new_from_icon_name("go-previous-symbolic");
    gtk_widget_add_css_class(back_btn, "flat");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer user_data) {
        MadariWindow *self = MADARI_WINDOW(user_data);
        gtk_editable_set_text(GTK_EDITABLE(self->search_entry), "");
        gtk_search_bar_set_search_mode(self->search_bar, FALSE);
        clear_search(self);
    }), self);
    gtk_box_append(GTK_BOX(header_box), back_btn);
    
    std::string title = "Search results for \"" + std::string(query) + "\"";
    GtkWidget *title_label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_label, "title-2");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(title_label, TRUE);
    gtk_box_append(GTK_BOX(header_box), title_label);
    
    gtk_box_append(self->catalogs_box, header_box);
    
    // Create results container
    GtkWidget *results_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(results_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(results_flow), FALSE);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(results_flow), 16);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(results_flow), 16);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(results_flow), 2);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(results_flow), 10);
    
    g_object_set_data(G_OBJECT(self->catalogs_box), "search-results", results_flow);
    gtk_box_append(self->catalogs_box, results_flow);
    
    // Track if we got any results
    auto has_results = std::make_shared<bool>(false);
    
    // Perform search
    service->search(
        query,
        [self, results_flow, has_results](const Stremio::Manifest& addon, 
                                           const Stremio::CatalogDefinition& catalog, 
                                           const std::vector<Stremio::MetaPreview>& results) {
            *has_results = true;
            
            // Add section header for this addon/catalog
            std::string section_title = addon.name + " - " + 
                (catalog.name.empty() ? catalog.type : catalog.name);
            
            GtkWidget *section_label = gtk_label_new(section_title.c_str());
            gtk_widget_add_css_class(section_label, "title-4");
            gtk_widget_add_css_class(section_label, "dim-label");
            gtk_widget_set_halign(section_label, GTK_ALIGN_START);
            gtk_widget_set_margin_top(section_label, 16);
            gtk_widget_set_margin_bottom(section_label, 8);
            
            // Insert before results flow
            GtkWidget *flow = GTK_WIDGET(g_object_get_data(G_OBJECT(self->catalogs_box), "search-results"));
            if (flow) {
                // Create a section box
                GtkWidget *section_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
                gtk_widget_set_margin_top(section_box, 8);
                
                for (const auto& meta : results) {
                    GtkWidget *item = create_poster_item(meta);
                    gtk_box_append(GTK_BOX(section_box), item);
                }
                
                // Wrap in scrolled window
                GtkWidget *section_scroll = gtk_scrolled_window_new();
                gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(section_scroll), 
                                               GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
                gtk_widget_set_size_request(section_scroll, -1, 320);
                gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(section_scroll), section_box);
                
                // Add to catalogs box before the flow
                GtkWidget *container = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
                gtk_box_append(GTK_BOX(container), section_label);
                gtk_box_append(GTK_BOX(container), section_scroll);
                
                // Insert before flow box
                gtk_box_insert_child_after(self->catalogs_box, container, 
                    gtk_widget_get_prev_sibling(flow));
            }
            
            gtk_stack_set_visible_child_name(self->main_stack, "content");
        },
        [self, has_results]() {
            if (!*has_results) {
                // Show no results message
                clear_catalogs_box(self);
                
                GtkWidget *status = adw_status_page_new();
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(status), "system-search-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(status), "No Results");
                
                std::string desc = "No results found for \"" + *self->current_search_query + "\"";
                adw_status_page_set_description(ADW_STATUS_PAGE(status), desc.c_str());
                
                gtk_box_append(self->catalogs_box, status);
            }
            
            gtk_stack_set_visible_child_name(self->main_stack, "content");
        }
    );
}

static void on_filter_toggled(GtkToggleButton *button, MadariWindow *self) {
    if (!gtk_toggle_button_get_active(button)) return;
    
    // Determine which filter was selected
    if (button == self->filter_all) {
        delete self->current_filter;
        self->current_filter = new std::string("");
    } else if (button == self->filter_movies) {
        delete self->current_filter;
        self->current_filter = new std::string("movie");
    } else if (button == self->filter_series) {
        delete self->current_filter;
        self->current_filter = new std::string("series");
    } else if (button == self->filter_channels) {
        delete self->current_filter;
        self->current_filter = new std::string("channel");
    }
    
    // Reload catalogs with new filter
    load_catalogs(self);
}

static void madari_window_init(MadariWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
    self->pending_catalogs = 0;
    self->soup_session = nullptr;
    self->current_filter = new std::string("");
    self->current_search_query = nullptr;
    self->is_searching = FALSE;
    self->current_meta_id = nullptr;
    self->current_meta_type = nullptr;
    self->current_video_id = nullptr;
    self->current_binge_group = nullptr;
    self->current_series_title = nullptr;
    self->current_season = 0;
    self->episode_list = nullptr;
    self->current_episode_index = -1;
    
    // Watch history tracking initialization
    self->current_poster_url = nullptr;
    self->current_episode_number = 0;
    self->history_save_timeout_id = 0;
    self->history_needs_save = FALSE;
    
    // Trakt scrobbling initialization
    self->scrobble_started = FALSE;
    self->last_scrobble_time = 0;
}

MadariWindow *madari_window_new(MadariApplication *app) {
    MadariWindow *window = MADARI_WINDOW(g_object_new(
        MADARI_TYPE_WINDOW,
        "application", app,
        nullptr
    ));
    
    window->app = app;
    
    // Connect search button to search bar
    g_object_bind_property(window->search_button, "active",
                           window->search_bar, "search-mode-enabled",
                           static_cast<GBindingFlags>(G_BINDING_BIDIRECTIONAL | G_BINDING_SYNC_CREATE));
    
    // Connect search bar to search entry
    gtk_search_bar_connect_entry(window->search_bar, GTK_EDITABLE(window->search_entry));
    
    // Connect search signals
    g_signal_connect(window->search_entry, "search-changed",
                     G_CALLBACK(on_search_changed), window);
    g_signal_connect(window->search_entry, "activate",
                     G_CALLBACK(on_search_activated), window);
    g_signal_connect(window->search_entry, "stop-search",
                     G_CALLBACK(on_search_stopped), window);
    
    // Connect filter signals
    g_signal_connect(window->filter_all, "toggled",
                     G_CALLBACK(on_filter_toggled), window);
    g_signal_connect(window->filter_movies, "toggled",
                     G_CALLBACK(on_filter_toggled), window);
    g_signal_connect(window->filter_series, "toggled",
                     G_CALLBACK(on_filter_toggled), window);
    g_signal_connect(window->filter_channels, "toggled",
                     G_CALLBACK(on_filter_toggled), window);
    
    // Subscribe to addon changes
    Stremio::AddonService *service = madari_application_get_addon_service(app);
    if (service) {
        service->on_addons_changed([window]() {
            load_catalogs(window);
        });
    }
    
    // Initial load
    load_catalogs(window);
    
    return window;
}

// ============= EMBEDDED PLAYER IMPLEMENTATION =============

static void setup_player_mpv(MadariWindow *self);
static void cleanup_player_mpv(MadariWindow *self);
static void update_player_ui(MadariWindow *self);
static void show_player_controls(MadariWindow *self);
static void schedule_hide_player_controls(MadariWindow *self);
static void update_track_menus(MadariWindow *self);
static void on_player_fullscreen(GtkButton *btn, MadariWindow *self);

static void *player_get_proc_address([[maybe_unused]] void *ctx, const char *name) {
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

// Inhibit system sleep/idle during video playback
static void inhibit_system_sleep(MadariWindow *self) {
    if (self->inhibit_cookie != 0) return;  // Already inhibited
    
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(self));
    if (app) {
        self->inhibit_cookie = gtk_application_inhibit(
            app,
            GTK_WINDOW(self),
            static_cast<GtkApplicationInhibitFlags>(GTK_APPLICATION_INHIBIT_IDLE | GTK_APPLICATION_INHIBIT_SUSPEND),
            "Video playback in progress"
        );
    }
}

// Remove system sleep inhibition
static void uninhibit_system_sleep(MadariWindow *self) {
    if (self->inhibit_cookie == 0) return;  // Not inhibited
    
    GtkApplication *app = gtk_window_get_application(GTK_WINDOW(self));
    if (app) {
        gtk_application_uninhibit(app, self->inhibit_cookie);
        self->inhibit_cookie = 0;
    }
}

static void on_player_render_update(void *ctx) {
    MadariWindow *self = MADARI_WINDOW(ctx);
    // Add a ref to prevent the window from being destroyed while the callback is pending
    if (MADARI_IS_WINDOW(self)) {
        g_object_ref(self);
        g_idle_add([](gpointer data) -> gboolean {
            MadariWindow *self = MADARI_WINDOW(data);
            // Verify the window is still valid
            if (MADARI_IS_WINDOW(self) && self->video_area && GTK_IS_GL_AREA(self->video_area)) {
                gtk_gl_area_queue_render(self->video_area);
            }
            g_object_unref(self);
            return G_SOURCE_REMOVE;
        }, self);
    }
}

// ============= Watch History Functions =============

static void save_watch_progress(MadariWindow *self) {
    if (!self->current_meta_id || !self->current_video_id) return;
    if (self->player_duration <= 0) return;  // No valid duration yet
    
    Madari::WatchHistoryService *history = madari_application_get_watch_history(self->app);
    if (!history) return;
    
    Madari::WatchHistoryEntry entry;
    entry.meta_id = *self->current_meta_id;
    entry.meta_type = self->current_meta_type ? *self->current_meta_type : "movie";
    entry.video_id = *self->current_video_id;
    entry.title = self->player_current_title ? *self->player_current_title : "Unknown";
    entry.poster_url = self->current_poster_url ? *self->current_poster_url : "";
    entry.position = self->player_position;
    entry.duration = self->player_duration;
    
    if (self->current_series_title) {
        entry.series_title = *self->current_series_title;
    }
    if (self->current_season > 0) {
        entry.season = self->current_season;
    }
    if (self->current_episode_number > 0) {
        entry.episode = self->current_episode_number;
    }
    if (self->current_binge_group) {
        entry.binge_group = *self->current_binge_group;
    }
    
    history->update_progress(entry);
    self->history_needs_save = FALSE;
}

static gboolean on_history_save_timeout(gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    
    if (self->history_needs_save) {
        save_watch_progress(self);
    }
    
    // Continue the timeout
    return G_SOURCE_CONTINUE;
}

static void schedule_history_save(MadariWindow *self) {
    self->history_needs_save = TRUE;
    
    // Start periodic save timer if not already running
    if (self->history_save_timeout_id == 0) {
        // Save every 10 seconds while playing
        self->history_save_timeout_id = g_timeout_add_seconds(10, on_history_save_timeout, self);
    }
}

static void stop_history_save_timer(MadariWindow *self) {
    if (self->history_save_timeout_id > 0) {
        g_source_remove(self->history_save_timeout_id);
        self->history_save_timeout_id = 0;
    }
    
    // Do a final save if needed
    if (self->history_needs_save) {
        save_watch_progress(self);
    }
}

// ============= End Watch History Functions =============

static void on_player_mpv_events(MadariWindow *self) {
    if (!self->mpv) return;
    
    while (true) {
        mpv_event *event = mpv_wait_event(self->mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        
        switch (event->event_id) {
            case MPV_EVENT_PROPERTY_CHANGE: {
                mpv_event_property *prop = static_cast<mpv_event_property*>(event->data);
                
                if (strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                    self->player_position = *static_cast<double*>(prop->data);
                    if (!self->player_seeking) {
                        update_player_ui(self);
                    }
                    // Schedule watch history save (will be batched)
                    schedule_history_save(self);
                } else if (strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
                    self->player_duration = *static_cast<double*>(prop->data);
                    update_player_ui(self);
                } else if (strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
                    gboolean was_playing = self->player_is_playing;
                    self->player_is_playing = !*static_cast<int*>(prop->data);
                    gtk_button_set_icon_name(self->player_play_btn,
                        self->player_is_playing ? "media-playback-pause-symbolic" : "media-playback-start-symbolic");
                    // Inhibit/uninhibit system sleep based on playback state
                    if (self->player_is_playing) {
                        inhibit_system_sleep(self);
                        // Trakt: Resume playback - send start scrobble
                        if (!was_playing && self->scrobble_started) {
                            trigger_scrobble(self, "start");
                        }
                    } else {
                        uninhibit_system_sleep(self);
                        // Trakt: Pause playback - send pause scrobble
                        if (was_playing && self->scrobble_started) {
                            trigger_scrobble(self, "pause");
                        }
                    }
                } else if (strcmp(prop->name, "eof-reached") == 0 && prop->format == MPV_FORMAT_FLAG) {
                    if (*static_cast<int*>(prop->data)) {
                        // Trakt: Video ended - send stop scrobble with 100% progress
                        if (self->scrobble_started) {
                            self->player_position = self->player_duration;  // Ensure 100%
                            trigger_scrobble(self, "stop");
                            self->scrobble_started = FALSE;
                        }
                        madari_window_stop_video(self);
                    }
                } else if (strcmp(prop->name, "track-list") == 0) {
                    update_track_menus(self);
                } else if (strcmp(prop->name, "core-idle") == 0 && prop->format == MPV_FORMAT_FLAG) {
                    gboolean idle = *static_cast<int*>(prop->data);
                    gtk_widget_set_visible(self->player_loading, idle && self->player_is_playing);
                }
                break;
            }
            case MPV_EVENT_FILE_LOADED:
                gtk_widget_set_visible(self->player_loading, FALSE);
                update_track_menus(self);
                
                // Trakt: Start scrobbling when file is loaded
                if (!self->scrobble_started) {
                    self->scrobble_started = TRUE;
                    trigger_scrobble(self, "start");
                }
                break;
            case MPV_EVENT_START_FILE:
                gtk_widget_set_visible(self->player_loading, TRUE);
                break;
            case MPV_EVENT_END_FILE: {
                mpv_event_end_file *end = static_cast<mpv_event_end_file*>(event->data);
                if (end->reason == MPV_END_FILE_REASON_ERROR) {
                    g_warning("MPV playback error: %s", mpv_error_string(end->error));
                }
                gtk_widget_set_visible(self->player_loading, FALSE);
                break;
            }
            default:
                break;
        }
    }
}

static gboolean on_player_mpv_event(gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    
    // Verify the window is still valid before processing events
    if (!MADARI_IS_WINDOW(self)) {
        g_object_unref(self);
        return G_SOURCE_REMOVE;
    }
    
    on_player_mpv_events(self);
    g_object_unref(self);
    return G_SOURCE_REMOVE;
}

static void player_mpv_wakeup(void *ctx) {
    MadariWindow *self = MADARI_WINDOW(ctx);
    // Add a ref to prevent the window from being destroyed while the callback is pending
    if (MADARI_IS_WINDOW(self)) {
        g_object_ref(self);
        g_idle_add(on_player_mpv_event, self);
    }
}

static void on_video_realize([[maybe_unused]] GtkWidget *widget, gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    
    g_print("on_video_realize called\n");
    
    if (!self->mpv) {
        g_print("  Setting up MPV...\n");
        setup_player_mpv(self);
        g_print("  MPV setup done, mpv=%p\n", (void*)self->mpv);
    }
    
    gtk_gl_area_make_current(self->video_area);
    
    GError *gl_error = gtk_gl_area_get_error(self->video_area);
    if (gl_error != nullptr) {
        g_warning("Player: Failed to initialize GL context: %s", gl_error->message);
        return;
    }
    
    if (self->mpv && !self->mpv_gl) {
        g_print("  Creating MPV GL context...\n");
        mpv_opengl_init_params gl_init_params = {
            .get_proc_address = player_get_proc_address,
            .get_proc_address_ctx = nullptr,
        };
        
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };
        
        if (mpv_render_context_create(&self->mpv_gl, self->mpv, params) < 0) {
            g_warning("Player: Failed to create MPV render context");
            return;
        }
        
        g_print("  MPV GL context created, mpv_gl=%p\n", (void*)self->mpv_gl);
        mpv_render_context_set_update_callback(self->mpv_gl, on_player_render_update, self);
        
        // Check for pending URL
        const char *pending_url = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "pending-url"));
        if (pending_url) {
            g_print("  Playing pending URL: %s\n", pending_url);
            const char *cmd[] = {"loadfile", pending_url, nullptr};
            mpv_command_async(self->mpv, 0, cmd);
            g_object_set_data(G_OBJECT(self), "pending-url", nullptr);
        }
    }
}

static void on_video_unrealize([[maybe_unused]] GtkWidget *widget, gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    if (self->mpv_gl) {
        mpv_render_context_free(self->mpv_gl);
        self->mpv_gl = nullptr;
    }
}

static gboolean on_video_render(GtkGLArea *area, [[maybe_unused]] GdkGLContext *context, gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    
    if (!self->mpv_gl) return FALSE;
    
    // Get the scale factor for HiDPI displays
    int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    int width = gtk_widget_get_width(GTK_WIDGET(area)) * scale;
    int height = gtk_widget_get_height(GTK_WIDGET(area)) * scale;
    
    int fbo = -1;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    
    mpv_opengl_fbo mpv_fbo = {
        .fbo = fbo,
        .w = width,
        .h = height,
        .internal_format = 0
    };
    
    int flip_y = 1;
    
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpv_fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    
    mpv_render_context_render(self->mpv_gl, params);
    
    return TRUE;
}

static void setup_player_mpv(MadariWindow *self) {
    setlocale(LC_NUMERIC, "C");
    
    self->mpv = mpv_create();
    if (!self->mpv) {
        g_warning("Failed to create MPV context");
        return;
    }
    
    mpv_set_option_string(self->mpv, "vo", "libmpv");
    mpv_set_option_string(self->mpv, "hwdec", "auto");
    mpv_set_option_string(self->mpv, "keep-open", "no");
    
    if (mpv_initialize(self->mpv) < 0) {
        g_warning("Failed to initialize MPV");
        mpv_destroy(self->mpv);
        self->mpv = nullptr;
        return;
    }
    
    mpv_observe_property(self->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(self->mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(self->mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(self->mpv, 0, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(self->mpv, 0, "core-idle", MPV_FORMAT_FLAG);
    mpv_observe_property(self->mpv, 0, "track-list", MPV_FORMAT_NODE);
    
    mpv_set_wakeup_callback(self->mpv, player_mpv_wakeup, self);
}

static void cleanup_player_mpv(MadariWindow *self) {
    if (self->mpv_gl) {
        mpv_render_context_free(self->mpv_gl);
        self->mpv_gl = nullptr;
    }
    if (self->mpv) {
        mpv_terminate_destroy(self->mpv);
        self->mpv = nullptr;
    }
}

static std::string format_player_time(double seconds) {
    if (seconds < 0) seconds = 0;
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    
    char buf[32];
    if (h > 0) {
        snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    } else {
        snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    }
    return buf;
}

static void update_player_ui(MadariWindow *self) {
    // Verify window and widgets are valid before updating UI
    if (!MADARI_IS_WINDOW(self)) return;
    
    if (self->player_duration > 0 && !self->player_seeking) {
        if (self->player_progress && GTK_IS_RANGE(self->player_progress)) {
            gtk_range_set_range(GTK_RANGE(self->player_progress), 0, self->player_duration);
            gtk_range_set_value(GTK_RANGE(self->player_progress), self->player_position);
        }
    }
    
    if (self->player_time_label && GTK_IS_LABEL(self->player_time_label)) {
        gtk_label_set_text(self->player_time_label, format_player_time(self->player_position).c_str());
    }
    
    // Show remaining time or total duration based on user preference
    if (self->player_duration_label && GTK_IS_LABEL(self->player_duration_label)) {
        if (self->player_show_remaining && self->player_duration > 0) {
            double remaining = self->player_duration - self->player_position;
            std::string remaining_str = "-" + format_player_time(remaining);
            gtk_label_set_text(self->player_duration_label, remaining_str.c_str());
        } else {
            gtk_label_set_text(self->player_duration_label, format_player_time(self->player_duration).c_str());
        }
    }
}

static void update_track_menus(MadariWindow *self) {
    // Verify window and MPV are valid before updating track menus
    if (!MADARI_IS_WINDOW(self)) return;
    if (!self->mpv) return;
    if (!self->audio_tracks || !self->subtitle_tracks) return;
    
    self->audio_tracks->clear();
    self->subtitle_tracks->clear();
    
    mpv_node track_list;
    if (mpv_get_property(self->mpv, "track-list", MPV_FORMAT_NODE, &track_list) >= 0) {
        if (track_list.format == MPV_FORMAT_NODE_ARRAY) {
            for (int i = 0; i < track_list.u.list->num; i++) {
                mpv_node *track = &track_list.u.list->values[i];
                if (track->format != MPV_FORMAT_NODE_MAP) continue;
                
                const char *type = nullptr;
                int id = 0;
                const char *title = nullptr;
                const char *lang = nullptr;
                
                for (int j = 0; j < track->u.list->num; j++) {
                    const char *key = track->u.list->keys[j];
                    mpv_node *val = &track->u.list->values[j];
                    
                    if (strcmp(key, "type") == 0 && val->format == MPV_FORMAT_STRING) {
                        type = val->u.string;
                    } else if (strcmp(key, "id") == 0 && val->format == MPV_FORMAT_INT64) {
                        id = val->u.int64;
                    } else if (strcmp(key, "title") == 0 && val->format == MPV_FORMAT_STRING) {
                        title = val->u.string;
                    } else if (strcmp(key, "lang") == 0 && val->format == MPV_FORMAT_STRING) {
                        lang = val->u.string;
                    }
                }
                
                std::string label;
                if (title && lang) {
                    // Show both title and language
                    label = std::string(title) + " (" + lang + ")";
                } else if (title) {
                    label = title;
                } else if (lang) {
                    // Capitalize language code
                    std::string langStr = lang;
                    if (!langStr.empty()) {
                        langStr[0] = toupper(langStr[0]);
                    }
                    label = langStr;
                } else {
                    label = "Track " + std::to_string(id);
                }
                
                if (type && strcmp(type, "audio") == 0) {
                    self->audio_tracks->push_back({id, label});
                } else if (type && strcmp(type, "sub") == 0) {
                    self->subtitle_tracks->push_back({id, label});
                }
            }
        }
        mpv_free_node_contents(&track_list);
    }
    
    // Build audio menu
    GMenu *audio_menu = g_menu_new();
    g_menu_append(audio_menu, "None", "win.audio-track(0)");
    for (const auto& track : *self->audio_tracks) {
        char action[64];
        snprintf(action, sizeof(action), "win.audio-track(%d)", track.first);
        g_menu_append(audio_menu, track.second.c_str(), action);
    }
    if (self->audio_track_btn && GTK_IS_MENU_BUTTON(self->audio_track_btn)) {
        gtk_menu_button_set_menu_model(self->audio_track_btn, G_MENU_MODEL(audio_menu));
    }
    g_object_unref(audio_menu);
    
    // Build subtitle menu
    GMenu *sub_menu = g_menu_new();
    g_menu_append(sub_menu, "None", "win.subtitle-track(0)");
    for (const auto& track : *self->subtitle_tracks) {
        char action[64];
        snprintf(action, sizeof(action), "win.subtitle-track(%d)", track.first);
        g_menu_append(sub_menu, track.second.c_str(), action);
    }
    if (self->subtitle_track_btn && GTK_IS_MENU_BUTTON(self->subtitle_track_btn)) {
        gtk_menu_button_set_menu_model(self->subtitle_track_btn, G_MENU_MODEL(sub_menu));
    }
    g_object_unref(sub_menu);
}

static gboolean hide_player_controls(gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    
    // Verify window is still valid
    if (!MADARI_IS_WINDOW(self)) {
        return G_SOURCE_REMOVE;
    }
    
    self->player_hide_controls_id = 0;
    
    // Don't hide if settings popover is open
    if (self->player_settings_btn && GTK_IS_MENU_BUTTON(self->player_settings_btn)) {
        GtkPopover *popover = gtk_menu_button_get_popover(self->player_settings_btn);
        if (popover && GTK_IS_POPOVER(popover) && gtk_widget_get_visible(GTK_WIDGET(popover))) {
            // Reschedule hide for later
            self->player_hide_controls_id = g_timeout_add(3000, hide_player_controls, self);
            return G_SOURCE_REMOVE;
        }
    }
    
    // Don't hide if audio/subtitle track menus are open
    if (self->audio_track_btn && GTK_IS_MENU_BUTTON(self->audio_track_btn)) {
        GtkPopover *popover = gtk_menu_button_get_popover(self->audio_track_btn);
        if (popover && GTK_IS_POPOVER(popover) && gtk_widget_get_visible(GTK_WIDGET(popover))) {
            self->player_hide_controls_id = g_timeout_add(3000, hide_player_controls, self);
            return G_SOURCE_REMOVE;
        }
    }
    if (self->subtitle_track_btn && GTK_IS_MENU_BUTTON(self->subtitle_track_btn)) {
        GtkPopover *popover = gtk_menu_button_get_popover(self->subtitle_track_btn);
        if (popover && GTK_IS_POPOVER(popover) && gtk_widget_get_visible(GTK_WIDGET(popover))) {
            self->player_hide_controls_id = g_timeout_add(3000, hide_player_controls, self);
            return G_SOURCE_REMOVE;
        }
    }
    
    if (self->player_is_playing) {
        if (self->player_controls_revealer && GTK_IS_REVEALER(self->player_controls_revealer)) {
            gtk_revealer_set_reveal_child(self->player_controls_revealer, FALSE);
        }
        if (self->player_header_revealer && GTK_IS_REVEALER(self->player_header_revealer)) {
            gtk_revealer_set_reveal_child(self->player_header_revealer, FALSE);
        }
        if (self->video_area && GTK_IS_GL_AREA(self->video_area)) {
            gtk_widget_set_cursor_from_name(GTK_WIDGET(self->video_area), "none");
        }
    }
    return G_SOURCE_REMOVE;
}

static void show_player_controls(MadariWindow *self) {
    if (!MADARI_IS_WINDOW(self)) return;
    
    if (self->player_controls_revealer && GTK_IS_REVEALER(self->player_controls_revealer)) {
        gtk_revealer_set_reveal_child(self->player_controls_revealer, TRUE);
    }
    if (self->player_header_revealer && GTK_IS_REVEALER(self->player_header_revealer)) {
        gtk_revealer_set_reveal_child(self->player_header_revealer, TRUE);
    }
    if (self->video_area && GTK_IS_GL_AREA(self->video_area)) {
        gtk_widget_set_cursor_from_name(GTK_WIDGET(self->video_area), "default");
    }
}

static void schedule_hide_player_controls(MadariWindow *self) {
    if (!MADARI_IS_WINDOW(self)) return;
    
    // Cancel existing timer
    if (self->player_hide_controls_id > 0) {
        g_source_remove(self->player_hide_controls_id);
        self->player_hide_controls_id = 0;
    }
    // Schedule new timer (3 seconds of inactivity)
    self->player_hide_controls_id = g_timeout_add(3000, hide_player_controls, self);
}

static void on_player_motion([[maybe_unused]] GtkEventControllerMotion *controller,
                              gdouble x, gdouble y,
                              MadariWindow *self) {
    static gdouble last_x = -1, last_y = -1;
    
    // Check if mouse actually moved (ignore sub-pixel jitter and spurious events)
    gdouble dx = x - last_x;
    gdouble dy = y - last_y;
    gboolean actually_moved = (last_x < 0) || (dx * dx + dy * dy > 1.0);
    
    if (!actually_moved) {
        return;
    }
    
    last_x = x;
    last_y = y;
    
    // Show controls on mouse movement
    show_player_controls(self);
    // Reset the hide timer - hide after 3 seconds of no movement
    schedule_hide_player_controls(self);
}

static void on_player_play_pause([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (!self->mpv) return;
    int pause = self->player_is_playing ? 1 : 0;
    mpv_set_property_async(self->mpv, 0, "pause", MPV_FORMAT_FLAG, &pause);
}

static void on_player_back([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    madari_window_stop_video(self);
}

static void on_player_episodes([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    // Stop playback and go to detail view for episode selection
    if (self->current_meta_id && self->current_meta_type) {
        // Stop playback first
        if (self->mpv) {
            const char *cmd[] = {"stop", nullptr};
            mpv_command_async(self->mpv, 0, cmd);
        }
        
        // Switch to browse view
        gtk_stack_set_visible_child_name(self->root_stack, "browse");
        
        // Navigate to detail view
        madari_window_show_detail(self, self->current_meta_id->c_str(), self->current_meta_type->c_str());
    }
}

// Forward declaration for play_episode_by_index
static void play_episode_by_index(MadariWindow *self, int index);

static void on_player_prev_episode([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (self->episode_list && self->current_episode_index > 0) {
        play_episode_by_index(self, self->current_episode_index - 1);
    }
}

static void on_player_next_episode([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (self->episode_list && 
        self->current_episode_index < (int)self->episode_list->size() - 1) {
        play_episode_by_index(self, self->current_episode_index + 1);
    }
}

static void update_episode_nav_buttons(MadariWindow *self) {
    if (!self->episode_list || self->episode_list->empty()) {
        gtk_widget_set_visible(GTK_WIDGET(self->player_prev_btn), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(self->player_next_btn), FALSE);
        return;
    }
    
    // Show buttons for series
    gtk_widget_set_visible(GTK_WIDGET(self->player_prev_btn), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->player_next_btn), TRUE);
    
    // Enable/disable based on position
    gtk_widget_set_sensitive(GTK_WIDGET(self->player_prev_btn), 
                             self->current_episode_index > 0);
    gtk_widget_set_sensitive(GTK_WIDGET(self->player_next_btn), 
                             self->current_episode_index < (int)self->episode_list->size() - 1);
}

static void update_mute_button_icon(MadariWindow *self) {
    const char *icon_name;
    if (self->player_is_muted) {
        icon_name = "audio-volume-muted-symbolic";
    } else {
        double volume = gtk_range_get_value(GTK_RANGE(self->player_volume));
        if (volume == 0) {
            icon_name = "audio-volume-muted-symbolic";
        } else if (volume < 33) {
            icon_name = "audio-volume-low-symbolic";
        } else if (volume < 66) {
            icon_name = "audio-volume-medium-symbolic";
        } else {
            icon_name = "audio-volume-high-symbolic";
        }
    }
    gtk_button_set_icon_name(self->player_mute_btn, icon_name);
}

static void on_player_mute_clicked([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (!self->mpv) return;
    
    if (self->player_is_muted) {
        // Unmute - restore previous volume
        self->player_is_muted = FALSE;
        gtk_range_set_value(GTK_RANGE(self->player_volume), self->player_volume_before_mute);
        mpv_set_property_async(self->mpv, 0, "volume", MPV_FORMAT_DOUBLE, &self->player_volume_before_mute);
    } else {
        // Mute - save current volume and set to 0
        self->player_volume_before_mute = gtk_range_get_value(GTK_RANGE(self->player_volume));
        self->player_is_muted = TRUE;
        double zero = 0.0;
        gtk_range_set_value(GTK_RANGE(self->player_volume), 0);
        mpv_set_property_async(self->mpv, 0, "volume", MPV_FORMAT_DOUBLE, &zero);
    }
    update_mute_button_icon(self);
}

// ============== Enhanced Player Features ==============

// Skip backward by given seconds
static void player_skip_backward(MadariWindow *self, double seconds) {
    if (!self->mpv) return;
    double pos = self->player_position - seconds;
    if (pos < 0) pos = 0;
    mpv_set_property_async(self->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &pos);
}

// Skip forward by given seconds
static void player_skip_forward(MadariWindow *self, double seconds) {
    if (!self->mpv) return;
    double pos = self->player_position + seconds;
    if (pos > self->player_duration) pos = self->player_duration;
    mpv_set_property_async(self->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &pos);
}

static void on_player_skip_back([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    player_skip_backward(self, 10);
}

static void on_player_skip_fwd([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    player_skip_forward(self, 10);
}

// Playback speed
static const double SPEED_OPTIONS[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0};
static const int NUM_SPEED_OPTIONS = 8;

static void set_playback_speed(MadariWindow *self, double speed) {
    if (!self->mpv) return;
    self->player_speed = speed;
    mpv_set_property_async(self->mpv, 0, "speed", MPV_FORMAT_DOUBLE, &speed);
}

static void cycle_speed_up(MadariWindow *self) {
    for (int i = 0; i < NUM_SPEED_OPTIONS - 1; i++) {
        if (self->player_speed <= SPEED_OPTIONS[i] + 0.01) {
            set_playback_speed(self, SPEED_OPTIONS[i + 1]);
            return;
        }
    }
}

static void cycle_speed_down(MadariWindow *self) {
    for (int i = NUM_SPEED_OPTIONS - 1; i > 0; i--) {
        if (self->player_speed >= SPEED_OPTIONS[i] - 0.01) {
            set_playback_speed(self, SPEED_OPTIONS[i - 1]);
            return;
        }
    }
}

// Aspect ratio
static const char* ASPECT_VALUES[] = {"-1", "-1", "16:9", "4:3"};  // -1 means use video aspect
static const int NUM_ASPECT_OPTIONS = 4;

static void set_aspect_ratio(MadariWindow *self, int mode) {
    if (!self->mpv || mode < 0 || mode >= NUM_ASPECT_OPTIONS) return;
    self->player_aspect_mode = mode;
    
    if (mode == 0) {
        // Fit - keep aspect ratio, letterbox
        const char *val = "no";
        mpv_set_property_async(self->mpv, 0, "panscan", MPV_FORMAT_STRING, &val);
        mpv_set_property_async(self->mpv, 0, "video-aspect-override", MPV_FORMAT_STRING, &ASPECT_VALUES[mode]);
    } else if (mode == 1) {
        // Fill - pan and scan to fill
        double panscan = 1.0;
        mpv_set_property_async(self->mpv, 0, "panscan", MPV_FORMAT_DOUBLE, &panscan);
    } else {
        // Force aspect ratio
        const char *val = "no";
        mpv_set_property_async(self->mpv, 0, "panscan", MPV_FORMAT_STRING, &val);
        mpv_set_property_async(self->mpv, 0, "video-aspect-override", MPV_FORMAT_STRING, &ASPECT_VALUES[mode]);
    }
}

static void cycle_aspect(MadariWindow *self) {
    set_aspect_ratio(self, (self->player_aspect_mode + 1) % NUM_ASPECT_OPTIONS);
}

// Loop toggle
static void update_loop_button(MadariWindow *self) {
    gtk_button_set_icon_name(self->player_loop_btn, 
        self->player_loop ? "media-playlist-repeat-symbolic" : "media-playlist-consecutive-symbolic");
    gtk_widget_remove_css_class(GTK_WIDGET(self->player_loop_btn), self->player_loop ? "dim-label" : "accent");
    if (self->player_loop) {
        gtk_widget_add_css_class(GTK_WIDGET(self->player_loop_btn), "accent");
    }
}

static void on_player_loop_clicked([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (!self->mpv) return;
    self->player_loop = !self->player_loop;
    const char *val = self->player_loop ? "inf" : "no";
    mpv_set_property_string(self->mpv, "loop-file", val);
    update_loop_button(self);
}

// Always on top
static void update_ontop_button(MadariWindow *self) {
    gtk_button_set_icon_name(self->player_ontop_btn,
        self->player_always_on_top ? "go-top-symbolic" : "go-top-symbolic");
    gtk_widget_remove_css_class(GTK_WIDGET(self->player_ontop_btn), self->player_always_on_top ? "dim-label" : "accent");
    if (self->player_always_on_top) {
        gtk_widget_add_css_class(GTK_WIDGET(self->player_ontop_btn), "accent");
    }
}

static void on_player_ontop_clicked([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    self->player_always_on_top = !self->player_always_on_top;
    // GTK4 doesn't have a direct always-on-top API, but we can use GDK
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(self));
    if (surface && GDK_IS_TOPLEVEL(surface)) {
        // Note: This may not work on all platforms (Wayland doesn't support this)
        // We'll try anyway
    }
    // For X11, we need to use the window manager hint
    // This is a workaround - GTK4 handles this differently
    update_ontop_button(self);
    
    // Show a toast to indicate the change
    AdwToast *toast = adw_toast_new(self->player_always_on_top ? 
        "Always on top enabled" : "Always on top disabled");
    adw_toast_set_timeout(toast, 1);
    // We need to find the toast overlay - for now just use gtk's built-in
}

// Screenshot
static void on_player_screenshot([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (!self->mpv) return;
    
    // Get the Pictures directory
    const char *pictures_dir = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
    if (!pictures_dir) {
        pictures_dir = g_get_home_dir();
    }
    
    // Create screenshots subdirectory
    char *screenshot_dir = g_build_filename(pictures_dir, "Madari Screenshots", nullptr);
    g_mkdir_with_parents(screenshot_dir, 0755);
    
    // Generate filename with timestamp
    GDateTime *now = g_date_time_new_now_local();
    char *timestamp = g_date_time_format(now, "%Y-%m-%d_%H-%M-%S");
    char *filename = g_strdup_printf("%s/screenshot_%s.png", screenshot_dir, timestamp);
    
    // Take screenshot using mpv
    const char *cmd[] = {"screenshot-to-file", filename, "video", nullptr};
    mpv_command_async(self->mpv, 0, cmd);
    
    g_print("Screenshot saved to: %s\n", filename);
    
    g_date_time_unref(now);
    g_free(timestamp);
    g_free(filename);
    g_free(screenshot_dir);
}

// Brightness control
static void set_brightness(MadariWindow *self, double brightness) {
    if (!self->mpv) return;
    if (brightness < -100) brightness = -100;
    if (brightness > 100) brightness = 100;
    self->player_brightness = brightness;
    int64_t val = (int64_t)brightness;
    mpv_set_property_async(self->mpv, 0, "brightness", MPV_FORMAT_INT64, &val);
}

static void on_brightness_changed(GtkRange *range, MadariWindow *self) {
    double value = gtk_range_get_value(range);
    set_brightness(self, value);
}

// Contrast control
static void set_contrast(MadariWindow *self, double contrast) {
    if (!self->mpv) return;
    if (contrast < -100) contrast = -100;
    if (contrast > 100) contrast = 100;
    self->player_contrast = contrast;
    int64_t val = (int64_t)contrast;
    mpv_set_property_async(self->mpv, 0, "contrast", MPV_FORMAT_INT64, &val);
}

static void on_contrast_changed(GtkRange *range, MadariWindow *self) {
    double value = gtk_range_get_value(range);
    set_contrast(self, value);
}

// Reset video settings
static void on_reset_video_settings([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    set_brightness(self, 0);
    set_contrast(self, 0);
    // Update sliders if they exist in the popover
}

// Toggle remaining time display
static void toggle_time_display(MadariWindow *self) {
    self->player_show_remaining = !self->player_show_remaining;
}

// Double-click handler for seeking
static void on_video_double_click([[maybe_unused]] GtkGestureClick *gesture,
                                   [[maybe_unused]] gint n_press,
                                   gdouble x,
                                   [[maybe_unused]] gdouble y,
                                   MadariWindow *self) {
    if (n_press != 2) return;
    
    // Get video area width
    int width = gtk_widget_get_width(GTK_WIDGET(self->video_area));
    
    // Left third = seek back, right third = seek forward, middle = toggle pause
    if (x < width / 3.0) {
        player_skip_backward(self, 10);
    } else if (x > width * 2.0 / 3.0) {
        player_skip_forward(self, 10);
    } else {
        // Middle - toggle fullscreen
        on_player_fullscreen(nullptr, self);
    }
}

// ============== End Enhanced Player Features ==============

// Forward declaration for showing streams dialog from window
static void show_episode_streams_dialog(MadariWindow *self, const std::string& video_id, 
                                         const std::string& episode_title);

static void play_episode_by_index(MadariWindow *self, int index) {
    if (!self->episode_list || index < 0 || index >= (int)self->episode_list->size()) {
        return;
    }
    
    // Trakt: Stop scrobble for current episode before switching
    if (self->scrobble_started) {
        trigger_scrobble(self, "stop");
        self->scrobble_started = FALSE;
    }
    
    // Get the episode info
    const auto& episode = (*self->episode_list)[index];
    const std::string& video_id = episode.video_id;
    const std::string& episode_title = episode.title;
    int episode_num = episode.episode;
    
    // Build full title: "Series Name - S1E2 - Episode Title"
    std::string full_title;
    if (self->current_series_title) {
        full_title = *self->current_series_title;
        full_title += " - S" + std::to_string(self->current_season > 0 ? self->current_season : 1);
        full_title += "E" + std::to_string(episode_num > 0 ? episode_num : index + 1);
        if (!episode_title.empty()) {
            full_title += " - " + episode_title;
        }
    } else {
        full_title = episode_title;
    }
    
    // Update current index
    self->current_episode_index = index;
    update_episode_nav_buttons(self);
    
    // Update the current video ID
    if (self->current_video_id) delete self->current_video_id;
    self->current_video_id = new std::string(video_id);
    
    // Show loading
    gtk_widget_set_visible(self->player_loading, TRUE);
    
    // Get addon service
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (!service || !self->current_meta_type) {
        gtk_widget_set_visible(self->player_loading, FALSE);
        show_episode_streams_dialog(self, video_id, full_title);
        return;
    }
    
    // Store data for callback
    struct EpisodeData {
        MadariWindow *window;
        std::string video_id;
        std::string full_title;  // Full formatted title
        std::string binge_group;
        bool found_match = false;
    };
    
    EpisodeData *data = new EpisodeData{
        self, 
        video_id, 
        full_title,
        self->current_binge_group ? *self->current_binge_group : "",
        false
    };
    
    // Fetch streams for the episode
    service->fetch_all_streams(
        *self->current_meta_type,
        video_id,
        [data]([[maybe_unused]] const Stremio::Manifest& addon, const std::vector<Stremio::Stream>& streams) {
            MadariWindow *self = data->window;
            
            // If we have a binge_group, try to find matching stream
            if (!data->binge_group.empty()) {
                for (const auto& stream : streams) {
                    if (stream.behavior_hints.binge_group.has_value() &&
                        *stream.behavior_hints.binge_group == data->binge_group) {
                        // Found matching stream! Get URL and play
                        std::string stream_url;
                        if (stream.url.has_value()) {
                            stream_url = *stream.url;
                        } else if (stream.info_hash.has_value()) {
                            // Build magnet URL for torrent
                            stream_url = "magnet:?xt=urn:btih:" + *stream.info_hash;
                            for (const auto& src : stream.sources) {
                                stream_url += "&tr=" + src;
                            }
                        }
                        
                        if (!stream_url.empty()) {
                            // Play directly
                            if (self->mpv) {
                                // Reset player state
                                self->player_duration = 0;
                                self->player_position = 0;
                                gtk_range_set_value(GTK_RANGE(self->player_progress), 0);
                                gtk_range_set_range(GTK_RANGE(self->player_progress), 0, 100);
                                gtk_label_set_text(self->player_time_label, "0:00");
                                gtk_label_set_text(self->player_duration_label, "0:00");
                                
                                // Update title
                                gtk_label_set_text(self->player_title_label, data->full_title.c_str());
                                
                                // Show loading spinner
                                gtk_widget_set_visible(self->player_loading, TRUE);
                                
                                // Load the new file - use loadfile with replace mode
                                const char *cmd[] = {"loadfile", stream_url.c_str(), "replace", nullptr};
                                mpv_command_async(self->mpv, 0, cmd);
                            }
                            
                            data->found_match = true;
                            return;
                        }
                    }
                }
            }
        },
        [data]() {
            // Done callback - if no match found, show stream selector
            if (!data->found_match) {
                gtk_widget_set_visible(data->window->player_loading, FALSE);
                show_episode_streams_dialog(data->window, data->video_id, data->full_title);
            }
            delete data;
        }
    );
}

// StreamsData struct for episode stream dialog
struct EpisodeStreamsData {
    MadariWindow *window;
    GtkWidget *loading_box;
    GtkListBox *streams_list;
    AdwDialog *dialog;
    std::string *episode_title;
};

static void on_episode_stream_play_clicked(GtkButton *btn, [[maybe_unused]] gpointer user_data) {
    const std::string *url = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "stream-url"));
    const std::string *title = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "stream-title"));
    const std::string *binge = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "binge-group"));
    EpisodeStreamsData *sdata = static_cast<EpisodeStreamsData*>(
        g_object_get_data(G_OBJECT(btn), "streams-data"));
    
    if (url && sdata && sdata->window) {
        MadariWindow *window = sdata->window;
        
        // Update binge group
        if (binge) {
            if (window->current_binge_group) delete window->current_binge_group;
            window->current_binge_group = new std::string(*binge);
        }
        
        // Update title
        std::string full_title = sdata->episode_title ? 
            *sdata->episode_title : (title ? *title : "Playing");
        gtk_label_set_text(window->player_title_label, full_title.c_str());
        
        // Close dialog
        adw_dialog_close(sdata->dialog);
        
        // Play
        if (window->mpv) {
            // Reset player state
            window->player_duration = 0;
            window->player_position = 0;
            gtk_range_set_value(GTK_RANGE(window->player_progress), 0);
            gtk_range_set_range(GTK_RANGE(window->player_progress), 0, 100);
            gtk_label_set_text(window->player_time_label, "0:00");
            gtk_label_set_text(window->player_duration_label, "0:00");
            
            // Show loading spinner
            gtk_widget_set_visible(window->player_loading, TRUE);
            
            const char *cmd[] = {"loadfile", url->c_str(), "replace", nullptr};
            mpv_command_async(window->mpv, 0, cmd);
        }
    }
}

static void show_episode_streams_dialog(MadariWindow *self, const std::string& video_id, 
                                         const std::string& episode_title) {
    // Create a streams selection dialog
    AdwDialog *dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, "Select Stream");
    adw_dialog_set_content_width(dialog, 500);
    adw_dialog_set_content_height(dialog, 450);
    
    GtkWidget *toolbar_view = adw_toolbar_view_new();
    
    GtkWidget *header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);
    
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(content_box, 16);
    gtk_widget_set_margin_end(content_box, 16);
    gtk_widget_set_margin_top(content_box, 16);
    gtk_widget_set_margin_bottom(content_box, 16);
    
    // Loading state
    GtkWidget *loading_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(loading_box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(loading_box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(loading_box, TRUE);
    
    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_size_request(spinner, 32, 32);
    gtk_box_append(GTK_BOX(loading_box), spinner);
    
    GtkWidget *loading_label = gtk_label_new("Loading streams...");
    gtk_widget_add_css_class(loading_label, "dim-label");
    gtk_box_append(GTK_BOX(loading_box), loading_label);
    
    gtk_box_append(GTK_BOX(content_box), loading_box);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content_box);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), scroll);
    adw_dialog_set_child(dialog, toolbar_view);
    
    // Streams list
    GtkWidget *streams_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(streams_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(streams_list, "boxed-list");
    gtk_widget_set_visible(streams_list, FALSE);
    gtk_box_append(GTK_BOX(content_box), streams_list);
    
    std::string *title_copy = new std::string(episode_title);
    EpisodeStreamsData *data = new EpisodeStreamsData{self, loading_box, GTK_LIST_BOX(streams_list), dialog, title_copy};
    
    g_object_set_data_full(G_OBJECT(dialog), "streams-data", data,
        (GDestroyNotify)+[](gpointer d) { 
            EpisodeStreamsData *sd = static_cast<EpisodeStreamsData*>(d);
            delete sd->episode_title;
            delete sd; 
        });
    
    // Get addon service and fetch streams
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (service && self->current_meta_type) {
        service->fetch_all_streams(
            *self->current_meta_type,
            video_id,
            [data](const Stremio::Manifest& addon, const std::vector<Stremio::Stream>& streams) {
                gtk_widget_set_visible(data->loading_box, FALSE);
                gtk_widget_set_visible(GTK_WIDGET(data->streams_list), TRUE);
                
                for (const auto& stream : streams) {
                    GtkWidget *row = adw_action_row_new();
                    
                    // Build stream title
                    std::string title;
                    if (stream.name.has_value() && !stream.name->empty()) {
                        title = *stream.name;
                        size_t pos;
                        while ((pos = title.find('\n')) != std::string::npos) {
                            title.replace(pos, 1, " • ");
                        }
                    } else if (stream.title.has_value() && !stream.title->empty()) {
                        title = *stream.title;
                    } else {
                        title = "Stream";
                    }
                    
                    gchar *escaped_title = g_markup_escape_text(title.c_str(), -1);
                    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), escaped_title);
                    g_free(escaped_title);
                    
                    // Subtitle
                    std::string subtitle = addon.name;
                    if (stream.description.has_value() && !stream.description->empty()) {
                        subtitle = *stream.description + "\n" + subtitle;
                    }
                    gchar *escaped_subtitle = g_markup_escape_text(subtitle.c_str(), -1);
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), escaped_subtitle);
                    g_free(escaped_subtitle);
                    adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(row), 3);
                    
                    // Play button
                    GtkWidget *play_btn = gtk_button_new_from_icon_name("media-playback-start-symbolic");
                    gtk_widget_add_css_class(play_btn, "flat");
                    gtk_widget_set_valign(play_btn, GTK_ALIGN_CENTER);
                    
                    // Get stream URL
                    std::string *stream_url = nullptr;
                    std::string *binge_group = nullptr;
                    
                    if (stream.url.has_value()) {
                        stream_url = new std::string(*stream.url);
                    } else if (stream.info_hash.has_value()) {
                        std::string magnet = "magnet:?xt=urn:btih:" + *stream.info_hash;
                        for (const auto& src : stream.sources) {
                            magnet += "&tr=" + src;
                        }
                        stream_url = new std::string(magnet);
                    }
                    
                    if (stream.behavior_hints.binge_group.has_value()) {
                        binge_group = new std::string(*stream.behavior_hints.binge_group);
                    }
                    
                    if (stream_url) {
                        g_object_set_data_full(G_OBJECT(play_btn), "stream-url", stream_url,
                            (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        g_object_set_data_full(G_OBJECT(play_btn), "stream-title", 
                            new std::string(title),
                            (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        if (binge_group) {
                            g_object_set_data_full(G_OBJECT(play_btn), "binge-group", binge_group,
                                (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        }
                        g_object_set_data(G_OBJECT(play_btn), "streams-data", data);
                        
                        g_signal_connect(play_btn, "clicked", 
                            G_CALLBACK(on_episode_stream_play_clicked), nullptr);
                    }
                    
                    adw_action_row_add_suffix(ADW_ACTION_ROW(row), play_btn);
                    adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), play_btn);
                    
                    gtk_list_box_append(data->streams_list, row);
                }
                
                // Show empty state if no streams
                if (streams.empty()) {
                    GtkWidget *empty = adw_status_page_new();
                    adw_status_page_set_icon_name(ADW_STATUS_PAGE(empty), "media-playback-stop-symbolic");
                    adw_status_page_set_title(ADW_STATUS_PAGE(empty), "No Streams Found");
                    gtk_widget_set_vexpand(empty, TRUE);
                    gtk_box_append(GTK_BOX(gtk_widget_get_parent(GTK_WIDGET(data->streams_list))), empty);
                }
            },
            []() { /* done callback */ }
        );
    }
    
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void on_player_progress_changed(GtkRange *range, MadariWindow *self) {
    if (self->player_seeking && self->mpv) {
        double value = gtk_range_get_value(range);
        mpv_set_property_async(self->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &value);
    }
}

static gboolean on_player_progress_pressed([[maybe_unused]] GtkGestureClick *gesture,
                                            [[maybe_unused]] gint n_press,
                                            [[maybe_unused]] gdouble x, [[maybe_unused]] gdouble y,
                                            MadariWindow *self) {
    self->player_seeking = TRUE;
    return FALSE;
}

static void on_player_progress_released([[maybe_unused]] GtkGestureClick *gesture,
                                         [[maybe_unused]] gint n_press,
                                         [[maybe_unused]] gdouble x, [[maybe_unused]] gdouble y,
                                         MadariWindow *self) {
    self->player_seeking = FALSE;
}

static void on_player_volume_changed(GtkRange *range, MadariWindow *self) {
    if (!self->mpv) return;
    double volume = gtk_range_get_value(range);
    mpv_set_property_async(self->mpv, 0, "volume", MPV_FORMAT_DOUBLE, &volume);
    
    // If user manually changes volume above 0, unmute
    if (volume > 0 && self->player_is_muted) {
        self->player_is_muted = FALSE;
    }
    // Update the mute button icon based on volume level
    if (self->player_mute_btn) {
        update_mute_button_icon(self);
    }
}

static void on_player_fullscreen([[maybe_unused]] GtkButton *btn, MadariWindow *self) {
    if (self->player_is_fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(self));
        gtk_button_set_icon_name(self->player_fullscreen_btn, "view-fullscreen-symbolic");
    } else {
        gtk_window_fullscreen(GTK_WINDOW(self));
        gtk_button_set_icon_name(self->player_fullscreen_btn, "view-restore-symbolic");
    }
    self->player_is_fullscreen = !self->player_is_fullscreen;
}

static gboolean on_player_key_pressed([[maybe_unused]] GtkEventControllerKey *controller,
                                       guint keyval, [[maybe_unused]] guint keycode,
                                       [[maybe_unused]] GdkModifierType state,
                                       MadariWindow *self) {
    if (!madari_window_is_playing(self)) return FALSE;
    
    switch (keyval) {
        case GDK_KEY_space:
        case GDK_KEY_k:
            on_player_play_pause(nullptr, self);
            return TRUE;
        case GDK_KEY_f:
        case GDK_KEY_F11:
            on_player_fullscreen(nullptr, self);
            return TRUE;
        case GDK_KEY_Left:
            player_skip_backward(self, 5);
            return TRUE;
        case GDK_KEY_Right:
            player_skip_forward(self, 5);
            return TRUE;
        case GDK_KEY_j:
            player_skip_backward(self, 10);
            return TRUE;
        case GDK_KEY_l:
            player_skip_forward(self, 10);
            return TRUE;
        case GDK_KEY_Up:
            gtk_range_set_value(GTK_RANGE(self->player_volume),
                                gtk_range_get_value(GTK_RANGE(self->player_volume)) + 5);
            return TRUE;
        case GDK_KEY_Down:
            gtk_range_set_value(GTK_RANGE(self->player_volume),
                                gtk_range_get_value(GTK_RANGE(self->player_volume)) - 5);
            return TRUE;
        case GDK_KEY_m:
        case GDK_KEY_M:
            on_player_mute_clicked(nullptr, self);
            return TRUE;
        case GDK_KEY_Escape:
            if (self->player_is_fullscreen) {
                on_player_fullscreen(nullptr, self);
            } else {
                madari_window_stop_video(self);
            }
            return TRUE;
        // Speed control: < and > (or , and .)
        case GDK_KEY_less:
        case GDK_KEY_comma:
            cycle_speed_down(self);
            return TRUE;
        case GDK_KEY_greater:
        case GDK_KEY_period:
            cycle_speed_up(self);
            return TRUE;
        // Screenshot
        case GDK_KEY_s:
        case GDK_KEY_S:
            on_player_screenshot(nullptr, self);
            return TRUE;
        // Loop toggle
        case GDK_KEY_r:
        case GDK_KEY_R:
            on_player_loop_clicked(nullptr, self);
            return TRUE;
        // Aspect ratio cycle
        case GDK_KEY_a:
        case GDK_KEY_A:
            cycle_aspect(self);
            return TRUE;
        // Toggle time display (remaining vs. total)
        case GDK_KEY_t:
        case GDK_KEY_T:
            toggle_time_display(self);
            return TRUE;
        // Always on top
        case GDK_KEY_p:
        case GDK_KEY_P:
            on_player_ontop_clicked(nullptr, self);
            return TRUE;
        // Brightness controls ([ and ])
        case GDK_KEY_bracketleft:
            set_brightness(self, self->player_brightness - 5);
            return TRUE;
        case GDK_KEY_bracketright:
            set_brightness(self, self->player_brightness + 5);
            return TRUE;
        // Contrast controls ({ and })
        case GDK_KEY_braceleft:
            set_contrast(self, self->player_contrast - 5);
            return TRUE;
        case GDK_KEY_braceright:
            set_contrast(self, self->player_contrast + 5);
            return TRUE;
        // Reset video settings
        case GDK_KEY_0:
            set_brightness(self, 0);
            set_contrast(self, 0);
            set_playback_speed(self, 1.0);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

static void audio_track_action([[maybe_unused]] GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    if (!self->mpv) return;
    int64_t track_id = g_variant_get_int32(parameter);
    mpv_set_property_async(self->mpv, 0, "aid", MPV_FORMAT_INT64, &track_id);
}

static void subtitle_track_action([[maybe_unused]] GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    MadariWindow *self = MADARI_WINDOW(user_data);
    if (!self->mpv) return;
    int64_t track_id = g_variant_get_int32(parameter);
    mpv_set_property_async(self->mpv, 0, "sid", MPV_FORMAT_INT64, &track_id);
}

static void create_player_ui(MadariWindow *self) {
    // Initialize player state
    self->mpv = nullptr;
    self->mpv_gl = nullptr;
    self->player_is_playing = FALSE;
    self->player_is_fullscreen = FALSE;
    self->player_seeking = FALSE;
    self->player_duration = 0;
    self->player_position = 0;
    self->player_hide_controls_id = 0;
    self->inhibit_cookie = 0;
    self->player_is_muted = FALSE;
    self->player_volume_before_mute = 100.0;
    self->player_mute_btn = nullptr;
    self->player_current_title = new std::string();
    self->audio_tracks = new std::vector<std::pair<int, std::string>>();
    self->subtitle_tracks = new std::vector<std::pair<int, std::string>>();
    
    // Enhanced player state
    self->player_speed = 1.0;
    self->player_aspect_mode = 0;
    self->player_loop = FALSE;
    self->player_always_on_top = FALSE;
    self->player_show_remaining = FALSE;
    self->player_brightness = 0;
    self->player_contrast = 0;
    
    // Create player page
    self->player_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(self->player_page, "player-view");
    
    // Player overlay
    self->player_overlay = GTK_OVERLAY(gtk_overlay_new());
    gtk_widget_set_vexpand(GTK_WIDGET(self->player_overlay), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->player_overlay), TRUE);
    
    // Video area
    self->video_area = GTK_GL_AREA(gtk_gl_area_new());
    gtk_gl_area_set_auto_render(self->video_area, FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(self->video_area), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->video_area), TRUE);
    gtk_overlay_set_child(self->player_overlay, GTK_WIDGET(self->video_area));
    
    g_signal_connect(self->video_area, "realize", G_CALLBACK(on_video_realize), self);
    g_signal_connect(self->video_area, "unrealize", G_CALLBACK(on_video_unrealize), self);
    g_signal_connect(self->video_area, "render", G_CALLBACK(on_video_render), self);
    
    // Loading spinner
    self->player_loading = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(self->player_loading, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(self->player_loading, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(self->player_loading, FALSE);
    GtkWidget *spinner = gtk_spinner_new();
    gtk_widget_set_size_request(spinner, 48, 48);
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_append(GTK_BOX(self->player_loading), spinner);
    gtk_overlay_add_overlay(self->player_overlay, self->player_loading);
    
    // Header revealer
    self->player_header_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(self->player_header_revealer, GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_reveal_child(self->player_header_revealer, TRUE);
    gtk_widget_set_valign(GTK_WIDGET(self->player_header_revealer), GTK_ALIGN_START);
    
    GtkWidget *player_header = adw_header_bar_new();
    gtk_widget_add_css_class(player_header, "osd");
    
    // Back button
    self->player_back_btn = GTK_BUTTON(gtk_button_new_from_icon_name("go-previous-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_back_btn), "flat");
    g_signal_connect(self->player_back_btn, "clicked", G_CALLBACK(on_player_back), self);
    adw_header_bar_pack_start(ADW_HEADER_BAR(player_header), GTK_WIDGET(self->player_back_btn));
    
    // Title - single line with ellipsis
    self->player_title_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_ellipsize(self->player_title_label, PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(self->player_title_label, TRUE);
    gtk_label_set_max_width_chars(self->player_title_label, 60);
    gtk_widget_set_hexpand(GTK_WIDGET(self->player_title_label), TRUE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(player_header), GTK_WIDGET(self->player_title_label));
    
    gtk_revealer_set_child(self->player_header_revealer, player_header);
    gtk_overlay_add_overlay(self->player_overlay, GTK_WIDGET(self->player_header_revealer));
    
    // Controls revealer
    self->player_controls_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(self->player_controls_revealer, GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_reveal_child(self->player_controls_revealer, TRUE);
    gtk_widget_set_valign(GTK_WIDGET(self->player_controls_revealer), GTK_ALIGN_END);
    gtk_widget_set_hexpand(GTK_WIDGET(self->player_controls_revealer), TRUE);
    
    // Main controls container with gradient background
    GtkWidget *controls_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(controls_wrapper, TRUE);
    gtk_widget_add_css_class(controls_wrapper, "player-controls-wrapper");
    
    GtkWidget *controls_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(controls_box, 24);
    gtk_widget_set_margin_end(controls_box, 24);
    gtk_widget_set_margin_bottom(controls_box, 20);
    gtk_widget_set_margin_top(controls_box, 40);
    
    // Progress bar (full width, minimal style)
    GtkWidget *progress_row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    
    self->player_progress = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 0.1));
    gtk_scale_set_draw_value(self->player_progress, FALSE);
    gtk_widget_set_hexpand(GTK_WIDGET(self->player_progress), TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(self->player_progress), "player-progress");
    
    GtkGesture *progress_click = gtk_gesture_click_new();
    g_signal_connect(progress_click, "pressed", G_CALLBACK(on_player_progress_pressed), self);
    g_signal_connect(progress_click, "released", G_CALLBACK(on_player_progress_released), self);
    gtk_widget_add_controller(GTK_WIDGET(self->player_progress), GTK_EVENT_CONTROLLER(progress_click));
    g_signal_connect(self->player_progress, "value-changed", G_CALLBACK(on_player_progress_changed), self);
    gtk_box_append(GTK_BOX(progress_row), GTK_WIDGET(self->player_progress));
    
    gtk_box_append(GTK_BOX(controls_box), progress_row);
    
    // Bottom row with all controls
    GtkWidget *bottom_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(bottom_row, TRUE);
    
    // Left section: Play, skip buttons, volume, time
    GtkWidget *left_section = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(left_section, GTK_ALIGN_START);
    
    // Previous episode button (hidden by default)
    self->player_prev_btn = GTK_BUTTON(gtk_button_new_from_icon_name("media-skip-backward-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_prev_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_prev_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_prev_btn), "Previous Episode");
    gtk_widget_set_visible(GTK_WIDGET(self->player_prev_btn), FALSE);
    g_signal_connect(self->player_prev_btn, "clicked", G_CALLBACK(on_player_prev_episode), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_prev_btn));
    
    // Skip backward button (-10s)
    self->player_skip_back_btn = GTK_BUTTON(gtk_button_new_from_icon_name("media-seek-backward-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_skip_back_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_skip_back_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_skip_back_btn), "Skip Back 10s (J)");
    g_signal_connect(self->player_skip_back_btn, "clicked", G_CALLBACK(on_player_skip_back), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_skip_back_btn));
    
    // Play/pause button (larger, prominent)
    self->player_play_btn = GTK_BUTTON(gtk_button_new_from_icon_name("media-playback-start-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_play_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_play_btn), "player-btn");
    gtk_button_set_icon_name(self->player_play_btn, "media-playback-start-symbolic");
    g_signal_connect(self->player_play_btn, "clicked", G_CALLBACK(on_player_play_pause), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_play_btn));
    
    // Skip forward button (+10s)
    self->player_skip_fwd_btn = GTK_BUTTON(gtk_button_new_from_icon_name("media-seek-forward-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_skip_fwd_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_skip_fwd_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_skip_fwd_btn), "Skip Forward 10s (L)");
    g_signal_connect(self->player_skip_fwd_btn, "clicked", G_CALLBACK(on_player_skip_fwd), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_skip_fwd_btn));
    
    // Next episode button (hidden by default)
    self->player_next_btn = GTK_BUTTON(gtk_button_new_from_icon_name("media-skip-forward-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_next_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_next_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_next_btn), "Next Episode");
    gtk_widget_set_visible(GTK_WIDGET(self->player_next_btn), FALSE);
    g_signal_connect(self->player_next_btn, "clicked", G_CALLBACK(on_player_next_episode), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_next_btn));
    
    // Volume/mute button and slider
    self->player_mute_btn = GTK_BUTTON(gtk_button_new_from_icon_name("audio-volume-high-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_mute_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_mute_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_mute_btn), "Mute (M)");
    g_signal_connect(self->player_mute_btn, "clicked", G_CALLBACK(on_player_mute_clicked), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_mute_btn));
    
    self->player_volume = GTK_SCALE(gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1));
    gtk_scale_set_draw_value(self->player_volume, FALSE);
    gtk_range_set_value(GTK_RANGE(self->player_volume), 100);
    gtk_widget_set_size_request(GTK_WIDGET(self->player_volume), 80, -1);
    gtk_widget_add_css_class(GTK_WIDGET(self->player_volume), "player-volume");
    g_signal_connect(self->player_volume, "value-changed", G_CALLBACK(on_player_volume_changed), self);
    gtk_box_append(GTK_BOX(left_section), GTK_WIDGET(self->player_volume));
    
    // Time display
    GtkWidget *time_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(time_box, 12);
    
    self->player_time_label = GTK_LABEL(gtk_label_new("0:00"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_time_label), "player-time");
    gtk_box_append(GTK_BOX(time_box), GTK_WIDGET(self->player_time_label));
    
    GtkWidget *time_sep = gtk_label_new("/");
    gtk_widget_add_css_class(time_sep, "player-time");
    gtk_widget_add_css_class(time_sep, "dim-label");
    gtk_box_append(GTK_BOX(time_box), time_sep);
    
    self->player_duration_label = GTK_LABEL(gtk_label_new("0:00"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_duration_label), "player-time");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_duration_label), "dim-label");
    gtk_box_append(GTK_BOX(time_box), GTK_WIDGET(self->player_duration_label));
    
    gtk_box_append(GTK_BOX(left_section), time_box);
    gtk_box_append(GTK_BOX(bottom_row), left_section);
    
    // Center spacer
    GtkWidget *center_spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(center_spacer, TRUE);
    gtk_box_append(GTK_BOX(bottom_row), center_spacer);
    
    // Right section: Audio, subtitles, settings, fullscreen
    GtkWidget *right_section = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(right_section, GTK_ALIGN_END);
    
    // Audio track button
    self->audio_track_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(self->audio_track_btn, "audio-x-generic-symbolic");
    gtk_widget_add_css_class(GTK_WIDGET(self->audio_track_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->audio_track_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->audio_track_btn), "Audio Track");
    gtk_box_append(GTK_BOX(right_section), GTK_WIDGET(self->audio_track_btn));
    
    // Subtitle track button
    self->subtitle_track_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(self->subtitle_track_btn, "media-view-subtitles-symbolic");
    gtk_widget_add_css_class(GTK_WIDGET(self->subtitle_track_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->subtitle_track_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->subtitle_track_btn), "Subtitles");
    gtk_box_append(GTK_BOX(right_section), GTK_WIDGET(self->subtitle_track_btn));
    
    // Episodes button (for series - hidden by default)
    self->player_episodes_btn = GTK_BUTTON(gtk_button_new_from_icon_name("view-list-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_episodes_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_episodes_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_episodes_btn), "Episodes");
    gtk_widget_set_visible(GTK_WIDGET(self->player_episodes_btn), FALSE);
    g_signal_connect(self->player_episodes_btn, "clicked", G_CALLBACK(on_player_episodes), self);
    gtk_box_append(GTK_BOX(right_section), GTK_WIDGET(self->player_episodes_btn));
    
    // Settings menu button (contains speed, aspect, loop, screenshot, always-on-top)
    self->player_settings_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(self->player_settings_btn, "emblem-system-symbolic");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_settings_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_settings_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_settings_btn), "Settings");
    
    // Create settings popover with organized sections
    GtkWidget *settings_popover = gtk_popover_new();
    gtk_widget_add_css_class(settings_popover, "menu");
    
    GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_top(settings_box, 6);
    gtk_widget_set_margin_bottom(settings_box, 6);
    gtk_widget_set_margin_start(settings_box, 6);
    gtk_widget_set_margin_end(settings_box, 6);
    
    // Speed section
    GtkWidget *speed_label = gtk_label_new("Playback Speed");
    gtk_widget_add_css_class(speed_label, "heading");
    gtk_widget_set_halign(speed_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(speed_label, 6);
    gtk_widget_set_margin_top(speed_label, 6);
    gtk_box_append(GTK_BOX(settings_box), speed_label);
    
    // Speed buttons in a flow box
    GtkWidget *speed_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(speed_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(speed_flow), 4);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(speed_flow), TRUE);
    gtk_widget_set_margin_start(speed_flow, 6);
    gtk_widget_set_margin_end(speed_flow, 6);
    gtk_widget_set_margin_top(speed_flow, 6);
    gtk_widget_set_margin_bottom(speed_flow, 6);
    
    const char *speed_labels[] = {"0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x"};
    const double speed_values[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    for (int i = 0; i < 6; i++) {
        GtkWidget *btn = gtk_button_new_with_label(speed_labels[i]);
        gtk_widget_add_css_class(btn, "flat");
        g_object_set_data(G_OBJECT(btn), "speed-value", GINT_TO_POINTER((int)(speed_values[i] * 100)));
        g_object_set_data(G_OBJECT(btn), "window", self);
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton *btn, [[maybe_unused]] gpointer data) {
            MadariWindow *win = MADARI_WINDOW(g_object_get_data(G_OBJECT(btn), "window"));
            int speed_int = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "speed-value"));
            double speed = speed_int / 100.0;
            set_playback_speed(win, speed);
        }), nullptr);
        gtk_flow_box_append(GTK_FLOW_BOX(speed_flow), btn);
    }
    gtk_box_append(GTK_BOX(settings_box), speed_flow);
    
    // Separator
    gtk_box_append(GTK_BOX(settings_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Aspect ratio section
    GtkWidget *aspect_label = gtk_label_new("Aspect Ratio");
    gtk_widget_add_css_class(aspect_label, "heading");
    gtk_widget_set_halign(aspect_label, GTK_ALIGN_START);
    gtk_widget_set_margin_start(aspect_label, 6);
    gtk_widget_set_margin_top(aspect_label, 6);
    gtk_box_append(GTK_BOX(settings_box), aspect_label);
    
    GtkWidget *aspect_flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(aspect_flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(aspect_flow), 4);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(aspect_flow), TRUE);
    gtk_widget_set_margin_start(aspect_flow, 6);
    gtk_widget_set_margin_end(aspect_flow, 6);
    gtk_widget_set_margin_top(aspect_flow, 6);
    gtk_widget_set_margin_bottom(aspect_flow, 6);
    
    const char *aspect_labels[] = {"Fit", "Fill", "16:9", "4:3"};
    for (int i = 0; i < 4; i++) {
        GtkWidget *btn = gtk_button_new_with_label(aspect_labels[i]);
        gtk_widget_add_css_class(btn, "flat");
        g_object_set_data(G_OBJECT(btn), "aspect-mode", GINT_TO_POINTER(i));
        g_object_set_data(G_OBJECT(btn), "window", self);
        g_signal_connect(btn, "clicked", G_CALLBACK(+[](GtkButton *btn, [[maybe_unused]] gpointer data) {
            MadariWindow *win = MADARI_WINDOW(g_object_get_data(G_OBJECT(btn), "window"));
            int mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "aspect-mode"));
            set_aspect_ratio(win, mode);
        }), nullptr);
        gtk_flow_box_append(GTK_FLOW_BOX(aspect_flow), btn);
    }
    gtk_box_append(GTK_BOX(settings_box), aspect_flow);
    
    // Separator
    gtk_box_append(GTK_BOX(settings_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Action buttons row (Loop, Screenshot, Always on Top)
    GtkWidget *actions_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(actions_box, 6);
    gtk_widget_set_margin_end(actions_box, 6);
    gtk_widget_set_margin_top(actions_box, 8);
    gtk_widget_set_margin_bottom(actions_box, 6);
    gtk_widget_set_halign(actions_box, GTK_ALIGN_CENTER);
    
    // Loop button
    self->player_loop_btn = GTK_BUTTON(gtk_button_new_from_icon_name("media-playlist-consecutive-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_loop_btn), "flat");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_loop_btn), "Loop (R)");
    g_signal_connect(self->player_loop_btn, "clicked", G_CALLBACK(on_player_loop_clicked), self);
    gtk_box_append(GTK_BOX(actions_box), GTK_WIDGET(self->player_loop_btn));
    
    // Screenshot button
    self->player_screenshot_btn = GTK_BUTTON(gtk_button_new_from_icon_name("camera-photo-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_screenshot_btn), "flat");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_screenshot_btn), "Screenshot (S)");
    g_signal_connect(self->player_screenshot_btn, "clicked", G_CALLBACK(on_player_screenshot), self);
    gtk_box_append(GTK_BOX(actions_box), GTK_WIDGET(self->player_screenshot_btn));
    
    // Always on top button
    self->player_ontop_btn = GTK_BUTTON(gtk_button_new_from_icon_name("go-top-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_ontop_btn), "flat");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_ontop_btn), "Always on Top (P)");
    g_signal_connect(self->player_ontop_btn, "clicked", G_CALLBACK(on_player_ontop_clicked), self);
    gtk_box_append(GTK_BOX(actions_box), GTK_WIDGET(self->player_ontop_btn));
    
    gtk_box_append(GTK_BOX(settings_box), actions_box);
    
    gtk_popover_set_child(GTK_POPOVER(settings_popover), settings_box);
    gtk_menu_button_set_popover(self->player_settings_btn, settings_popover);
    gtk_box_append(GTK_BOX(right_section), GTK_WIDGET(self->player_settings_btn));
    
    // Fullscreen button
    self->player_fullscreen_btn = GTK_BUTTON(gtk_button_new_from_icon_name("view-fullscreen-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->player_fullscreen_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->player_fullscreen_btn), "player-btn");
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->player_fullscreen_btn), "Fullscreen (F)");
    g_signal_connect(self->player_fullscreen_btn, "clicked", G_CALLBACK(on_player_fullscreen), self);
    gtk_box_append(GTK_BOX(right_section), GTK_WIDGET(self->player_fullscreen_btn));
    
    gtk_box_append(GTK_BOX(bottom_row), right_section);
    gtk_box_append(GTK_BOX(controls_box), bottom_row);
    
    gtk_box_append(GTK_BOX(controls_wrapper), controls_box);
    gtk_revealer_set_child(self->player_controls_revealer, controls_wrapper);
    gtk_overlay_add_overlay(self->player_overlay, GTK_WIDGET(self->player_controls_revealer));
    
    // Motion controller for overlay - show controls on mouse movement, hide after inactivity
    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_player_motion), self);
    gtk_widget_add_controller(GTK_WIDGET(self->player_overlay), motion);
    
    // Also add motion controller directly to video area for better event capture
    GtkEventController *video_motion = gtk_event_controller_motion_new();
    g_signal_connect(video_motion, "motion", G_CALLBACK(on_player_motion), self);
    gtk_widget_add_controller(GTK_WIDGET(self->video_area), video_motion);
    
    // Double-click gesture on video area for quick seeking
    GtkGesture *video_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(video_click), GDK_BUTTON_PRIMARY);
    g_signal_connect(video_click, "pressed", G_CALLBACK(on_video_double_click), self);
    gtk_widget_add_controller(GTK_WIDGET(self->video_area), GTK_EVENT_CONTROLLER(video_click));
    
    // Motion controllers for controls - also trigger on motion over controls
    GtkEventController *controls_motion = gtk_event_controller_motion_new();
    g_signal_connect(controls_motion, "motion", G_CALLBACK(on_player_motion), self);
    gtk_widget_add_controller(controls_wrapper, controls_motion);
    
    // Motion controller for header content
    GtkEventController *header_motion = gtk_event_controller_motion_new();
    g_signal_connect(header_motion, "motion", G_CALLBACK(on_player_motion), self);
    gtk_widget_add_controller(player_header, header_motion);
    
    // Key controller
    GtkEventController *key = gtk_event_controller_key_new();
    g_signal_connect(key, "key-pressed", G_CALLBACK(on_player_key_pressed), self);
    gtk_widget_add_controller(GTK_WIDGET(self), key);
    
    gtk_box_append(GTK_BOX(self->player_page), GTK_WIDGET(self->player_overlay));
    
    // Add to root stack (not main_stack which is inside navigation)
    gtk_stack_add_named(self->root_stack, self->player_page, "player");
    
    // Add actions for track selection
    GSimpleAction *audio_action = g_simple_action_new("audio-track", G_VARIANT_TYPE_INT32);
    g_signal_connect(audio_action, "activate", G_CALLBACK(audio_track_action), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(audio_action));
    
    GSimpleAction *sub_action = g_simple_action_new("subtitle-track", G_VARIANT_TYPE_INT32);
    g_signal_connect(sub_action, "activate", G_CALLBACK(subtitle_track_action), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(sub_action));
}

// ============= Resume Dialog for Continue Watching =============

struct ResumeDialogData {
    MadariWindow *window;
    Madari::WatchHistoryEntry entry;
    AdwDialog *dialog;
    double resume_position;      // Position in seconds (for local items)
    double resume_percent;       // Position as percentage 0-100 (for Trakt items)
    bool use_percent;            // True if we should use percentage-based seeking
};

static void on_resume_stream_play(GtkButton *btn, gpointer user_data) {
    ResumeDialogData *data = static_cast<ResumeDialogData*>(user_data);
    
    const std::string *url = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "stream-url"));
    const std::string *binge = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "binge-group"));
    gboolean from_start = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(btn), "from-start"));
    
    if (!url || !data) return;
    
    // Close dialog
    adw_dialog_close(data->dialog);
    
    MadariWindow *window = data->window;
    const Madari::WatchHistoryEntry& entry = data->entry;
    
    // Store context for watch history
    if (window->current_meta_id) delete window->current_meta_id;
    if (window->current_meta_type) delete window->current_meta_type;
    if (window->current_video_id) delete window->current_video_id;
    if (window->current_binge_group) delete window->current_binge_group;
    if (window->current_series_title) delete window->current_series_title;
    if (window->current_poster_url) delete window->current_poster_url;
    
    window->current_meta_id = new std::string(entry.meta_id);
    window->current_meta_type = new std::string(entry.meta_type);
    window->current_video_id = new std::string(entry.video_id);
    window->current_binge_group = binge ? new std::string(*binge) : 
        (entry.binge_group.has_value() ? new std::string(*entry.binge_group) : nullptr);
    window->current_series_title = entry.series_title.has_value() ? 
        new std::string(*entry.series_title) : nullptr;
    window->current_poster_url = new std::string(entry.poster_url);
    window->current_season = entry.season.value_or(0);
    window->current_episode_number = entry.episode.value_or(0);
    
    // Show episodes button for series
    bool is_series = entry.meta_type == "series";
    gtk_widget_set_visible(GTK_WIDGET(window->player_episodes_btn), is_series);
    
    // Build title
    std::string title = entry.title;
    
    // Play video
    madari_window_play_video(window, url->c_str(), title.c_str());
    
    // Seek to resume position if not starting from beginning
    if (!from_start) {
        if (data->use_percent && data->resume_percent > 1.0) {
            // Percentage-based seeking (for Trakt items)
            // Store percentage and seek after we know the duration
            double *seek_pct = new double(data->resume_percent);
            g_object_set_data_full(G_OBJECT(window), "pending-seek-percent", seek_pct,
                [](gpointer d) { delete static_cast<double*>(d); });
            
            // Wait for file to load and get duration, then seek
            g_timeout_add(1000, [](gpointer user_data) -> gboolean {
                MadariWindow *window = MADARI_WINDOW(user_data);
                double *pct = static_cast<double*>(g_object_get_data(G_OBJECT(window), "pending-seek-percent"));
                if (pct && window->mpv) {
                    // Get duration
                    double duration = 0;
                    mpv_get_property(window->mpv, "duration", MPV_FORMAT_DOUBLE, &duration);
                    if (duration > 0) {
                        double pos = (duration * (*pct)) / 100.0;
                        mpv_set_property_async(window->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, &pos);
                        g_object_set_data(G_OBJECT(window), "pending-seek-percent", nullptr);
                    } else {
                        // Duration not ready yet, retry
                        return G_SOURCE_CONTINUE;
                    }
                }
                return G_SOURCE_REMOVE;
            }, window);
        } else if (data->resume_position > 30) {
            // Time-based seeking (for local items)
            double *seek_pos = new double(data->resume_position);
            g_object_set_data_full(G_OBJECT(window), "pending-seek", seek_pos,
                [](gpointer d) { delete static_cast<double*>(d); });
            
            // Wait a bit for the file to load then seek
            g_timeout_add(500, [](gpointer user_data) -> gboolean {
                MadariWindow *window = MADARI_WINDOW(user_data);
                double *pos = static_cast<double*>(g_object_get_data(G_OBJECT(window), "pending-seek"));
                if (pos && window->mpv) {
                    mpv_set_property_async(window->mpv, 0, "time-pos", MPV_FORMAT_DOUBLE, pos);
                    g_object_set_data(G_OBJECT(window), "pending-seek", nullptr);
                }
                return G_SOURCE_REMOVE;
            }, window);
        }
    }
}

static void show_resume_dialog(MadariWindow *self, const Madari::WatchHistoryEntry& entry) {
    AdwDialog *dialog = adw_dialog_new();
    
    // Title with progress info
    std::string dialog_title = "Resume " + (entry.series_title.has_value() ? *entry.series_title : entry.title);
    if (entry.meta_type == "series" && entry.season.has_value() && entry.episode.has_value()) {
        dialog_title = "Resume S" + std::to_string(*entry.season) + "E" + std::to_string(*entry.episode);
    }
    adw_dialog_set_title(dialog, dialog_title.c_str());
    adw_dialog_set_content_width(dialog, 500);
    adw_dialog_set_content_height(dialog, 450);
    
    GtkWidget *toolbar_view = adw_toolbar_view_new();
    
    GtkWidget *header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header);
    
    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(content_box, 16);
    gtk_widget_set_margin_end(content_box, 16);
    gtk_widget_set_margin_top(content_box, 16);
    gtk_widget_set_margin_bottom(content_box, 16);
    
    // Progress info at the top
    GtkWidget *progress_info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    
    GtkWidget *progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), entry.get_progress());
    gtk_widget_add_css_class(progress_bar, "osd");
    gtk_box_append(GTK_BOX(progress_info), progress_bar);
    
    GtkWidget *progress_label = gtk_label_new(entry.get_progress_string().c_str());
    gtk_widget_add_css_class(progress_label, "dim-label");
    gtk_widget_set_halign(progress_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(progress_info), progress_label);
    
    gtk_box_append(GTK_BOX(content_box), progress_info);
    
    // Separator
    gtk_box_append(GTK_BOX(content_box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    
    // Loading state
    GtkWidget *loading_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_valign(loading_box, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(loading_box, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(loading_box, TRUE);
    
    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_size_request(spinner, 32, 32);
    gtk_box_append(GTK_BOX(loading_box), spinner);
    
    GtkWidget *loading_label = gtk_label_new("Loading streams...");
    gtk_widget_add_css_class(loading_label, "dim-label");
    gtk_box_append(GTK_BOX(loading_box), loading_label);
    
    gtk_box_append(GTK_BOX(content_box), loading_box);
    
    // Streams list
    GtkWidget *streams_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(streams_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(streams_list, "boxed-list");
    gtk_widget_set_visible(streams_list, FALSE);
    gtk_box_append(GTK_BOX(content_box), streams_list);
    
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), content_box);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), scroll);
    adw_dialog_set_child(dialog, toolbar_view);
    
    // Store dialog data
    // Detect if this is a Trakt item (normalized duration = 100)
    bool is_trakt_item = (entry.duration == 100.0);
    ResumeDialogData *data = new ResumeDialogData{
        self, 
        entry, 
        dialog, 
        is_trakt_item ? 0.0 : entry.position,           // resume_position (seconds) for local items
        is_trakt_item ? entry.position : 0.0,           // resume_percent (0-100) for Trakt items
        is_trakt_item                                   // use_percent flag
    };
    g_object_set_data_full(G_OBJECT(dialog), "resume-data", data,
        [](gpointer d) { delete static_cast<ResumeDialogData*>(d); });
    
    // Fetch streams for the video
    Stremio::AddonService *service = madari_application_get_addon_service(self->app);
    if (service) {
        service->fetch_all_streams(
            entry.meta_type,
            entry.video_id,
            [data, loading_box, streams_list](const Stremio::Manifest& addon, const std::vector<Stremio::Stream>& streams) {
                gtk_widget_set_visible(loading_box, FALSE);
                gtk_widget_set_visible(streams_list, TRUE);
                
                for (const auto& stream : streams) {
                    GtkWidget *row = adw_action_row_new();
                    
                    // Build stream title - prefer name, then title (matches detail_view.cpp)
                    std::string title;
                    std::string details;
                    
                    if (stream.name.has_value() && !stream.name->empty()) {
                        // Stream name often contains quality info like "Torrentio\n4K"
                        // Replace newlines with " • "
                        title = *stream.name;
                        size_t pos;
                        while ((pos = title.find('\n')) != std::string::npos) {
                            title.replace(pos, 1, " • ");
                        }
                    }
                    
                    if (stream.title.has_value() && !stream.title->empty()) {
                        if (title.empty()) {
                            title = *stream.title;
                        } else {
                            details = *stream.title;
                        }
                    }
                    
                    if (title.empty()) {
                        title = "Stream";
                    }
                    
                    // Use description for more details
                    if (stream.description.has_value() && !stream.description->empty()) {
                        if (details.empty()) {
                            details = *stream.description;
                        }
                    }
                    
                    // Set title - escape markup
                    gchar *escaped_title = g_markup_escape_text(title.c_str(), -1);
                    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), escaped_title);
                    g_free(escaped_title);
                    adw_action_row_set_title_lines(ADW_ACTION_ROW(row), 0);  // 0 = no limit, show full title
                    
                    // Subtitle with details and addon name
                    std::string subtitle;
                    if (!details.empty()) {
                        subtitle = details;
                    }
                    subtitle += (subtitle.empty() ? "" : "\n") + addon.name;
                    
                    gchar *escaped_subtitle = g_markup_escape_text(subtitle.c_str(), -1);
                    adw_action_row_set_subtitle(ADW_ACTION_ROW(row), escaped_subtitle);
                    g_free(escaped_subtitle);
                    adw_action_row_set_subtitle_lines(ADW_ACTION_ROW(row), 0);  // 0 = no limit, show full subtitle
                    
                    // Icon based on stream type
                    GtkWidget *icon;
                    if (stream.info_hash.has_value()) {
                        icon = gtk_image_new_from_icon_name("network-transmit-symbolic");
                    } else if (stream.yt_id.has_value()) {
                        icon = gtk_image_new_from_icon_name("video-display-symbolic");
                    } else {
                        icon = gtk_image_new_from_icon_name("network-server-symbolic");
                    }
                    adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
                    
                    // Get stream URL (matches detail_view.cpp logic)
                    std::string *stream_url = nullptr;
                    std::string *binge_group = nullptr;
                    
                    if (stream.url.has_value()) {
                        stream_url = new std::string(*stream.url);
                    } else if (stream.external_url.has_value()) {
                        stream_url = new std::string(*stream.external_url);
                    } else if (stream.yt_id.has_value()) {
                        stream_url = new std::string("https://youtube.com/watch?v=" + *stream.yt_id);
                    } else if (stream.info_hash.has_value()) {
                        std::string magnet = "magnet:?xt=urn:btih:" + *stream.info_hash;
                        for (const auto& src : stream.sources) {
                            magnet += "&tr=" + src;
                        }
                        stream_url = new std::string(magnet);
                    }
                    
                    if (stream.behavior_hints.binge_group.has_value()) {
                        binge_group = new std::string(*stream.behavior_hints.binge_group);
                    }
                    
                    // Highlight matching binge group
                    bool is_match = data->entry.binge_group.has_value() && 
                                   binge_group && 
                                   *data->entry.binge_group == *binge_group;
                    if (is_match) {
                        gtk_widget_add_css_class(row, "suggested-action");
                    }
                    
                    // Buttons box
                    GtkWidget *buttons_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
                    gtk_widget_set_valign(buttons_box, GTK_ALIGN_CENTER);
                    
                    // Resume button
                    GtkWidget *resume_btn = gtk_button_new_with_label("Resume");
                    gtk_widget_add_css_class(resume_btn, "flat");
                    if (stream_url) {
                        g_object_set_data_full(G_OBJECT(resume_btn), "stream-url", 
                            new std::string(*stream_url),
                            (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        if (binge_group) {
                            g_object_set_data_full(G_OBJECT(resume_btn), "binge-group", 
                                new std::string(*binge_group),
                                (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        }
                        g_object_set_data(G_OBJECT(resume_btn), "from-start", GINT_TO_POINTER(FALSE));
                        g_signal_connect(resume_btn, "clicked", G_CALLBACK(on_resume_stream_play), data);
                    }
                    gtk_box_append(GTK_BOX(buttons_box), resume_btn);
                    
                    // Start from beginning button
                    GtkWidget *start_btn = gtk_button_new_from_icon_name("view-refresh-symbolic");
                    gtk_widget_add_css_class(start_btn, "flat");
                    gtk_widget_set_tooltip_text(start_btn, "Start from beginning");
                    if (stream_url) {
                        g_object_set_data_full(G_OBJECT(start_btn), "stream-url", 
                            new std::string(*stream_url),
                            (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        if (binge_group) {
                            g_object_set_data_full(G_OBJECT(start_btn), "binge-group", 
                                new std::string(*binge_group),
                                (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                        }
                        g_object_set_data(G_OBJECT(start_btn), "from-start", GINT_TO_POINTER(TRUE));
                        g_signal_connect(start_btn, "clicked", G_CALLBACK(on_resume_stream_play), data);
                    }
                    gtk_box_append(GTK_BOX(buttons_box), start_btn);
                    
                    if (stream_url) delete stream_url;
                    if (binge_group) delete binge_group;
                    
                    adw_action_row_add_suffix(ADW_ACTION_ROW(row), buttons_box);
                    gtk_list_box_append(GTK_LIST_BOX(streams_list), row);
                }
            },
            [loading_box, streams_list]() {
                // Done callback
                GtkWidget *first = gtk_widget_get_first_child(streams_list);
                if (!first) {
                    gtk_widget_set_visible(loading_box, TRUE);
                    gtk_spinner_stop(GTK_SPINNER(gtk_widget_get_first_child(loading_box)));
                    GtkWidget *label = gtk_widget_get_last_child(loading_box);
                    if (GTK_IS_LABEL(label)) {
                        gtk_label_set_text(GTK_LABEL(label), "No streams found");
                    }
                }
            }
        );
    }
    
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

// ============= End Resume Dialog =============

void madari_window_play_video(MadariWindow *self, const char *url, const char *title) {
    g_print("madari_window_play_video called\n");
    g_print("  player_page=%p\n", (void*)self->player_page);
    
    if (!self->player_page) {
        g_print("  Creating player UI...\n");
        create_player_ui(self);
        g_print("  Player UI created, player_page=%p\n", (void*)self->player_page);
    }
    
    *self->player_current_title = title ? title : "Playing";
    gtk_label_set_text(self->player_title_label, self->player_current_title->c_str());
    
    // Reset state
    self->player_position = 0;
    self->player_duration = 0;
    self->player_is_playing = FALSE;
    update_player_ui(self);
    
    // Store URL for playback
    g_object_set_data_full(G_OBJECT(self), "pending-url", g_strdup(url), g_free);
    
    // Show player
    g_print("  Switching to player view...\n");
    g_print("  root_stack=%p\n", (void*)self->root_stack);
    gtk_stack_set_visible_child_name(self->root_stack, "player");
    g_print("  Stack switched\n");
    
    gtk_widget_set_visible(self->player_loading, TRUE);
    show_player_controls(self);
    schedule_hide_player_controls(self);
    
    // If MPV is already ready, play immediately, otherwise wait for realize
    g_print("  mpv=%p, mpv_gl=%p\n", (void*)self->mpv, (void*)self->mpv_gl);
    if (self->mpv && self->mpv_gl) {
        g_print("  Starting playback immediately...\n");
        const char *cmd[] = {"loadfile", url, nullptr};
        mpv_command_async(self->mpv, 0, cmd);
        g_object_set_data(G_OBJECT(self), "pending-url", nullptr);
    } else {
        // Wait for the widget to be mapped and realized, then initialize
        g_print("  MPV not ready, scheduling initialization...\n");
        g_idle_add([](gpointer data) -> gboolean {
            MadariWindow *self = MADARI_WINDOW(data);
            g_print("Idle callback: checking video_area state\n");
            g_print("  realized=%d, mapped=%d\n", 
                    gtk_widget_get_realized(GTK_WIDGET(self->video_area)),
                    gtk_widget_get_mapped(GTK_WIDGET(self->video_area)));
            
            if (!gtk_widget_get_realized(GTK_WIDGET(self->video_area))) {
                g_print("  Not realized yet, waiting...\n");
                return G_SOURCE_CONTINUE;
            }
            
            // Widget is realized, try to initialize MPV
            if (!self->mpv || !self->mpv_gl) {
                g_print("  Calling on_video_realize...\n");
                on_video_realize(GTK_WIDGET(self->video_area), self);
            }
            
            // Check if we have a pending URL to play
            const char *pending_url = static_cast<const char*>(g_object_get_data(G_OBJECT(self), "pending-url"));
            if (pending_url && self->mpv && self->mpv_gl) {
                g_print("  Playing pending URL: %s\n", pending_url);
                const char *cmd[] = {"loadfile", pending_url, nullptr};
                mpv_command_async(self->mpv, 0, cmd);
                g_object_set_data(G_OBJECT(self), "pending-url", nullptr);
            }
            
            return G_SOURCE_REMOVE;
        }, self);
    }
}

void madari_window_play_episode(MadariWindow *self, const char *url, const char *title,
                                 const char *meta_id, const char *meta_type,
                                 const char *video_id, const char *binge_group,
                                 const char *poster_url, int episode_num) {
    // Store episode context for episode navigation
    if (self->current_meta_id) delete self->current_meta_id;
    if (self->current_meta_type) delete self->current_meta_type;
    if (self->current_video_id) delete self->current_video_id;
    if (self->current_binge_group) delete self->current_binge_group;
    if (self->current_poster_url) delete self->current_poster_url;
    
    self->current_meta_id = meta_id ? new std::string(meta_id) : nullptr;
    self->current_meta_type = meta_type ? new std::string(meta_type) : nullptr;
    self->current_video_id = video_id ? new std::string(video_id) : nullptr;
    self->current_binge_group = binge_group ? new std::string(binge_group) : nullptr;
    self->current_poster_url = poster_url ? new std::string(poster_url) : nullptr;
    self->current_episode_number = episode_num;
    
    // Show episodes button for series
    bool is_series = meta_type && strcmp(meta_type, "series") == 0;
    gtk_widget_set_visible(GTK_WIDGET(self->player_episodes_btn), is_series);
    
    // Update episode navigation buttons
    update_episode_nav_buttons(self);
    
    // Play the video
    madari_window_play_video(self, url, title);
}

void madari_window_set_episode_list(MadariWindow *self, 
                                     const std::vector<MadariEpisodeInfo>& episodes,
                                     int current_index,
                                     const char *series_title,
                                     int season) {
    // Clear old list
    if (self->episode_list) {
        delete self->episode_list;
    }
    
    // Store series info
    if (self->current_series_title) delete self->current_series_title;
    self->current_series_title = series_title ? new std::string(series_title) : nullptr;
    self->current_season = season;
    
    // Copy episode list - convert to internal format
    self->episode_list = new std::vector<_MadariWindow::EpisodeInfo>();
    for (const auto& ep : episodes) {
        self->episode_list->push_back({ep.video_id, ep.title, ep.episode});
    }
    self->current_episode_index = current_index;
    
    // Update navigation buttons
    update_episode_nav_buttons(self);
}

void madari_window_stop_video(MadariWindow *self) {
    // Trakt: Stop scrobbling before clearing context
    if (self->scrobble_started) {
        trigger_scrobble(self, "stop");
        self->scrobble_started = FALSE;
    }
    
    // Stop watch history save timer and do final save
    stop_history_save_timer(self);
    
    if (self->mpv) {
        const char *cmd[] = {"stop", nullptr};
        mpv_command_async(self->mpv, 0, cmd);
    }
    
    // Clear track lists
    if (self->audio_tracks) self->audio_tracks->clear();
    if (self->subtitle_tracks) self->subtitle_tracks->clear();
    
    // Clear episode context
    if (self->current_meta_id) {
        delete self->current_meta_id;
        self->current_meta_id = nullptr;
    }
    if (self->current_meta_type) {
        delete self->current_meta_type;
        self->current_meta_type = nullptr;
    }
    if (self->current_video_id) {
        delete self->current_video_id;
        self->current_video_id = nullptr;
    }
    if (self->current_binge_group) {
        delete self->current_binge_group;
        self->current_binge_group = nullptr;
    }
    if (self->current_series_title) {
        delete self->current_series_title;
        self->current_series_title = nullptr;
    }
    self->current_season = 0;
    if (self->episode_list) {
        delete self->episode_list;
        self->episode_list = nullptr;
    }
    self->current_episode_index = -1;
    
    // Clear watch history related data
    if (self->current_poster_url) {
        delete self->current_poster_url;
        self->current_poster_url = nullptr;
    }
    self->current_episode_number = 0;
    
    gtk_widget_set_visible(GTK_WIDGET(self->player_episodes_btn), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->player_prev_btn), FALSE);
    gtk_widget_set_visible(GTK_WIDGET(self->player_next_btn), FALSE);
    
    // Exit fullscreen if needed
    if (self->player_is_fullscreen) {
        gtk_window_unfullscreen(GTK_WINDOW(self));
        self->player_is_fullscreen = FALSE;
    }
    
    // Cancel hide timer
    if (self->player_hide_controls_id > 0) {
        g_source_remove(self->player_hide_controls_id);
        self->player_hide_controls_id = 0;
    }
    
    // Remove sleep inhibition
    uninhibit_system_sleep(self);
    
    // Switch back to browse view
    gtk_stack_set_visible_child_name(self->root_stack, "browse");
    
    // Refresh catalogs to update Continue Watching section
    load_catalogs(self);
}

gboolean madari_window_is_playing(MadariWindow *self) {
    const char *visible = gtk_stack_get_visible_child_name(self->root_stack);
    return visible && strcmp(visible, "player") == 0;
}
