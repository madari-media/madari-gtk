#include "preferences_window.hpp"

struct _MadariPreferencesWindow {
    AdwWindow parent_instance;
    
    // Services
    Stremio::AddonService *addon_service;
    Trakt::TraktService *trakt_service;
    
    // UI elements - Addons page
    AdwPreferencesPage *addons_page;
    AdwPreferencesGroup *installed_addons_group;
    GtkListBox *addons_list;
    GtkButton *add_addon_button;
    
    // Add addon dialog elements
    AdwDialog *add_addon_dialog;
    AdwEntryRow *addon_url_entry;
    GtkButton *install_button;
    GtkSpinner *install_spinner;
    GtkLabel *install_error_label;
    
    // View stack (from template)
    AdwViewStack *view_stack;
    
    // Trakt UI elements (created programmatically)
    AdwPreferencesPage *trakt_page;

    AdwPreferencesGroup *trakt_account_group;
    AdwPreferencesGroup *trakt_sync_group;

    AdwActionRow *trakt_account_row;
    GtkButton *trakt_login_btn;
    GtkButton *trakt_logout_btn;
    AdwSwitchRow *trakt_sync_watchlist_switch;
    AdwSwitchRow *trakt_sync_history_switch;
    AdwSwitchRow *trakt_sync_progress_switch;
    
    // Trakt auth dialog elements
    AdwDialog *trakt_auth_dialog;
    GtkLabel *trakt_auth_code_label;
    GtkLabel *trakt_auth_url_label;
    GtkSpinner *trakt_auth_spinner;
    GtkLabel *trakt_auth_status_label;
    guint trakt_poll_timeout_id;
    std::string *trakt_device_code;
};

G_DEFINE_TYPE(MadariPreferencesWindow, madari_preferences_window, ADW_TYPE_WINDOW)

static void refresh_addons_list(MadariPreferencesWindow *self);
static void on_add_addon_clicked(GtkButton *button, MadariPreferencesWindow *self);
static void on_install_addon_clicked(GtkButton *button, MadariPreferencesWindow *self);
static void on_addon_url_changed(AdwEntryRow *entry, MadariPreferencesWindow *self);

