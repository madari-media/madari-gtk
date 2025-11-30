#pragma once

#include <adwaita.h>
#include "stremio/stremio.hpp"
#include "trakt/trakt.hpp"
#include "watch_history.hpp"

G_BEGIN_DECLS

#define MADARI_TYPE_APPLICATION (madari_application_get_type())

G_DECLARE_FINAL_TYPE(MadariApplication, madari_application, MADARI, APPLICATION, AdwApplication)

MadariApplication *madari_application_new(void);

Stremio::AddonService* madari_application_get_addon_service(MadariApplication *app);

Madari::WatchHistoryService* madari_application_get_watch_history(MadariApplication *app);

Trakt::TraktService* madari_application_get_trakt_service(MadariApplication *app);

G_END_DECLS
