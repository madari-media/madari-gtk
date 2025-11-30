#pragma once

#include <adwaita.h>
#include "stremio/stremio.hpp"

G_BEGIN_DECLS

#define MADARI_TYPE_DETAIL_VIEW (madari_detail_view_get_type())

G_DECLARE_FINAL_TYPE(MadariDetailView, madari_detail_view, MADARI, DETAIL_VIEW, AdwNavigationPage)

MadariDetailView *madari_detail_view_new(Stremio::AddonService *addon_service,
                                          const char *meta_id,
                                          const char *meta_type);

G_END_DECLS