static GtkWidget* create_addon_row(MadariPreferencesWindow *self, const Stremio::InstalledAddon& addon) {
    AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
    
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), addon.manifest.name.c_str());
    adw_action_row_set_subtitle(row, addon.manifest.description.c_str());
    
    // Enable/disable switch
    GtkSwitch *enable_switch = GTK_SWITCH(gtk_switch_new());
    gtk_switch_set_active(enable_switch, addon.enabled);
    gtk_widget_set_valign(GTK_WIDGET(enable_switch), GTK_ALIGN_CENTER);
    
    std::string *addon_id = new std::string(addon.manifest.id);
    g_object_set_data_full(G_OBJECT(enable_switch), "addon-id", addon_id,
                           [](gpointer data) { delete static_cast<std::string*>(data); });
    g_object_set_data(G_OBJECT(enable_switch), "prefs-window", self);
    
    g_signal_connect(enable_switch, "state-set",
                     G_CALLBACK(+[](GtkSwitch *sw, gboolean state, gpointer user_data) -> gboolean {
                         auto *self = static_cast<MadariPreferencesWindow*>(
                             g_object_get_data(G_OBJECT(sw), "prefs-window"));
                         auto *addon_id = static_cast<std::string*>(
                             g_object_get_data(G_OBJECT(sw), "addon-id"));
                         if (self && addon_id) {
                             self->addon_service->set_addon_enabled(*addon_id, state);
                         }
                         return FALSE;
                     }), nullptr);
    
    adw_action_row_add_suffix(row, GTK_WIDGET(enable_switch));
    
    // Remove button
    GtkButton *remove_button = GTK_BUTTON(gtk_button_new_from_icon_name("user-trash-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(remove_button), "flat");
    gtk_widget_set_valign(GTK_WIDGET(remove_button), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(remove_button), "Remove addon");
    
    std::string *remove_addon_id = new std::string(addon.manifest.id);
    g_object_set_data_full(G_OBJECT(remove_button), "addon-id", remove_addon_id,
                           [](gpointer data) { delete static_cast<std::string*>(data); });
    g_object_set_data(G_OBJECT(remove_button), "prefs-window", self);
    
    g_signal_connect(remove_button, "clicked",
                     G_CALLBACK(+[](GtkButton *btn, gpointer user_data) {
                         auto *self = static_cast<MadariPreferencesWindow*>(
                             g_object_get_data(G_OBJECT(btn), "prefs-window"));
                         auto *addon_id = static_cast<std::string*>(
                             g_object_get_data(G_OBJECT(btn), "addon-id"));
                         if (self && addon_id) {
                             self->addon_service->uninstall_addon(*addon_id);
                             refresh_addons_list(self);
                         }
                     }), nullptr);
    
    adw_action_row_add_suffix(row, GTK_WIDGET(remove_button));
    
    // Show addon types and resources as badges
    std::string info;
    for (const auto& type : addon.manifest.types) {
        if (!info.empty()) info += ", ";
        info += type;
    }
    if (!info.empty()) {
        adw_action_row_set_subtitle(row, (addon.manifest.description + "\n" + "Types: " + info).c_str());
    }
    
    return GTK_WIDGET(row);
}

static void refresh_addons_list(MadariPreferencesWindow *self) {
    // Clear existing rows
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self->addons_list))) != nullptr) {
        gtk_list_box_remove(self->addons_list, child);
    }
    
    const auto& addons = self->addon_service->get_installed_addons();
    
    if (addons.empty()) {
        // Show placeholder
        AdwActionRow *placeholder = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(placeholder), "No addons installed");
        adw_action_row_set_subtitle(placeholder, "Click the + button to add a Stremio addon");
        gtk_widget_set_sensitive(GTK_WIDGET(placeholder), FALSE);
        gtk_list_box_append(self->addons_list, GTK_WIDGET(placeholder));
    } else {
        for (const auto& addon : addons) {
            GtkWidget *row = create_addon_row(self, addon);
            gtk_list_box_append(self->addons_list, row);
        }
    }
}

static void show_add_addon_dialog(MadariPreferencesWindow *self) {
    // Create dialog content
    GtkWidget *dialog_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(dialog_content, 12);
    gtk_widget_set_margin_end(dialog_content, 12);
    gtk_widget_set_margin_top(dialog_content, 12);
    gtk_widget_set_margin_bottom(dialog_content, 12);
    
    // URL entry
    AdwPreferencesGroup *group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, "Addon URL");
    adw_preferences_group_set_description(group, "Enter the manifest URL of the Stremio addon");
    
    self->addon_url_entry = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->addon_url_entry), "URL");
    adw_entry_row_set_input_purpose(self->addon_url_entry, GTK_INPUT_PURPOSE_URL);
    gtk_editable_set_text(GTK_EDITABLE(self->addon_url_entry), "");
    
    g_signal_connect(self->addon_url_entry, "changed",
                     G_CALLBACK(on_addon_url_changed), self);
    
    adw_preferences_group_add(group, GTK_WIDGET(self->addon_url_entry));
    gtk_box_append(GTK_BOX(dialog_content), GTK_WIDGET(group));
    
    // Error label
    self->install_error_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->install_error_label), "error");
    gtk_widget_set_visible(GTK_WIDGET(self->install_error_label), FALSE);
    gtk_box_append(GTK_BOX(dialog_content), GTK_WIDGET(self->install_error_label));
    
    // Buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_widget_set_margin_top(button_box, 12);
    
    GtkButton *cancel_button = GTK_BUTTON(gtk_button_new_with_label("Cancel"));
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(cancel_button));
    
    self->install_button = GTK_BUTTON(gtk_button_new_with_label("Install"));
    gtk_widget_add_css_class(GTK_WIDGET(self->install_button), "suggested-action");
    gtk_widget_set_sensitive(GTK_WIDGET(self->install_button), FALSE);
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(self->install_button));
    
    self->install_spinner = GTK_SPINNER(gtk_spinner_new());
    gtk_widget_set_visible(GTK_WIDGET(self->install_spinner), FALSE);
    gtk_box_append(GTK_BOX(button_box), GTK_WIDGET(self->install_spinner));
    
    gtk_box_append(GTK_BOX(dialog_content), button_box);
    
    // Create dialog
    self->add_addon_dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(self->add_addon_dialog, "Add Addon");
    adw_dialog_set_content_width(self->add_addon_dialog, 400);
    adw_dialog_set_content_height(self->add_addon_dialog, 200);
    adw_dialog_set_child(self->add_addon_dialog, dialog_content);
    
    // Connect signals
    g_signal_connect_swapped(cancel_button, "clicked",
                             G_CALLBACK(adw_dialog_close), self->add_addon_dialog);
    g_signal_connect(self->install_button, "clicked",
                     G_CALLBACK(on_install_addon_clicked), self);
    
    adw_dialog_present(self->add_addon_dialog, GTK_WIDGET(self));
}

