#include "watch_history.hpp"
#include <json-glib/json-glib.h>
#include <glib.h>
#include <algorithm>
#include <cstdio>
#include <set>

namespace Madari {

// Helper function to format time
static std::string format_time(double seconds) {
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

std::string WatchHistoryEntry::get_progress_string() const {
    return format_time(position) + " / " + format_time(duration);
}

std::string WatchHistoryEntry::get_remaining_string() const {
    double remaining = duration - position;
    if (remaining <= 0) return "Finished";
    
    int mins = static_cast<int>(remaining / 60);
    if (mins >= 60) {
        int hours = mins / 60;
        mins = mins % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "%dh %dm left", hours, mins);
        return buf;
    } else if (mins > 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%dm left", mins);
        return buf;
    } else {
        return "< 1m left";
    }
}

WatchHistoryService::WatchHistoryService() {
    storage_path_ = get_storage_path();
}

WatchHistoryService::~WatchHistoryService() {
    // Save on destruction
    save();
}

std::string WatchHistoryService::get_storage_path() {
    const char *data_dir = g_get_user_data_dir();
    std::string app_dir = std::string(data_dir) + "/madari";
    g_mkdir_with_parents(app_dir.c_str(), 0755);
    return app_dir + "/watch_history.json";
}

void WatchHistoryService::load() {
    history_.clear();
    
    if (!g_file_test(storage_path_.c_str(), G_FILE_TEST_EXISTS)) {
        return;
    }
    
    g_autoptr(GError) error = nullptr;
    g_autoptr(JsonParser) parser = json_parser_new();
    
    if (!json_parser_load_from_file(parser, storage_path_.c_str(), &error)) {
        g_warning("Failed to load watch history: %s", error->message);
        return;
    }
    
    JsonNode *root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
        g_warning("Invalid watch history format");
        return;
    }
    
    JsonArray *array = json_node_get_array(root);
    guint length = json_array_get_length(array);
    
    for (guint i = 0; i < length; i++) {
        JsonNode *node = json_array_get_element(array, i);
        if (!JSON_NODE_HOLDS_OBJECT(node)) continue;
        
        JsonObject *obj = json_node_get_object(node);
        
        WatchHistoryEntry entry;
        
        if (json_object_has_member(obj, "meta_id"))
            entry.meta_id = json_object_get_string_member(obj, "meta_id");
        if (json_object_has_member(obj, "meta_type"))
            entry.meta_type = json_object_get_string_member(obj, "meta_type");
        if (json_object_has_member(obj, "video_id"))
            entry.video_id = json_object_get_string_member(obj, "video_id");
        if (json_object_has_member(obj, "title"))
            entry.title = json_object_get_string_member(obj, "title");
        if (json_object_has_member(obj, "poster_url"))
            entry.poster_url = json_object_get_string_member(obj, "poster_url");
        
        if (json_object_has_member(obj, "series_title")) {
            const char *val = json_object_get_string_member(obj, "series_title");
            if (val && strlen(val) > 0) entry.series_title = val;
        }
        if (json_object_has_member(obj, "season"))
            entry.season = json_object_get_int_member(obj, "season");
        if (json_object_has_member(obj, "episode"))
            entry.episode = json_object_get_int_member(obj, "episode");
        
        if (json_object_has_member(obj, "position"))
            entry.position = json_object_get_double_member(obj, "position");
        if (json_object_has_member(obj, "duration"))
            entry.duration = json_object_get_double_member(obj, "duration");
        if (json_object_has_member(obj, "last_watched"))
            entry.last_watched = json_object_get_int_member(obj, "last_watched");
        
        if (json_object_has_member(obj, "binge_group")) {
            const char *val = json_object_get_string_member(obj, "binge_group");
            if (val && strlen(val) > 0) entry.binge_group = val;
        }
        
        // Only add valid entries
        if (!entry.meta_id.empty() && !entry.video_id.empty()) {
            history_.push_back(entry);
        }
    }
    
    // Sort by last_watched (most recent first)
    std::sort(history_.begin(), history_.end(), 
        [](const WatchHistoryEntry& a, const WatchHistoryEntry& b) {
            return a.last_watched > b.last_watched;
        });
}

void WatchHistoryService::save() {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    
    json_builder_begin_array(builder);
    
    for (const auto& entry : history_) {
        json_builder_begin_object(builder);
        
        json_builder_set_member_name(builder, "meta_id");
        json_builder_add_string_value(builder, entry.meta_id.c_str());
        
        json_builder_set_member_name(builder, "meta_type");
        json_builder_add_string_value(builder, entry.meta_type.c_str());
        
        json_builder_set_member_name(builder, "video_id");
        json_builder_add_string_value(builder, entry.video_id.c_str());
        
        json_builder_set_member_name(builder, "title");
        json_builder_add_string_value(builder, entry.title.c_str());
        
        json_builder_set_member_name(builder, "poster_url");
        json_builder_add_string_value(builder, entry.poster_url.c_str());
        
        if (entry.series_title.has_value()) {
            json_builder_set_member_name(builder, "series_title");
            json_builder_add_string_value(builder, entry.series_title->c_str());
        }
        
        if (entry.season.has_value()) {
            json_builder_set_member_name(builder, "season");
            json_builder_add_int_value(builder, *entry.season);
        }
        
        if (entry.episode.has_value()) {
            json_builder_set_member_name(builder, "episode");
            json_builder_add_int_value(builder, *entry.episode);
        }
        
        json_builder_set_member_name(builder, "position");
        json_builder_add_double_value(builder, entry.position);
        
        json_builder_set_member_name(builder, "duration");
        json_builder_add_double_value(builder, entry.duration);
        
        json_builder_set_member_name(builder, "last_watched");
        json_builder_add_int_value(builder, entry.last_watched);
        
        if (entry.binge_group.has_value()) {
            json_builder_set_member_name(builder, "binge_group");
            json_builder_add_string_value(builder, entry.binge_group->c_str());
        }
        
        json_builder_end_object(builder);
    }
    
    json_builder_end_array(builder);
    
    g_autoptr(JsonGenerator) generator = json_generator_new();
    json_generator_set_pretty(generator, TRUE);
    
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    
    g_autoptr(GError) error = nullptr;
    if (!json_generator_to_file(generator, storage_path_.c_str(), &error)) {
        g_warning("Failed to save watch history: %s", error->message);
    }
    
    json_node_unref(root);
}

