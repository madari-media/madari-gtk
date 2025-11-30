#include "detail_view.hpp"
#include "window.hpp"
#include <libsoup/soup.h>
#include <map>
#include <set>
#include <algorithm>

struct _MadariDetailView {
    AdwNavigationPage parent_instance;
    
    // Service
    Stremio::AddonService *addon_service;
    
    // Content info
    std::string *meta_id;
    std::string *meta_type;
    Stremio::Meta *meta;
    
    // UI widgets - Header
    GtkPicture *background_picture;
    GtkPicture *poster;
    GtkLabel *title_label;
    GtkLabel *tagline_label;
    GtkBox *info_chips;
    GtkLabel *description_label;
    GtkBox *action_buttons;
    
    // Info sections
    GtkBox *details_grid;
    GtkBox *cast_box;
    GtkBox *trailers_box;
    GtkBox *seasons_box;
    GtkBox *episodes_box;
    GtkBox *episodes_section;
    GtkDropDown *season_dropdown;
    GtkButton *play_button;
    
    // Main containers
    GtkBox *content_box;
    GtkSpinner *loading_spinner;
    GtkStack *main_stack;
    
    // For series
    int current_season;
    std::map<int, std::vector<Stremio::Video>> *seasons_map;
    std::vector<int> *season_numbers;
    GtkStringList *season_model;
};

G_DEFINE_TYPE(MadariDetailView, madari_detail_view, ADW_TYPE_NAVIGATION_PAGE)

// Forward declarations
static void load_meta(MadariDetailView *self);
static void populate_ui(MadariDetailView *self);
static void load_image(GtkPicture *picture, const std::string& url, int width, int height);
static void show_streams_dialog(MadariDetailView *self, const std::string& video_id);
static void populate_seasons(MadariDetailView *self);
static void populate_episodes(MadariDetailView *self, int season);
static GtkWidget* create_episode_row(MadariDetailView *self, const Stremio::Video& video);
static GtkWidget* create_info_chip(const char* text);
static GtkWidget* create_detail_row(const char* label, const std::string& value);
static GtkWidget* create_cast_item(const std::string& name, const char* role);
static GtkWidget* create_trailer_button(const Stremio::Trailer& trailer);

// StreamsData structure used by stream selection dialog
struct StreamsData {
    MadariDetailView *view;
    GtkBox *content_box;
    GtkWidget *loading_box;
    GtkListBox *streams_list;
    AdwDialog *dialog;
    std::string *meta_title;
    std::string *meta_id;
    std::string *meta_type;
    std::string *video_id;
    std::string *episode_title;  // Episode title for series
    std::string *poster_url;     // Poster URL for watch history
    int season;
    int episode;
    // Filter-related fields
    GtkBox *filter_box;
    std::set<std::string> *addon_names;
    std::string *active_filter;  // Empty string means "All"
};