static void on_add_addon_clicked([[maybe_unused]] GtkButton *button, MadariPreferencesWindow *self) {
    show_add_addon_dialog(self);
}

static void on_addon_url_changed([[maybe_unused]] AdwEntryRow *entry, MadariPreferencesWindow *self) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(self->addon_url_entry));
    gboolean valid = text && strlen(text) > 0 && 
                     (g_str_has_prefix(text, "http://") || g_str_has_prefix(text, "https://"));
    gtk_widget_set_sensitive(GTK_WIDGET(self->install_button), valid);
    gtk_widget_set_visible(GTK_WIDGET(self->install_error_label), FALSE);
}

static void on_install_addon_clicked([[maybe_unused]] GtkButton *button, MadariPreferencesWindow *self) {
    const char *url = gtk_editable_get_text(GTK_EDITABLE(self->addon_url_entry));
    
    gtk_widget_set_sensitive(GTK_WIDGET(self->install_button), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(self->addon_url_entry), FALSE);
    gtk_spinner_start(self->install_spinner);
    gtk_widget_set_visible(GTK_WIDGET(self->install_spinner), TRUE);
    gtk_widget_set_visible(GTK_WIDGET(self->install_error_label), FALSE);
    
    self->addon_service->install_addon(url, 
        [self](bool success, const std::string& error) {
            // This callback runs on the main thread via GLib's async mechanisms
            gtk_spinner_stop(self->install_spinner);
            gtk_widget_set_visible(GTK_WIDGET(self->install_spinner), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(self->addon_url_entry), TRUE);
            
            if (success) {
                adw_dialog_close(self->add_addon_dialog);
                refresh_addons_list(self);
            } else {
                gtk_label_set_text(self->install_error_label, error.c_str());
                gtk_widget_set_visible(GTK_WIDGET(self->install_error_label), TRUE);
                gtk_widget_set_sensitive(GTK_WIDGET(self->install_button), TRUE);
            }
        });
}

// ============ Trakt UI Functions ============

static void update_trakt_account_ui(MadariPreferencesWindow *self);
static void on_trakt_login_clicked(GtkButton *btn, MadariPreferencesWindow *self);
static void on_trakt_logout_clicked(GtkButton *btn, MadariPreferencesWindow *self);
static void on_trakt_sync_switch_changed(GObject *obj, GParamSpec *pspec, MadariPreferencesWindow *self);

