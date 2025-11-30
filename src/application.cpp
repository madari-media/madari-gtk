#include "application.hpp"
#include "window.hpp"
#include "preferences_window.hpp"

struct _MadariApplication {
    AdwApplication parent_instance;
    
    Stremio::AddonService *addon_service;
    Madari::WatchHistoryService *watch_history;
    Trakt::TraktService *trakt_service;
};

G_DEFINE_TYPE(MadariApplication, madari_application, ADW_TYPE_APPLICATION)

static void on_preferences_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);
static void on_about_action(GSimpleAction *action, GVariant *parameter, gpointer user_data);

static void madari_application_activate(GApplication *app) {
    GtkWindow *window;

    g_assert(MADARI_IS_APPLICATION(app));

    window = gtk_application_get_active_window(GTK_APPLICATION(app));

    if (window == nullptr) {
        window = GTK_WINDOW(madari_window_new(MADARI_APPLICATION(app)));
    }

    gtk_window_present(window);
}

static void madari_application_startup(GApplication *app) {
    G_APPLICATION_CLASS(madari_application_parent_class)->startup(app);
    
    MadariApplication *self = MADARI_APPLICATION(app);
    
    // Load CSS styles
    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(css_provider, "/media/madari/app/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(css_provider);
    
    // Initialize addon service
    self->addon_service = new Stremio::AddonService();
    self->addon_service->load();
    
    // Initialize watch history service
    self->watch_history = new Madari::WatchHistoryService();
    self->watch_history->load();
    
    // Initialize Trakt service
    self->trakt_service = new Trakt::TraktService();
    self->trakt_service->load();
    
    // Add actions
    static const GActionEntry app_actions[] = {
        { "preferences", on_preferences_action, nullptr, nullptr, nullptr },
        { "about", on_about_action, nullptr, nullptr, nullptr },
    };
    
    g_action_map_add_action_entries(G_ACTION_MAP(app), app_actions, G_N_ELEMENTS(app_actions), app);
    
    // Set keyboard shortcuts
    const char *preferences_accels[] = { "<Control>comma", nullptr };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.preferences", preferences_accels);
}

static void madari_application_shutdown(GApplication *app) {
    MadariApplication *self = MADARI_APPLICATION(app);
    
    if (self->trakt_service) {
        delete self->trakt_service;
        self->trakt_service = nullptr;
    }
    
    if (self->watch_history) {
        delete self->watch_history;
        self->watch_history = nullptr;
    }
    
    if (self->addon_service) {
        delete self->addon_service;
        self->addon_service = nullptr;
    }
    
    G_APPLICATION_CLASS(madari_application_parent_class)->shutdown(app);
}

static void on_preferences_action([[maybe_unused]] GSimpleAction *action, 
                                   [[maybe_unused]] GVariant *parameter, 
                                   gpointer user_data) {
    MadariApplication *self = MADARI_APPLICATION(user_data);
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(self));
    
    MadariPreferencesWindow *prefs = madari_preferences_window_new(window, self->addon_service, self->trakt_service);
    gtk_window_present(GTK_WINDOW(prefs));
}

static void on_about_action([[maybe_unused]] GSimpleAction *action, 
                            [[maybe_unused]] GVariant *parameter, 
                            gpointer user_data) {
    MadariApplication *self = MADARI_APPLICATION(user_data);
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(self));
    
    const char *developers[] = {
        "Madari Team",
        nullptr
    };
    
    adw_show_about_dialog(GTK_WIDGET(window),
        "application-name", "Madari",
        "application-icon", "media-playback-start-symbolic",
        "version", "0.1.0",
        "copyright", "© 2024 Madari",
        "license-type", GTK_LICENSE_GPL_3_0,
        "developers", developers,
        "website", "https://madari.media",
        "issue-url", "https://github.com/madari/madari-gtk/issues",
        "comments", "A media streaming application with Stremio addon support",
        nullptr);
}

static void madari_application_class_init(MadariApplicationClass *klass) {
    GApplicationClass *app_class = G_APPLICATION_CLASS(klass);

    app_class->activate = madari_application_activate;
    app_class->startup = madari_application_startup;
    app_class->shutdown = madari_application_shutdown;
}

static void madari_application_init(MadariApplication *self) {
    self->addon_service = nullptr;
    self->watch_history = nullptr;
    self->trakt_service = nullptr;
}

MadariApplication *madari_application_new(void) {
    return MADARI_APPLICATION(g_object_new(
        MADARI_TYPE_APPLICATION,
        "application-id", "media.madari.app",
        "flags", G_APPLICATION_DEFAULT_FLAGS,
        nullptr
    ));
}

Stremio::AddonService* madari_application_get_addon_service(MadariApplication *app) {
    g_return_val_if_fail(MADARI_IS_APPLICATION(app), nullptr);
    return app->addon_service;
}

Madari::WatchHistoryService* madari_application_get_watch_history(MadariApplication *app) {
    g_return_val_if_fail(MADARI_IS_APPLICATION(app), nullptr);
    return app->watch_history;
}

Trakt::TraktService* madari_application_get_trakt_service(MadariApplication *app) {
    g_return_val_if_fail(MADARI_IS_APPLICATION(app), nullptr);
    return app->trakt_service;
}
