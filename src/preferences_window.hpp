#pragma once

#include <adwaita.h>
#include "stremio/stremio.hpp"
#include "trakt/trakt.hpp"

G_BEGIN_DECLS

#define MADARI_TYPE_PREFERENCES_WINDOW (madari_preferences_window_get_type())

G_DECLARE_FINAL_TYPE(MadariPreferencesWindow, madari_preferences_window, MADARI, PREFERENCES_WINDOW, AdwWindow)

MadariPreferencesWindow *madari_preferences_window_new(GtkWindow *parent, 
                                                        Stremio::AddonService *addon_service,
                                                        Trakt::TraktService *trakt_service);

G_END_DECLS