static void update_trakt_account_ui(MadariPreferencesWindow *self) {
    if (!self->trakt_service) return;
    
    const auto& config = self->trakt_service->get_config();
    bool is_authenticated = self->trakt_service->is_authenticated();
    
    // Update account row
    if (is_authenticated && config.username.has_value()) {
        std::string account_text = "Connected as " + *config.username;
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->trakt_account_row), account_text.c_str());
        adw_action_row_set_subtitle(self->trakt_account_row, "Your Trakt account is connected");
        gtk_widget_set_visible(GTK_WIDGET(self->trakt_login_btn), FALSE);
        gtk_widget_set_visible(GTK_WIDGET(self->trakt_logout_btn), TRUE);
    } else {
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->trakt_account_row), "Not connected");
        adw_action_row_set_subtitle(self->trakt_account_row, "Click Login to connect your Trakt account");
        gtk_widget_set_visible(GTK_WIDGET(self->trakt_login_btn), TRUE);
        gtk_widget_set_visible(GTK_WIDGET(self->trakt_logout_btn), FALSE);
    }
    
    // Update sync switches
    adw_switch_row_set_active(self->trakt_sync_watchlist_switch, config.sync_watchlist);
    adw_switch_row_set_active(self->trakt_sync_history_switch, config.sync_history);
    adw_switch_row_set_active(self->trakt_sync_progress_switch, config.sync_progress);
    
    // Enable/disable sync group based on auth status
    gtk_widget_set_sensitive(GTK_WIDGET(self->trakt_sync_group), is_authenticated);
}

static gboolean trakt_poll_device_token(gpointer user_data);

static void start_trakt_device_auth(MadariPreferencesWindow *self) {
    self->trakt_service->start_device_auth([self](std::optional<Trakt::DeviceCode> code, const std::string& error) {
        if (!code) {
            gtk_label_set_text(self->trakt_auth_status_label, error.c_str());
            gtk_widget_add_css_class(GTK_WIDGET(self->trakt_auth_status_label), "error");
            gtk_widget_set_visible(GTK_WIDGET(self->trakt_auth_status_label), TRUE);
            gtk_spinner_stop(self->trakt_auth_spinner);
            return;
        }
        
        // Show the code and URL
        gtk_label_set_text(self->trakt_auth_code_label, code->user_code.c_str());
        
        std::string url_markup = "<a href=\"" + code->verification_url + "\">" + code->verification_url + "</a>";
        gtk_label_set_markup(self->trakt_auth_url_label, url_markup.c_str());
        
        gtk_label_set_text(self->trakt_auth_status_label, "Enter the code above on the Trakt website");
        gtk_widget_remove_css_class(GTK_WIDGET(self->trakt_auth_status_label), "error");
        gtk_widget_set_visible(GTK_WIDGET(self->trakt_auth_status_label), TRUE);
        
        // Store device code for polling
        if (self->trakt_device_code) delete self->trakt_device_code;
        self->trakt_device_code = new std::string(code->device_code);
        
        // Start polling
        int interval = code->interval > 0 ? code->interval : 5;
        self->trakt_poll_timeout_id = g_timeout_add_seconds(interval, trakt_poll_device_token, self);
    });
}

