#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <ctime>

namespace Madari {

/**
 * Represents a single watch history entry
 * Each entry is uniquely identified by a combination of:
 * - meta_id: The ID of the movie or series
 * - video_id: The ID of the specific video/episode (for series) or same as meta_id (for movies)
 */
struct WatchHistoryEntry {
    // Content identification
    std::string meta_id;          // Movie or series ID (e.g., "tt1234567")
    std::string meta_type;        // "movie" or "series"
    std::string video_id;         // Episode ID for series, same as meta_id for movies
    
    // Content metadata (for display in Continue Watching)
    std::string title;            // Display title
    std::string poster_url;       // Poster image URL
    std::optional<std::string> series_title;  // Series name for series episodes
    std::optional<int> season;    // Season number (for series)
    std::optional<int> episode;   // Episode number (for series)
    
    // Watch progress
    double position;              // Current position in seconds
    double duration;              // Total duration in seconds
    int64_t last_watched;         // Unix timestamp of last watch
    
    // Stream selection (for auto-resume with same quality)
    std::optional<std::string> binge_group;  // Binge group for matching streams
    
    /**
     * Calculate progress percentage (0.0 - 1.0)
     */
    double get_progress() const {
        if (duration <= 0) return 0.0;
        return std::min(1.0, position / duration);
    }
    
    /**
     * Check if the content is considered "finished" (>= 90% watched)
     */
    bool is_finished() const {
        return get_progress() >= 0.9;
    }
    
    /**
     * Check if this is a resumable entry (started but not finished)
     */
    bool is_resumable() const {
        return position > 30 && !is_finished();  // Started (>30s) and not finished
    }
    
    /**
     * Get formatted progress string (e.g., "1:23:45 / 2:00:00")
     */
    std::string get_progress_string() const;
    
    /**
     * Get formatted "remaining" time string (e.g., "36m left")
     */
    std::string get_remaining_string() const;
    
    /**
     * Unique key for this entry (meta_id:video_id)
     */
    std::string get_key() const {
        return meta_id + ":" + video_id;
    }
};

/**
 * Service for managing watch history
 * Stores progress for movies and individual series episodes
 */
class WatchHistoryService {
public:
    using HistoryChangedCallback = std::function<void()>;
    
    WatchHistoryService();
    ~WatchHistoryService();
    
    /**
     * Load history from disk
     */
    void load();
    
    /**
     * Save history to disk
     */
    void save();
    
    /**
     * Update watch progress for a content item
     * Creates new entry if doesn't exist, updates if exists
     */
    void update_progress(const WatchHistoryEntry& entry);
    
    /**
     * Update just the position for an existing entry (faster for frequent updates)
     */
    void update_position(const std::string& meta_id, const std::string& video_id, 
                         double position, double duration);
    
    /**
     * Get watch history entry for a specific content item
     */
    std::optional<WatchHistoryEntry> get_entry(const std::string& meta_id, 
                                                const std::string& video_id) const;
    
    /**
     * Get the latest entry for a series (most recently watched episode)
     */
    std::optional<WatchHistoryEntry> get_latest_for_series(const std::string& meta_id) const;
    
    /**
     * Get all resumable items (started but not finished)
     * Returns sorted by last_watched (most recent first)
     */
    std::vector<WatchHistoryEntry> get_continue_watching(int limit = 20) const;
    
    /**
     * Get all history entries (including finished ones)
     * Returns sorted by last_watched (most recent first)
     */
    std::vector<WatchHistoryEntry> get_all_history(int limit = 100) const;
    
    /**
     * Remove a specific entry
     */
    bool remove_entry(const std::string& meta_id, const std::string& video_id);
    
    /**
     * Remove all entries for a series
     */
    bool remove_series_history(const std::string& meta_id);
    
    /**
     * Clear all watch history
     */
    void clear_all();
    
    /**
     * Subscribe to history changes
     */
    void on_history_changed(HistoryChangedCallback callback);

private:
    std::vector<WatchHistoryEntry> history_;
    std::vector<HistoryChangedCallback> change_callbacks_;
    std::string storage_path_;
    
    void notify_change();
    std::string get_storage_path();
    
    // Find entry index, returns -1 if not found
    int find_entry_index(const std::string& meta_id, const std::string& video_id) const;
};

} // namespace Madari