int WatchHistoryService::find_entry_index(const std::string& meta_id, 
                                          const std::string& video_id) const {
    for (size_t i = 0; i < history_.size(); i++) {
        if (history_[i].meta_id == meta_id && history_[i].video_id == video_id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void WatchHistoryService::update_progress(const WatchHistoryEntry& entry) {
    int idx = find_entry_index(entry.meta_id, entry.video_id);
    
    if (idx >= 0) {
        // Update existing entry
        history_[idx] = entry;
        history_[idx].last_watched = std::time(nullptr);
        
        // Move to front (most recent)
        if (idx > 0) {
            WatchHistoryEntry moved = history_[idx];
            history_.erase(history_.begin() + idx);
            history_.insert(history_.begin(), moved);
        }
    } else {
        // Add new entry at the front
        WatchHistoryEntry new_entry = entry;
        new_entry.last_watched = std::time(nullptr);
        history_.insert(history_.begin(), new_entry);
        
        // Limit history size to 500 entries
        if (history_.size() > 500) {
            history_.resize(500);
        }
    }
    
    notify_change();
}

void WatchHistoryService::update_position(const std::string& meta_id, 
                                          const std::string& video_id,
                                          double position, double duration) {
    int idx = find_entry_index(meta_id, video_id);
    
    if (idx >= 0) {
        history_[idx].position = position;
        history_[idx].duration = duration;
        history_[idx].last_watched = std::time(nullptr);
        
        // Don't move to front for position updates (too frequent)
        // Just save periodically
    }
    // If entry doesn't exist, do nothing - a full update_progress is needed first
}

std::optional<WatchHistoryEntry> WatchHistoryService::get_entry(
    const std::string& meta_id, const std::string& video_id) const {
    
    int idx = find_entry_index(meta_id, video_id);
    if (idx >= 0) {
        return history_[idx];
    }
    return std::nullopt;
}

std::optional<WatchHistoryEntry> WatchHistoryService::get_latest_for_series(
    const std::string& meta_id) const {
    
    // History is already sorted by last_watched, so first match is latest
    for (const auto& entry : history_) {
        if (entry.meta_id == meta_id) {
            return entry;
        }
    }
    return std::nullopt;
}

std::vector<WatchHistoryEntry> WatchHistoryService::get_continue_watching(int limit) const {
    std::vector<WatchHistoryEntry> result;
    std::set<std::string> seen_series;  // Track series to avoid duplicates
    
    for (const auto& entry : history_) {
        if (result.size() >= static_cast<size_t>(limit)) break;
        
        // Only include resumable content
        if (!entry.is_resumable()) continue;
        
        // For series, only include the most recent episode
        if (entry.meta_type == "series") {
            if (seen_series.count(entry.meta_id) > 0) continue;
            seen_series.insert(entry.meta_id);
        }
        
        result.push_back(entry);
    }
    
    return result;
}

std::vector<WatchHistoryEntry> WatchHistoryService::get_all_history(int limit) const {
    std::vector<WatchHistoryEntry> result;
    
    for (const auto& entry : history_) {
        if (result.size() >= static_cast<size_t>(limit)) break;
        result.push_back(entry);
    }
    
    return result;
}

bool WatchHistoryService::remove_entry(const std::string& meta_id, 
                                        const std::string& video_id) {
    int idx = find_entry_index(meta_id, video_id);
    if (idx >= 0) {
        history_.erase(history_.begin() + idx);
        notify_change();
        return true;
    }
    return false;
}

bool WatchHistoryService::remove_series_history(const std::string& meta_id) {
    size_t before = history_.size();
    history_.erase(
        std::remove_if(history_.begin(), history_.end(),
            [&meta_id](const WatchHistoryEntry& e) {
                return e.meta_id == meta_id;
            }),
        history_.end()
    );
    
    if (history_.size() != before) {
        notify_change();
        return true;
    }
    return false;
}

void WatchHistoryService::clear_all() {
    history_.clear();
    notify_change();
}

void WatchHistoryService::on_history_changed(HistoryChangedCallback callback) {
    change_callbacks_.push_back(callback);
}

void WatchHistoryService::notify_change() {
    // Save to disk
    save();
    
    // Notify listeners
    for (const auto& callback : change_callbacks_) {
        callback();
    }
}

} // namespace Madari