static gboolean trakt_poll_device_token(gpointer user_data) {
    MadariPreferencesWindow *self = MADARI_PREFERENCES_WINDOW(user_data);
    
    if (!self->trakt_device_code) {
        self->trakt_poll_timeout_id = 0;
        return G_SOURCE_REMOVE;
    }
    
    self->trakt_service->poll_device_token(*self->trakt_device_code, 
        [self](bool success, bool pending, const std::string& error) {
            if (success) {
                // Auth successful!
                gtk_spinner_stop(self->trakt_auth_spinner);
                gtk_label_set_text(self->trakt_auth_status_label, "Successfully authenticated!");
                gtk_widget_add_css_class(GTK_WIDGET(self->trakt_auth_status_label), "success");
                
                // Stop polling
                if (self->trakt_poll_timeout_id > 0) {
                    g_source_remove(self->trakt_poll_timeout_id);
                    self->trakt_poll_timeout_id = 0;
                }
                
                // Close dialog after a short delay
                g_timeout_add(1500, [](gpointer data) -> gboolean {
                    MadariPreferencesWindow *self = MADARI_PREFERENCES_WINDOW(data);
                    if (self->trakt_auth_dialog) {
                        adw_dialog_close(self->trakt_auth_dialog);
                    }
                    update_trakt_account_ui(self);
                    return G_SOURCE_REMOVE;
                }, self);
                
            } else if (!pending) {
                // Auth failed or expired
                gtk_spinner_stop(self->trakt_auth_spinner);
                gtk_label_set_text(self->trakt_auth_status_label, error.c_str());
                gtk_widget_add_css_class(GTK_WIDGET(self->trakt_auth_status_label), "error");
                
                if (self->trakt_poll_timeout_id > 0) {
                    g_source_remove(self->trakt_poll_timeout_id);
                    self->trakt_poll_timeout_id = 0;
                }
            }
            // If pending, continue polling (timeout will re-trigger)
        });
    
    return G_SOURCE_CONTINUE;
}

static void on_trakt_login_clicked([[maybe_unused]] GtkButton *btn, MadariPreferencesWindow *self) {
    // Create auth dialog
    self->trakt_auth_dialog = ADW_DIALOG(adw_dialog_new());
    adw_dialog_set_title(self->trakt_auth_dialog, "Trakt Authentication");
    adw_dialog_set_content_width(self->trakt_auth_dialog, 400);
    adw_dialog_set_content_height(self->trakt_auth_dialog, 300);
    
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);
    
    // Instructions
    GtkWidget *instructions = gtk_label_new("Go to the URL below and enter this code:");
    gtk_widget_add_css_class(instructions, "dim-label");
    gtk_box_append(GTK_BOX(content), instructions);
    
    // Code display (large, copyable)
    self->trakt_auth_code_label = GTK_LABEL(gtk_label_new("Loading..."));
    gtk_widget_add_css_class(GTK_WIDGET(self->trakt_auth_code_label), "title-1");
    gtk_label_set_selectable(self->trakt_auth_code_label, TRUE);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->trakt_auth_code_label));
    
    // URL (clickable link)
    self->trakt_auth_url_label = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_use_markup(self->trakt_auth_url_label, TRUE);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->trakt_auth_url_label));
    
    // Spinner
    self->trakt_auth_spinner = GTK_SPINNER(gtk_spinner_new());
    gtk_spinner_start(self->trakt_auth_spinner);
    gtk_widget_set_halign(GTK_WIDGET(self->trakt_auth_spinner), GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->trakt_auth_spinner));
    
    // Status label
    self->trakt_auth_status_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_visible(GTK_WIDGET(self->trakt_auth_status_label), FALSE);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(self->trakt_auth_status_label));
    
    // Cancel button
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    gtk_widget_set_halign(cancel_btn, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(cancel_btn, 16);
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(adw_dialog_close), self->trakt_auth_dialog);
    gtk_box_append(GTK_BOX(content), cancel_btn);
    
    adw_dialog_set_child(self->trakt_auth_dialog, content);
    
    // Handle dialog close to cleanup
    g_signal_connect(self->trakt_auth_dialog, "closed", G_CALLBACK(+[](AdwDialog*, gpointer user_data) {
        MadariPreferencesWindow *self = MADARI_PREFERENCES_WINDOW(user_data);
        if (self->trakt_poll_timeout_id > 0) {
            g_source_remove(self->trakt_poll_timeout_id);
            self->trakt_poll_timeout_id = 0;
        }
        if (self->trakt_device_code) {
            delete self->trakt_device_code;
            self->trakt_device_code = nullptr;
        }
    }), self);
    
    adw_dialog_present(self->trakt_auth_dialog, GTK_WIDGET(self));
    
    // Start the device auth flow
    start_trakt_device_auth(self);
}

