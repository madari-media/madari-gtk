#pragma once

#include "trakt_types.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Trakt {

/**
 * Trakt API service
 * Handles authentication, sync, and API calls
 */
class TraktService {
public:
    // Callback types
    using ConfigChangedCallback = std::function<void()>;
    using AuthCallback = std::function<void(bool success, const std::string& error)>;
    using DeviceCodeCallback = std::function<void(std::optional<DeviceCode> code, const std::string& error)>;
    using TokenPollCallback = std::function<void(bool success, bool pending, const std::string& error)>;
    
    template<typename T>
    using ResultCallback = std::function<void(std::optional<T> result, const std::string& error)>;
    
    using MoviesCallback = ResultCallback<std::vector<Movie>>;
    using ShowsCallback = ResultCallback<std::vector<Show>>;
    using PlaybackCallback = ResultCallback<std::vector<PlaybackProgress>>;
    using WatchlistCallback = ResultCallback<std::vector<WatchlistItem>>;
    using HistoryCallback = ResultCallback<std::vector<HistoryItem>>;
    using SearchCallback = ResultCallback<std::vector<SearchResult>>;
    using UserSettingsCallback = ResultCallback<UserSettings>;
    
    TraktService();
    ~TraktService();
    
    /**
     * Load configuration from storage
     */
    void load();
    
    /**
     * Save configuration to storage
     */
    void save();
    
    /**
     * Get current configuration
     */
    const TraktConfig& get_config() const;
    
    /**
     * Update configuration (client_id, client_secret, etc.)
     */
    void set_config(const TraktConfig& config);
    
    /**
     * Set just the API credentials
     */
    void set_credentials(const std::string& client_id, const std::string& client_secret);
    
    /**
     * Check if Trakt is configured with credentials
     */
    bool is_configured() const;
    
    /**
     * Check if user is authenticated
     */
    bool is_authenticated() const;
    
    /**
     * Start device authentication flow
     * Returns device code and user_code for user to enter on trakt.tv
     */
    void start_device_auth(DeviceCodeCallback callback);
    
    /**
     * Poll for token after user enters code
     * Call this periodically until success or expiration
     */
    void poll_device_token(const std::string& device_code, TokenPollCallback callback);
    
    /**
     * Refresh access token using refresh token
     */
    void refresh_token(AuthCallback callback);
    
    /**
     * Logout / revoke token
     */
    void logout(AuthCallback callback);
    
    /**
     * Get user settings
     */
    void get_user_settings(UserSettingsCallback callback);
    
    /**
     * Subscribe to configuration changes
     */
    void on_config_changed(ConfigChangedCallback callback);
    
    // ============ Catalog Methods ============
    
    /**
     * Get trending movies
     */
    void get_trending_movies(int page, int limit, MoviesCallback callback);
    
    /**
     * Get popular movies
     */
    void get_popular_movies(int page, int limit, MoviesCallback callback);
    
    /**
     * Get anticipated movies
     */
    void get_anticipated_movies(int page, int limit, MoviesCallback callback);
    
    /**
     * Get trending shows
     */
    void get_trending_shows(int page, int limit, ShowsCallback callback);
    
    /**
     * Get popular shows
     */
    void get_popular_shows(int page, int limit, ShowsCallback callback);
    
    /**
     * Get anticipated shows
     */
    void get_anticipated_shows(int page, int limit, ShowsCallback callback);
    
    /**
     * Search for movies and shows
     */
    void search(const std::string& query, const std::string& type, SearchCallback callback);
    
    // ============ Sync Methods ============
    
    /**
     * Get playback progress (continue watching)
     */
    void get_playback(PlaybackCallback callback);
    
    /**
     * Remove playback progress item
     */
    void remove_playback(int64_t playback_id, AuthCallback callback);
    
    /**
     * Get user's watchlist
     */
    void get_watchlist(const std::string& type, WatchlistCallback callback);
    
    /**
     * Add item to watchlist
     */
    void add_to_watchlist(const std::string& type, const std::string& imdb_id, AuthCallback callback);
    
    /**
     * Remove item from watchlist
     */
    void remove_from_watchlist(const std::string& type, const std::string& imdb_id, AuthCallback callback);
    
    /**
     * Get watch history
     */
    void get_history(const std::string& type, int page, int limit, HistoryCallback callback);
    
    /**
     * Add to history (mark as watched)
     */
    void add_to_history(const std::string& type, const std::string& imdb_id, 
                        const std::string& watched_at, AuthCallback callback);
    
    // ============ Scrobble Methods ============
    
    /**
     * Start scrobble (when playback starts)
     */
    void scrobble_start(const std::string& type, const std::string& imdb_id,
                        double progress, AuthCallback callback);
    
    /**
     * Pause scrobble
     */
    void scrobble_pause(const std::string& type, const std::string& imdb_id,
                        double progress, AuthCallback callback);
    
    /**
     * Stop scrobble (when playback ends)
     */
    void scrobble_stop(const std::string& type, const std::string& imdb_id,
                       double progress, AuthCallback callback);

private:
    TraktConfig config_;
    std::vector<ConfigChangedCallback> change_callbacks_;
    std::string storage_path_;
    
    void notify_change();
    std::string get_storage_path();
    
    // Internal HTTP request helper
    void make_request(const std::string& method, const std::string& endpoint,
                      const std::string& body, bool require_auth,
                      std::function<void(const std::string& response, int status_code, const std::string& error)> callback);
    
    // Token management
    void ensure_valid_token(std::function<void(bool valid)> callback);
};

} // namespace Trakt