// Static callback for stream play button
static void on_stream_play_clicked(GtkButton *btn, [[maybe_unused]] gpointer user_data) {
    g_print("Play button clicked!\n");
    
    const std::string *url = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "stream-url"));
    const std::string *title = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "stream-title"));
    const std::string *binge = static_cast<const std::string*>(
        g_object_get_data(G_OBJECT(btn), "binge-group"));
    StreamsData *sdata = static_cast<StreamsData*>(
        g_object_get_data(G_OBJECT(btn), "streams-data"));
    
    g_print("url=%p, sdata=%p, view=%p\n", (void*)url, (void*)sdata, 
            sdata ? (void*)sdata->view : nullptr);
    
    if (url && sdata && sdata->view) {
        // Get the main window
        GtkWidget *toplevel = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(sdata->view)));
        g_print("toplevel=%p, type=%s\n", (void*)toplevel, 
                toplevel ? G_OBJECT_TYPE_NAME(toplevel) : "null");
        
        if (toplevel && MADARI_IS_WINDOW(toplevel)) {
            MadariWindow *window = MADARI_WINDOW(toplevel);
            
            // Build title - format: "Series Name - S1E2 - Episode Title" or "Movie Name"
            std::string full_title;
            if (sdata->meta_title) {
                full_title = *sdata->meta_title;
            }
            
            // Add season/episode info for series
            if (sdata->meta_type && *sdata->meta_type == "series" && 
                (sdata->season > 0 || sdata->episode > 0)) {
                std::string se_str = " - S";
                se_str += std::to_string(sdata->season > 0 ? sdata->season : 1);
                se_str += "E";
                se_str += std::to_string(sdata->episode > 0 ? sdata->episode : 1);
                full_title += se_str;
                
                // Add episode title if available
                if (sdata->episode_title && !sdata->episode_title->empty()) {
                    full_title += " - " + *sdata->episode_title;
                }
            } else if (title && !title->empty()) {
                // For movies or if no season/episode, use stream title
                full_title += " - " + *title;
            }
            
            if (full_title.empty()) {
                full_title = "Playing";
            }
            
            g_print("Playing: %s\n", url->c_str());
            
            // Close the dialog first
            adw_dialog_close(sdata->dialog);
            
            // Play in the embedded player with episode context and binge group
            madari_window_play_episode(window, url->c_str(), full_title.c_str(),
                sdata->meta_id ? sdata->meta_id->c_str() : nullptr,
                sdata->meta_type ? sdata->meta_type->c_str() : nullptr,
                sdata->video_id ? sdata->video_id->c_str() : nullptr,
                binge ? binge->c_str() : nullptr,
                sdata->poster_url ? sdata->poster_url->c_str() : nullptr,
                sdata->episode);
            
            // Build and set episode list for the current season
            MadariDetailView *view = sdata->view;
            if (view && view->seasons_map && view->current_season >= 0) {
                auto it = view->seasons_map->find(view->current_season);
                if (it != view->seasons_map->end()) {
                    std::vector<MadariEpisodeInfo> episodes;
                    int current_idx = -1;
                    
                    // Sort episodes by episode number
                    std::vector<Stremio::Video> sorted_eps = it->second;
                    std::sort(sorted_eps.begin(), sorted_eps.end(), 
                        [](const Stremio::Video& a, const Stremio::Video& b) {
                            return a.episode.value_or(0) < b.episode.value_or(0);
                        });
                    
                    for (size_t i = 0; i < sorted_eps.size(); i++) {
                        const auto& ep = sorted_eps[i];
                        std::string ep_title = ep.title.empty() ? 
                            ("Episode " + std::to_string(ep.episode.value_or(i+1))) : 
                            ep.title;
                        episodes.push_back({ep.id, ep_title, ep.episode.value_or(i+1)});
                        
                        if (sdata->video_id && ep.id == *sdata->video_id) {
                            current_idx = i;
                        }
                    }
                    
                    if (!episodes.empty()) {
                        // Get series title
                        std::string series_title = view->meta ? view->meta->name : "";
                        madari_window_set_episode_list(window, episodes, current_idx, 
                            series_title.c_str(), view->current_season);
                    }
                }
            }
        } else {
            g_warning("Could not get MadariWindow to play video (toplevel is not MadariWindow)");
        }
    } else {
        g_warning("Missing data: url=%p, sdata=%p", (void*)url, (void*)sdata);
    }
}

static SoupSession* get_image_session() {
    static SoupSession *session = nullptr;
    if (!session) {
        session = soup_session_new();
        g_object_set(session, "timeout", 30, nullptr);
    }
    return session;
}