static void on_trakt_logout_clicked([[maybe_unused]] GtkButton *btn, MadariPreferencesWindow *self) {
    self->trakt_service->logout([self](bool success, const std::string& error) {
        update_trakt_account_ui(self);
    });
}

static void on_trakt_sync_switch_changed([[maybe_unused]] GObject *obj, 
                                          [[maybe_unused]] GParamSpec *pspec, 
                                          MadariPreferencesWindow *self) {
    if (!self->trakt_service) return;
    
    auto config = self->trakt_service->get_config();
    config.sync_watchlist = adw_switch_row_get_active(self->trakt_sync_watchlist_switch);
    config.sync_history = adw_switch_row_get_active(self->trakt_sync_history_switch);
    config.sync_progress = adw_switch_row_get_active(self->trakt_sync_progress_switch);
    self->trakt_service->set_config(config);
}

static void create_trakt_page(MadariPreferencesWindow *self) {
    // Create Trakt preferences page
    self->trakt_page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    adw_preferences_page_set_title(self->trakt_page, "Trakt");
    adw_preferences_page_set_icon_name(self->trakt_page, "emblem-synchronizing-symbolic");
    
    // Account group
    self->trakt_account_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(self->trakt_account_group, "Account");
    adw_preferences_group_set_description(self->trakt_account_group, 
        "Connect your Trakt account to sync watchlist, history, and playback progress");
    
    self->trakt_account_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->trakt_account_row), "Not connected");
    adw_action_row_set_subtitle(self->trakt_account_row, "Click Login to connect your Trakt account");
    
    // Login button
    self->trakt_login_btn = GTK_BUTTON(gtk_button_new_with_label("Login"));
    gtk_widget_add_css_class(GTK_WIDGET(self->trakt_login_btn), "suggested-action");
    gtk_widget_set_valign(GTK_WIDGET(self->trakt_login_btn), GTK_ALIGN_CENTER);
    g_signal_connect(self->trakt_login_btn, "clicked", G_CALLBACK(on_trakt_login_clicked), self);
    adw_action_row_add_suffix(self->trakt_account_row, GTK_WIDGET(self->trakt_login_btn));
    
    // Logout button
    self->trakt_logout_btn = GTK_BUTTON(gtk_button_new_with_label("Logout"));
    gtk_widget_add_css_class(GTK_WIDGET(self->trakt_logout_btn), "destructive-action");
    gtk_widget_set_valign(GTK_WIDGET(self->trakt_logout_btn), GTK_ALIGN_CENTER);
    gtk_widget_set_visible(GTK_WIDGET(self->trakt_logout_btn), FALSE);
    g_signal_connect(self->trakt_logout_btn, "clicked", G_CALLBACK(on_trakt_logout_clicked), self);
    adw_action_row_add_suffix(self->trakt_account_row, GTK_WIDGET(self->trakt_logout_btn));
    
    adw_preferences_group_add(self->trakt_account_group, GTK_WIDGET(self->trakt_account_row));
    adw_preferences_page_add(self->trakt_page, self->trakt_account_group);
    
    // Sync settings group
    self->trakt_sync_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(self->trakt_sync_group, "Sync Settings");
    adw_preferences_group_set_description(self->trakt_sync_group, "Choose what to sync with Trakt");
    
    self->trakt_sync_watchlist_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->trakt_sync_watchlist_switch), "Sync Watchlist");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->trakt_sync_watchlist_switch), 
        "Show your Trakt watchlist in the app");
    g_signal_connect(self->trakt_sync_watchlist_switch, "notify::active", 
                     G_CALLBACK(on_trakt_sync_switch_changed), self);
    adw_preferences_group_add(self->trakt_sync_group, GTK_WIDGET(self->trakt_sync_watchlist_switch));
    
    self->trakt_sync_history_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->trakt_sync_history_switch), "Sync History");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->trakt_sync_history_switch), 
        "Mark items as watched on Trakt when you finish watching");
    g_signal_connect(self->trakt_sync_history_switch, "notify::active", 
                     G_CALLBACK(on_trakt_sync_switch_changed), self);
    adw_preferences_group_add(self->trakt_sync_group, GTK_WIDGET(self->trakt_sync_history_switch));
    
    self->trakt_sync_progress_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->trakt_sync_progress_switch), "Sync Playback Progress");
    adw_action_row_set_subtitle(ADW_ACTION_ROW(self->trakt_sync_progress_switch), 
        "Sync your watch progress with Trakt (scrobbling)");
    g_signal_connect(self->trakt_sync_progress_switch, "notify::active", 
                     G_CALLBACK(on_trakt_sync_switch_changed), self);
    adw_preferences_group_add(self->trakt_sync_group, GTK_WIDGET(self->trakt_sync_progress_switch));
    
    gtk_widget_set_sensitive(GTK_WIDGET(self->trakt_sync_group), FALSE);
    adw_preferences_page_add(self->trakt_page, self->trakt_sync_group);
}

