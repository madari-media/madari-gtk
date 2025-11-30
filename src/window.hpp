#pragma once

#include <adwaita.h>
#include <vector>
#include <string>
#include <utility>
#include "application.hpp"

G_BEGIN_DECLS

#define MADARI_TYPE_WINDOW (madari_window_get_type())

G_DECLARE_FINAL_TYPE(MadariWindow, madari_window, MADARI, WINDOW, AdwApplicationWindow)

MadariWindow *madari_window_new(MadariApplication *app);

void madari_window_refresh_catalogs(MadariWindow *self);

void madari_window_show_detail(MadariWindow *self, const char *meta_id, const char *meta_type);

// Player functions
void madari_window_play_video(MadariWindow *self, const char *url, const char *title);
void madari_window_play_episode(MadariWindow *self, const char *url, const char *title, 
                                 const char *meta_id, const char *meta_type,
                                 const char *video_id, const char *binge_group,
                                 const char *poster_url = nullptr, int episode_num = 0);
// EpisodeInfo structure for episode navigation
struct MadariEpisodeInfo {
    std::string video_id;
    std::string title;
    int episode;
};

void madari_window_set_episode_list(MadariWindow *self, 
                                     const std::vector<MadariEpisodeInfo>& episodes,
                                     int current_index,
                                     const char *series_title,
                                     int season);
void madari_window_stop_video(MadariWindow *self);
gboolean madari_window_is_playing(MadariWindow *self);

G_END_DECLS