static void load_image(GtkPicture *picture, const std::string& url, int width, int height) {
    SoupMessage *msg = soup_message_new("GET", url.c_str());
    if (!msg) return;
    
    g_object_ref(picture);
    
    struct LoadData {
        int width;
        int height;
    };
    LoadData *data = new LoadData{width, height};
    g_object_set_data_full(G_OBJECT(picture), "load-data", data, 
        [](gpointer d) { delete static_cast<LoadData*>(d); });
    
    soup_session_send_and_read_async(
        get_image_session(),
        msg,
        G_PRIORITY_DEFAULT,
        nullptr,
        [](GObject *source, GAsyncResult *result, gpointer user_data) {
            GtkPicture *picture = GTK_PICTURE(user_data);
            g_autoptr(GError) error = nullptr;
            
            LoadData *data = static_cast<LoadData*>(g_object_get_data(G_OBJECT(picture), "load-data"));
            int width = data ? data->width : 300;
            int height = data ? data->height : 450;
            
            GBytes *bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
            
            if (bytes && !error) {
                gsize size;
                const guchar *img_data = static_cast<const guchar*>(g_bytes_get_data(bytes, &size));
                
                if (img_data && size > 0) {
                    g_autoptr(GInputStream) stream = g_memory_input_stream_new_from_data(
                        g_memdup2(img_data, size), size, g_free);
                    
                    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_stream_at_scale(
                        stream, width, height, TRUE, nullptr, nullptr);
                    
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

static GtkWidget* create_info_chip(const char* text) {
    GtkWidget *chip = gtk_label_new(text);
    gtk_widget_add_css_class(chip, "caption");
    gtk_widget_add_css_class(chip, "dim-label");
    return chip;
}

static GtkWidget* create_detail_row(const char* label, const std::string& value) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_top(row, 4);
    gtk_widget_set_margin_bottom(row, 4);
    
    GtkWidget *label_widget = gtk_label_new(label);
    gtk_widget_add_css_class(label_widget, "dim-label");
    gtk_widget_set_halign(label_widget, GTK_ALIGN_START);
    gtk_label_set_width_chars(GTK_LABEL(label_widget), 12);
    gtk_label_set_xalign(GTK_LABEL(label_widget), 0);
    gtk_box_append(GTK_BOX(row), label_widget);
    
    GtkWidget *value_widget = gtk_label_new(value.c_str());
    gtk_widget_set_halign(value_widget, GTK_ALIGN_START);
    gtk_label_set_wrap(GTK_LABEL(value_widget), TRUE);
    gtk_label_set_xalign(GTK_LABEL(value_widget), 0);
    gtk_widget_set_hexpand(value_widget, TRUE);
    gtk_box_append(GTK_BOX(row), value_widget);
    
    return row;
}

static GtkWidget* create_cast_item(const std::string& name, const char* role) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_size_request(box, 100, -1);
    
    // Avatar placeholder
    GtkWidget *avatar = adw_avatar_new(56, name.c_str(), TRUE);
    gtk_widget_set_halign(avatar, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), avatar);
    
    // Name
    GtkWidget *name_label = gtk_label_new(name.c_str());
    gtk_label_set_max_width_chars(GTK_LABEL(name_label), 12);
    gtk_label_set_ellipsize(GTK_LABEL(name_label), PANGO_ELLIPSIZE_END);
    gtk_widget_add_css_class(name_label, "caption");
    gtk_widget_set_halign(name_label, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), name_label);
    
    // Role
    if (role) {
        GtkWidget *role_label = gtk_label_new(role);
        gtk_widget_add_css_class(role_label, "caption");
        gtk_widget_add_css_class(role_label, "dim-label");
        gtk_widget_set_halign(role_label, GTK_ALIGN_CENTER);
        gtk_box_append(GTK_BOX(box), role_label);
    }
    
    return box;
}

static GtkWidget* create_trailer_button(const Stremio::Trailer& trailer) {
    GtkWidget *button = gtk_button_new();
    gtk_widget_add_css_class(button, "flat");
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    
    GtkWidget *icon = gtk_image_new_from_icon_name("media-playback-start-symbolic");
    gtk_box_append(GTK_BOX(box), icon);
    
    std::string label = trailer.type.empty() ? "Trailer" : trailer.type;
    GtkWidget *label_widget = gtk_label_new(label.c_str());
    gtk_box_append(GTK_BOX(box), label_widget);
    
    gtk_button_set_child(GTK_BUTTON(button), box);
    
    // Store YouTube ID
    std::string *yt_id = new std::string(trailer.source);
    g_object_set_data_full(G_OBJECT(button), "youtube-id", yt_id,
        [](gpointer d) { delete static_cast<std::string*>(d); });
    
    g_signal_connect(button, "clicked", G_CALLBACK(+[](GtkButton *btn, gpointer) {
        const std::string *id = static_cast<const std::string*>(
            g_object_get_data(G_OBJECT(btn), "youtube-id"));
        if (id) {
            std::string url = "https://www.youtube.com/watch?v=" + *id;
            GtkUriLauncher *launcher = gtk_uri_launcher_new(url.c_str());
            gtk_uri_launcher_launch(launcher, nullptr, nullptr, nullptr, nullptr);
            g_object_unref(launcher);
        }
    }), nullptr);
    
    return button;
}

static void on_play_clicked([[maybe_unused]] GtkButton *button, MadariDetailView *self) {
    if (!self->meta) return;
    
    std::string video_id;
    if (self->meta->type == "movie") {
        video_id = self->meta->id;
    } else if (!self->meta->videos.empty()) {
        video_id = self->meta->videos[0].id;
    } else {
        video_id = self->meta->id;
    }
    
    show_streams_dialog(self, video_id);
}

static void on_episode_play_clicked(GtkButton *button, MadariDetailView *self) {
    const char *video_id = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "video-id"));
    if (video_id) {
        show_streams_dialog(self, video_id);
    }
}

// Filter streams by addon name
static void apply_stream_filter(StreamsData *data) {
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(data->streams_list));
    while (child) {
        const char *addon_name = static_cast<const char*>(
            g_object_get_data(G_OBJECT(child), "addon-name"));
        
        bool visible = data->active_filter->empty() || 
                       (addon_name && *data->active_filter == addon_name);
        gtk_widget_set_visible(child, visible);
        
        child = gtk_widget_get_next_sibling(child);
    }
}

// Callback for filter button toggle
static void on_filter_button_toggled(GtkToggleButton *button, StreamsData *data) {
    if (!gtk_toggle_button_get_active(button)) {
        // Prevent deselecting the active button - re-activate it
        gtk_toggle_button_set_active(button, TRUE);
        return;
    }
    
    // Deactivate other buttons
    GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(data->filter_box));
    while (child) {
        if (GTK_IS_TOGGLE_BUTTON(child) && GTK_TOGGLE_BUTTON(child) != button) {
            g_signal_handlers_block_by_func(child, (gpointer)on_filter_button_toggled, data);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(child), FALSE);
            g_signal_handlers_unblock_by_func(child, (gpointer)on_filter_button_toggled, data);
        }
        child = gtk_widget_get_next_sibling(child);
    }
    
    // Update active filter
    const char *filter_name = static_cast<const char*>(
        g_object_get_data(G_OBJECT(button), "filter-name"));
    *data->active_filter = filter_name ? filter_name : "";
    
    // Apply filter
    apply_stream_filter(data);
}