// ============ End Trakt UI Functions ============

static void madari_preferences_window_class_init(MadariPreferencesWindowClass *klass) {
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    
    gtk_widget_class_set_template_from_resource(
        widget_class,
        "/media/madari/app/preferences-window.ui"
    );
    
    gtk_widget_class_bind_template_child(widget_class, MadariPreferencesWindow, view_stack);
    gtk_widget_class_bind_template_child(widget_class, MadariPreferencesWindow, addons_page);
    gtk_widget_class_bind_template_child(widget_class, MadariPreferencesWindow, installed_addons_group);
    gtk_widget_class_bind_template_child(widget_class, MadariPreferencesWindow, addons_list);
    gtk_widget_class_bind_template_child(widget_class, MadariPreferencesWindow, add_addon_button);
    
    gtk_widget_class_bind_template_callback(widget_class, on_add_addon_clicked);
}

static void madari_preferences_window_init(MadariPreferencesWindow *self) {
    gtk_widget_init_template(GTK_WIDGET(self));
    
    // Initialize Trakt-related pointers
    self->trakt_service = nullptr;
    self->trakt_poll_timeout_id = 0;
    self->trakt_device_code = nullptr;
    
    // Connect add button signal
    g_signal_connect(self->add_addon_button, "clicked",
                     G_CALLBACK(on_add_addon_clicked), self);
}

MadariPreferencesWindow *madari_preferences_window_new(GtkWindow *parent, 
                                                        Stremio::AddonService *addon_service,
                                                        Trakt::TraktService *trakt_service) {
    MadariPreferencesWindow *window = MADARI_PREFERENCES_WINDOW(g_object_new(
        MADARI_TYPE_PREFERENCES_WINDOW,
        "transient-for", parent,
        nullptr
    ));
    
    window->addon_service = addon_service;
    window->trakt_service = trakt_service;
    
    // Subscribe to addon changes
    addon_service->on_addons_changed([window]() {
        refresh_addons_list(window);
    });
    
    // Initial refresh
    refresh_addons_list(window);
    
    // Create and add Trakt page to the view stack
    create_trakt_page(window);
    
    // Add Trakt page to the view stack
    if (window->view_stack) {
        adw_view_stack_add_titled_with_icon(window->view_stack, 
                                             GTK_WIDGET(window->trakt_page),
                                             "trakt", "Trakt", 
                                             "emblem-synchronizing-symbolic");
    }
    
    // Subscribe to Trakt config changes and update UI
    if (trakt_service) {
        trakt_service->on_config_changed([window]() {
            update_trakt_account_ui(window);
        });
        
        // Initial Trakt UI update
        update_trakt_account_ui(window);
    }
    
    return window;
}