// Add a filter button for an addon
static void add_filter_button(StreamsData *data, const std::string& addon_name, bool is_all = false) {
    GtkWidget *button = gtk_toggle_button_new_with_label(is_all ? "All" : addon_name.c_str());
    gtk_widget_add_css_class(button, "flat");
    
    // Store filter name (empty string for "All")
    std::string *filter_name = new std::string(is_all ? "" : addon_name);
    g_object_set_data_full(G_OBJECT(button), "filter-name", 
        const_cast<char*>(filter_name->c_str()), nullptr);
    g_object_set_data_full(G_OBJECT(data->filter_box), 
        (std::string("filter-str-") + (is_all ? "all" : addon_name)).c_str(), 
        filter_name, [](gpointer d) { delete static_cast<std::string*>(d); });
    
    // Set "All" as initially active
    if (is_all) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
    }
    
    g_signal_connect(button, "toggled", G_CALLBACK(on_filter_button_toggled), data);
    gtk_box_append(data->filter_box, button);
}

static void show_streams_dialog(MadariDetailView *self, const std::string& video_id) {
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
    
    // Filter bar (horizontal scrollable box with toggle buttons)
    GtkWidget *filter_scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(filter_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_visible(filter_scroll, FALSE);  // Hidden until streams load
    
    GtkWidget *filter_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(filter_box, "linked");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(filter_scroll), filter_box);
    gtk_box_append(GTK_BOX(content_box), filter_scroll);
    
    // Streams list
    GtkWidget *streams_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(streams_list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(streams_list, "boxed-list");
    gtk_widget_set_visible(streams_list, FALSE);
    gtk_box_append(GTK_BOX(content_box), streams_list);
    
    std::string *meta_title = new std::string(self->meta ? self->meta->name : "Video");
    std::string *meta_id = new std::string(self->meta_id ? *self->meta_id : "");
    std::string *meta_type = new std::string(self->meta_type ? *self->meta_type : "");
    std::string *vid_id = new std::string(video_id);
    
    // Find episode info from video_id
    std::string *episode_title = nullptr;
    int season_num = 0;
    int episode_num = 0;
    
    if (self->meta && self->meta_type && *self->meta_type == "series") {
        for (const auto& video : self->meta->videos) {
            if (video.id == video_id) {
                episode_title = new std::string(video.title);
                season_num = video.season.value_or(0);
                episode_num = video.episode.value_or(0);
                break;
            }
        }
    }
    
    // Initialize filter-related data
    std::set<std::string> *addon_names = new std::set<std::string>();
    std::string *active_filter = new std::string("");  // Empty = show all
    
    // Get poster URL from meta
    std::string *poster_url = nullptr;
    if (self->meta && self->meta->poster.has_value()) {
        poster_url = new std::string(*self->meta->poster);
    }
    
    StreamsData *data = new StreamsData{
        self, GTK_BOX(content_box), loading_box, GTK_LIST_BOX(streams_list), dialog, 
        meta_title, meta_id, meta_type, vid_id, episode_title, poster_url, season_num, episode_num,
        GTK_BOX(filter_box), addon_names, active_filter
    };
    
    // Store filter_scroll reference for later visibility toggle
    g_object_set_data(G_OBJECT(dialog), "filter-scroll", filter_scroll);
    
    g_object_set_data_full(G_OBJECT(dialog), "streams-data", data,
        (GDestroyNotify)+[](gpointer d) { 
            StreamsData *sd = static_cast<StreamsData*>(d);
            delete sd->meta_title;
            delete sd->meta_id;
            delete sd->meta_type;
            delete sd->video_id;
            if (sd->episode_title) delete sd->episode_title;
            if (sd->poster_url) delete sd->poster_url;
            delete sd->addon_names;
            delete sd->active_filter;
            delete sd; 
        });
    
    self->addon_service->fetch_all_streams(
        *self->meta_type,
        video_id,
        [data](const Stremio::Manifest& addon, const std::vector<Stremio::Stream>& streams) {
            gtk_widget_set_visible(data->loading_box, FALSE);
            gtk_widget_set_visible(GTK_WIDGET(data->streams_list), TRUE);
            
            // Track this addon and add filter button if it's new
            if (data->addon_names->find(addon.name) == data->addon_names->end()) {
                // First addon - add "All" button
                if (data->addon_names->empty()) {
                    add_filter_button(data, "", true);  // "All" button
                    // Show filter bar
                    GtkWidget *filter_scroll = static_cast<GtkWidget*>(
                        g_object_get_data(G_OBJECT(data->dialog), "filter-scroll"));
                    if (filter_scroll) {
                        gtk_widget_set_visible(filter_scroll, TRUE);
                    }
                }
                data->addon_names->insert(addon.name);
                add_filter_button(data, addon.name);
            }
            
            for (const auto& stream : streams) {
                GtkWidget *row = adw_action_row_new();
                
                // Build stream title - prefer name, then title
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
                
                // Icon
                GtkWidget *icon;
                if (stream.info_hash.has_value()) {
                    icon = gtk_image_new_from_icon_name("network-transmit-symbolic");
                } else if (stream.yt_id.has_value()) {
                    icon = gtk_image_new_from_icon_name("video-display-symbolic");
                } else {
                    icon = gtk_image_new_from_icon_name("network-server-symbolic");
                }
                adw_action_row_add_prefix(ADW_ACTION_ROW(row), icon);
                
                // Play button
                GtkWidget *play_btn = gtk_button_new_from_icon_name("media-playback-start-symbolic");
                gtk_widget_add_css_class(play_btn, "flat");
                gtk_widget_set_valign(play_btn, GTK_ALIGN_CENTER);
                
                std::string *stream_url = nullptr;
                if (stream.url.has_value()) {
                    stream_url = new std::string(*stream.url);
                } else if (stream.external_url.has_value()) {
                    stream_url = new std::string(*stream.external_url);
                } else if (stream.yt_id.has_value()) {
                    stream_url = new std::string("https://youtube.com/watch?v=" + *stream.yt_id);
                }
                
                if (stream_url) {
                    // Store stream URL
                    g_object_set_data_full(G_OBJECT(play_btn), "stream-url", stream_url,
                        (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                    
                    // Store title for player window
                    std::string *stream_title = new std::string(title);
                    g_object_set_data_full(G_OBJECT(play_btn), "stream-title", stream_title,
                        (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                    
                    // Store binge_group if available
                    if (stream.behavior_hints.binge_group.has_value()) {
                        std::string *binge = new std::string(*stream.behavior_hints.binge_group);
                        g_object_set_data_full(G_OBJECT(play_btn), "binge-group", binge,
                            (GDestroyNotify)+[](gpointer d) { delete static_cast<std::string*>(d); });
                    }
                    
                    // Store data pointer for dialog close
                    g_object_set_data(G_OBJECT(play_btn), "streams-data", data);
                    
                    g_signal_connect(play_btn, "clicked", G_CALLBACK(on_stream_play_clicked), nullptr);
                }
                
                adw_action_row_add_suffix(ADW_ACTION_ROW(row), play_btn);
                adw_action_row_set_activatable_widget(ADW_ACTION_ROW(row), play_btn);
                
                // Store addon name on row for filtering
                std::string *row_addon_name = new std::string(addon.name);
                g_object_set_data_full(G_OBJECT(row), "addon-name", 
                    const_cast<char*>(row_addon_name->c_str()), nullptr);
                g_object_set_data_full(G_OBJECT(row), "addon-name-str", row_addon_name,
                    [](gpointer d) { delete static_cast<std::string*>(d); });
                
                gtk_list_box_append(data->streams_list, row);
            }
        },
        [data]() {
            GtkWidget *first = gtk_widget_get_first_child(GTK_WIDGET(data->streams_list));
            if (!first) {
                gtk_widget_set_visible(data->loading_box, FALSE);
                
                GtkWidget *no_streams = adw_status_page_new();
                adw_status_page_set_icon_name(ADW_STATUS_PAGE(no_streams), "face-uncertain-symbolic");
                adw_status_page_set_title(ADW_STATUS_PAGE(no_streams), "No Streams Available");
                adw_status_page_set_description(ADW_STATUS_PAGE(no_streams), 
                    "No streaming sources were found for this content.");
                gtk_box_append(data->content_box, no_streams);
            }
        }
    );
    
    adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void on_season_changed(GtkDropDown *dropdown, [[maybe_unused]] GParamSpec *pspec, MadariDetailView *self) {
    guint selected = gtk_drop_down_get_selected(dropdown);
    if (selected < self->season_numbers->size()) {
        int season = (*self->season_numbers)[selected];
        self->current_season = season;
        populate_episodes(self, season);
    }
}

static void populate_seasons(MadariDetailView *self) {
    if (!self->meta || self->meta->videos.empty()) return;
    
    // Organize videos by season
    self->seasons_map->clear();
    self->season_numbers->clear();
    
    for (const auto& video : self->meta->videos) {
        int season = video.season.value_or(1);
        (*self->seasons_map)[season].push_back(video);
    }
    
    if (self->seasons_map->empty()) return;
    
    // Create season model for dropdown
    self->season_model = gtk_string_list_new(nullptr);
    
    for (const auto& [season, videos] : *self->seasons_map) {
        self->season_numbers->push_back(season);
        std::string label = "Season " + std::to_string(season) + " (" + std::to_string(videos.size()) + " episodes)";
        gtk_string_list_append(self->season_model, label.c_str());
    }
    
    gtk_drop_down_set_model(self->season_dropdown, G_LIST_MODEL(self->season_model));
    gtk_drop_down_set_selected(self->season_dropdown, 0);
    
    // Connect to selection changes
    g_signal_connect(self->season_dropdown, "notify::selected", 
                     G_CALLBACK(on_season_changed), self);
    
    // Set current season
    if (!self->season_numbers->empty()) {
        self->current_season = (*self->season_numbers)[0];
    }
    
    // Show episodes section
    gtk_widget_set_visible(GTK_WIDGET(self->episodes_section), TRUE);
    populate_episodes(self, self->current_season);
}

static void populate_episodes(MadariDetailView *self, int season) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self->episodes_box))) != nullptr) {
        gtk_box_remove(self->episodes_box, child);
    }
    
    auto it = self->seasons_map->find(season);
    if (it == self->seasons_map->end()) return;
    
    std::vector<Stremio::Video> episodes = it->second;
    std::sort(episodes.begin(), episodes.end(), [](const Stremio::Video& a, const Stremio::Video& b) {
        return a.episode.value_or(0) < b.episode.value_or(0);
    });
    
    // Create episode cards in a flow layout
    for (const auto& video : episodes) {
        GtkWidget *card = create_episode_row(self, video);
        gtk_box_append(self->episodes_box, card);
    }
}

static GtkWidget* create_episode_row(MadariDetailView *self, const Stremio::Video& video) {
    // Create a horizontal card for episode
    GtkWidget *card = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_margin_bottom(card, 8);
    
    // Thumbnail
    GtkWidget *thumb_frame = gtk_frame_new(nullptr);
    gtk_widget_set_overflow(thumb_frame, GTK_OVERFLOW_HIDDEN);
    gtk_widget_set_size_request(thumb_frame, 178, 100);
    
    GtkWidget *thumb_overlay = gtk_overlay_new();
    
    // Placeholder
    GtkWidget *thumb_placeholder = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(thumb_placeholder, 178, 100);
    GtkWidget *thumb_icon = gtk_image_new_from_icon_name("video-x-generic-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(thumb_icon), 32);
    gtk_widget_add_css_class(thumb_icon, "dim-label");
    gtk_widget_set_valign(thumb_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(thumb_icon, GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(thumb_icon, TRUE);
    gtk_box_append(GTK_BOX(thumb_placeholder), thumb_icon);
    gtk_overlay_set_child(GTK_OVERLAY(thumb_overlay), thumb_placeholder);
    
    // Actual thumbnail
    if (video.thumbnail.has_value() && !video.thumbnail->empty()) {
        GtkWidget *thumb = gtk_picture_new();
        gtk_widget_set_size_request(thumb, 178, 100);
        gtk_picture_set_content_fit(GTK_PICTURE(thumb), GTK_CONTENT_FIT_COVER);
        load_image(GTK_PICTURE(thumb), *video.thumbnail, 178, 100);
        gtk_overlay_add_overlay(GTK_OVERLAY(thumb_overlay), thumb);
    }
    
    // Episode number badge
    if (video.episode.has_value()) {
        GtkWidget *badge = gtk_label_new(std::to_string(*video.episode).c_str());
        gtk_widget_add_css_class(badge, "heading");
        gtk_widget_set_halign(badge, GTK_ALIGN_START);
        gtk_widget_set_valign(badge, GTK_ALIGN_END);
        gtk_widget_set_margin_start(badge, 8);
        gtk_widget_set_margin_bottom(badge, 8);
        gtk_overlay_add_overlay(GTK_OVERLAY(thumb_overlay), badge);
    }
    
    gtk_frame_set_child(GTK_FRAME(thumb_frame), thumb_overlay);
    gtk_box_append(GTK_BOX(card), thumb_frame);
    
    // Episode info
    GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_hexpand(info_box, TRUE);
    gtk_widget_set_valign(info_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(info_box, 12);
    gtk_widget_set_margin_bottom(info_box, 12);
    gtk_widget_set_margin_end(info_box, 8);
    
    // Title
    std::string title = video.title;
    if (title.empty() && video.episode.has_value()) {
        title = "Episode " + std::to_string(*video.episode);
    }
    GtkWidget *title_label = gtk_label_new(title.c_str());
    gtk_widget_add_css_class(title_label, "heading");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(title_label), 50);
    gtk_box_append(GTK_BOX(info_box), title_label);
    
    // Overview
    if (video.overview.has_value() && !video.overview->empty()) {
        GtkWidget *overview_label = gtk_label_new(video.overview->c_str());
        gtk_widget_add_css_class(overview_label, "dim-label");
        gtk_widget_add_css_class(overview_label, "caption");
        gtk_widget_set_halign(overview_label, GTK_ALIGN_START);
        gtk_label_set_ellipsize(GTK_LABEL(overview_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_lines(GTK_LABEL(overview_label), 2);
        gtk_label_set_max_width_chars(GTK_LABEL(overview_label), 80);
        gtk_label_set_wrap(GTK_LABEL(overview_label), TRUE);
        gtk_label_set_xalign(GTK_LABEL(overview_label), 0);
        gtk_box_append(GTK_BOX(info_box), overview_label);
    }
    
    // Release date if available
    if (!video.released.empty()) {
        GtkWidget *date_label = gtk_label_new(video.released.substr(0, 10).c_str());
        gtk_widget_add_css_class(date_label, "dim-label");
        gtk_widget_add_css_class(date_label, "caption");
        gtk_widget_set_halign(date_label, GTK_ALIGN_START);
        gtk_widget_set_margin_top(date_label, 4);
        gtk_box_append(GTK_BOX(info_box), date_label);
    }
    
    gtk_box_append(GTK_BOX(card), info_box);
    
    // Play button
    GtkWidget *play_btn = gtk_button_new_from_icon_name("media-playback-start-symbolic");
    gtk_widget_add_css_class(play_btn, "circular");
    gtk_widget_add_css_class(play_btn, "suggested-action");
    gtk_widget_set_valign(play_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_end(play_btn, 16);
    
    std::string *video_id = new std::string(video.id);
    g_object_set_data_full(G_OBJECT(play_btn), "video-id", 
        const_cast<char*>(video_id->c_str()), nullptr);
    g_object_set_data_full(G_OBJECT(card), "video-id-str", video_id,
        [](gpointer d) { delete static_cast<std::string*>(d); });
    
    g_signal_connect(play_btn, "clicked", G_CALLBACK(on_episode_play_clicked), self);
    gtk_box_append(GTK_BOX(card), play_btn);
    
    return card;
}

static void populate_ui(MadariDetailView *self) {
    if (!self->meta) return;
    
    // Title
    gtk_label_set_text(self->title_label, self->meta->name.c_str());
    adw_navigation_page_set_title(ADW_NAVIGATION_PAGE(self), self->meta->name.c_str());
    
    // Load background (picture is always visible, just empty until loaded)
    if (self->meta->background.has_value() && !self->meta->background->empty()) {
        load_image(self->background_picture, *self->meta->background, 1200, 400);
    }
    
    // Load poster
    if (self->meta->poster.has_value() && !self->meta->poster->empty()) {
        load_image(self->poster, *self->meta->poster, 200, 300);
    }
    
    // Info chips (year, rating, runtime, genres)
    // Clear existing chips
    GtkWidget *chip_child;
    while ((chip_child = gtk_widget_get_first_child(GTK_WIDGET(self->info_chips))) != nullptr) {
        gtk_box_remove(self->info_chips, chip_child);
    }
    
    if (self->meta->release_info.has_value() && !self->meta->release_info->empty()) {
        gtk_box_append(self->info_chips, create_info_chip(self->meta->release_info->c_str()));
    }
    
    if (self->meta->imdb_rating.has_value() && !self->meta->imdb_rating->empty()) {
        std::string rating = "★ " + *self->meta->imdb_rating;
        gtk_box_append(self->info_chips, create_info_chip(rating.c_str()));
    }
    
    if (self->meta->runtime.has_value() && !self->meta->runtime->empty()) {
        gtk_box_append(self->info_chips, create_info_chip(self->meta->runtime->c_str()));
    }
    
    if (!self->meta->genres.empty()) {
        std::string genres;
        for (size_t i = 0; i < std::min(self->meta->genres.size(), size_t(3)); i++) {
            if (i > 0) genres += ", ";
            genres += self->meta->genres[i];
        }
        gtk_box_append(self->info_chips, create_info_chip(genres.c_str()));
    }
    
    // Description
    if (self->meta->description.has_value() && !self->meta->description->empty()) {
        gtk_label_set_text(self->description_label, self->meta->description->c_str());
        gtk_widget_set_visible(GTK_WIDGET(self->description_label), TRUE);
    }
    
    // Details grid
    GtkWidget *detail_child;
    while ((detail_child = gtk_widget_get_first_child(GTK_WIDGET(self->details_grid))) != nullptr) {
        gtk_box_remove(self->details_grid, detail_child);
    }
    
    if (!self->meta->director.empty()) {
        std::string directors;
        for (size_t i = 0; i < self->meta->director.size(); i++) {
            if (i > 0) directors += ", ";
            directors += self->meta->director[i];
        }
        gtk_box_append(self->details_grid, create_detail_row("Director", directors));
    }
    
    if (!self->meta->writer.empty()) {
        std::string writers;
        for (size_t i = 0; i < std::min(self->meta->writer.size(), size_t(3)); i++) {
            if (i > 0) writers += ", ";
            writers += self->meta->writer[i];
        }
        gtk_box_append(self->details_grid, create_detail_row("Writers", writers));
    }
    
    if (self->meta->language.has_value() && !self->meta->language->empty()) {
        gtk_box_append(self->details_grid, create_detail_row("Language", *self->meta->language));
    }
    
    if (self->meta->country.has_value() && !self->meta->country->empty()) {
        gtk_box_append(self->details_grid, create_detail_row("Country", *self->meta->country));
    }
    
    if (self->meta->awards.has_value() && !self->meta->awards->empty()) {
        gtk_box_append(self->details_grid, create_detail_row("Awards", *self->meta->awards));
    }
    
    if (gtk_widget_get_first_child(GTK_WIDGET(self->details_grid))) {
        gtk_widget_set_visible(GTK_WIDGET(self->details_grid), TRUE);
    }
    
    // Cast section
    GtkWidget *cast_child;
    while ((cast_child = gtk_widget_get_first_child(GTK_WIDGET(self->cast_box))) != nullptr) {
        gtk_box_remove(self->cast_box, cast_child);
    }
    
    if (!self->meta->cast.empty()) {
        for (size_t i = 0; i < std::min(self->meta->cast.size(), size_t(10)); i++) {
            gtk_box_append(self->cast_box, create_cast_item(self->meta->cast[i], "Actor"));
        }
        gtk_widget_set_visible(GTK_WIDGET(self->cast_box), TRUE);
    }
    
    // Trailers section
    GtkWidget *trailer_child;
    while ((trailer_child = gtk_widget_get_first_child(GTK_WIDGET(self->trailers_box))) != nullptr) {
        gtk_box_remove(self->trailers_box, trailer_child);
    }
    
    if (!self->meta->trailers.empty()) {
        for (const auto& trailer : self->meta->trailers) {
            gtk_box_append(self->trailers_box, create_trailer_button(trailer));
        }
        gtk_widget_set_visible(GTK_WIDGET(self->trailers_box), TRUE);
    }
    
    // Handle series with seasons
    if (self->meta->type == "series" && !self->meta->videos.empty()) {
        populate_seasons(self);
    } else {
        gtk_widget_set_visible(GTK_WIDGET(self->seasons_box), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(self->episodes_box), FALSE);
    }
    
    gtk_stack_set_visible_child_name(self->main_stack, "content");
}

static void load_meta(MadariDetailView *self) {
    gtk_stack_set_visible_child_name(self->main_stack, "loading");
    
    self->addon_service->fetch_meta(
        *self->meta_type,
        *self->meta_id,
        [self](std::optional<Stremio::MetaResponse> response, const std::string& error) {
            if (response) {
                self->meta = new Stremio::Meta(response->meta);
                populate_ui(self);
            } else {
                gtk_stack_set_visible_child_name(self->main_stack, "error");
                g_warning("Failed to load meta: %s", error.c_str());
            }
        }
    );
}

static void madari_detail_view_dispose(GObject *object) {
    MadariDetailView *self = MADARI_DETAIL_VIEW(object);
    
    delete self->meta_id;
    delete self->meta_type;
    delete self->meta;
    delete self->seasons_map;
    delete self->season_numbers;
    
    self->meta_id = nullptr;
    self->meta_type = nullptr;
    self->meta = nullptr;
    self->seasons_map = nullptr;
    self->season_numbers = nullptr;
    self->season_model = nullptr;
    
    G_OBJECT_CLASS(madari_detail_view_parent_class)->dispose(object);
}

static void madari_detail_view_class_init(MadariDetailViewClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    
    object_class->dispose = madari_detail_view_dispose;
    
    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/media/madari/app/detail-view.ui"
    );
    
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, background_picture);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, poster);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, title_label);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, info_chips);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, description_label);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, action_buttons);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, details_grid);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, cast_box);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, trailers_box);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, seasons_box);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, episodes_box);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, episodes_section);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, season_dropdown);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, play_button);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, content_box);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, loading_spinner);
    gtk_widget_class_bind_template_child(widget_class, MadariDetailView, main_stack);
}

static void madari_detail_view_init(MadariDetailView *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
    
    self->meta_id = nullptr;
    self->meta_type = nullptr;
    self->meta = nullptr;
    self->addon_service = nullptr;
    self->current_season = 1;
    self->seasons_map = new std::map<int, std::vector<Stremio::Video>>();
    self->season_numbers = new std::vector<int>();
    self->season_model = nullptr;
    
    // Connect play button
    g_signal_connect(self->play_button, "clicked", G_CALLBACK(on_play_clicked), self);
}

MadariDetailView *madari_detail_view_new(Stremio::AddonService *addon_service,
                                          const char *meta_id,
                                          const char *meta_type) {
    MadariDetailView *view = MADARI_DETAIL_VIEW(g_object_new(
        MADARI_TYPE_DETAIL_VIEW,
        nullptr
    ));
    
    view->addon_service = addon_service;
    view->meta_id = new std::string(meta_id);
    view->meta_type = new std::string(meta_type);
    
    load_meta(view);
    
    return view;
}
