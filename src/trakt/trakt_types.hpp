#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <ctime>

namespace Trakt {

/**
 * Trakt IDs object - contains various ID types
 */
struct Ids {
    std::optional<int64_t> trakt;
    std::optional<std::string> slug;
    std::optional<std::string> imdb;
    std::optional<int64_t> tmdb;
    std::optional<int64_t> tvdb;
};

/**
 * Parsed content IDs from Stremio format
 * Used for scrobbling - supports multiple ID types and episode info
 * 
 * Stremio ID formats:
 * - IMDB: "tt1234567" or "tt1234567:2:5" (with season:episode)
 * - TMDB: "tmdb:12345" or "tmdb:12345:2:5"
 * - TVDB: "tvdb:67890" or "tvdb:67890:2:5"
 * - Kitsu: "kitsu:12345" or "kitsu:12345:2:5"
 */
struct ContentIds {
    std::optional<std::string> imdb;
    std::optional<int64_t> tmdb;
    std::optional<int64_t> tvdb;
    std::optional<int64_t> kitsu;
    
    // Episode info (parsed from "id:season:episode" format)
    bool is_episode = false;
    int season = 0;
    int episode = 0;
    
    // Check if any ID is available
    bool has_id() const {
        return imdb.has_value() || tmdb.has_value() || 
               tvdb.has_value() || kitsu.has_value();
    }
};

/**
 * Parse a Stremio-format ID string into ContentIds
 * Examples:
 *   "tt1234567" -> imdb="tt1234567"
 *   "tt1234567:2:5" -> imdb="tt1234567", is_episode=true, season=2, episode=5
 *   "tmdb:12345" -> tmdb=12345
 *   "tmdb:12345:2:5" -> tmdb=12345, is_episode=true, season=2, episode=5
 */
ContentIds parse_stremio_id(const std::string& id);

/**
 * Movie object from Trakt API
 */
struct Movie {
    std::string title;
    std::optional<int> year;
    Ids ids;
    std::optional<std::string> tagline;
    std::optional<std::string> overview;
    std::optional<std::string> released;
    std::optional<int> runtime;
    std::optional<std::string> country;
    std::optional<std::string> trailer;
    std::optional<std::string> homepage;
    std::optional<std::string> status;
    std::optional<double> rating;
    std::optional<int64_t> votes;
    std::optional<std::string> language;
    std::vector<std::string> genres;
    std::optional<std::string> certification;
};

/**
 * Show object from Trakt API
 */
struct Show {
    std::string title;
    std::optional<int> year;
    Ids ids;
    std::optional<std::string> overview;
    std::optional<std::string> first_aired;
    std::optional<int> runtime;
    std::optional<std::string> certification;
    std::optional<std::string> network;
    std::optional<std::string> country;
    std::optional<std::string> trailer;
    std::optional<std::string> homepage;
    std::optional<std::string> status;
    std::optional<double> rating;
    std::optional<int64_t> votes;
    std::optional<std::string> language;
    std::vector<std::string> genres;
    std::optional<int> aired_episodes;
};

/**
 * Episode object from Trakt API
 */
struct Episode {
    int season;
    int number;
    std::string title;
    Ids ids;
    std::optional<std::string> overview;
    std::optional<double> rating;
    std::optional<int64_t> votes;
    std::optional<std::string> first_aired;
    std::optional<int> runtime;
};

/**
 * Season object from Trakt API
 */
struct Season {
    int number;
    Ids ids;
    std::optional<double> rating;
    std::optional<int64_t> votes;
    std::optional<int> episode_count;
    std::optional<int> aired_episodes;
    std::optional<std::string> title;
    std::optional<std::string> overview;
    std::optional<std::string> first_aired;
    std::vector<Episode> episodes;
};

/**
 * Playback progress item (for scrobble/sync)
 */
struct PlaybackProgress {
    int64_t id;
    double progress;  // 0-100
    std::optional<Movie> movie;
    std::optional<Show> show;
    std::optional<Episode> episode;
    std::string paused_at;
    std::string type;  // "movie" or "episode"
};

/**
 * Watchlist item
 */
struct WatchlistItem {
    int64_t rank;
    std::string listed_at;
    std::string type;  // "movie", "show", "season", "episode"
    std::optional<Movie> movie;
    std::optional<Show> show;
    std::optional<Season> season;
    std::optional<Episode> episode;
    std::optional<std::string> notes;
};

/**
 * Watched item
 */
struct WatchedItem {
    int64_t plays;
    std::string last_watched_at;
    std::string last_updated_at;
    std::optional<Movie> movie;
    std::optional<Show> show;
    std::optional<std::string> reset_at;
    std::vector<Season> seasons;  // For shows
};

/**
 * History item
 */
struct HistoryItem {
    int64_t id;
    std::string watched_at;
    std::string action;  // "scrobble", "checkin", "watch"
    std::string type;    // "movie", "episode"
    std::optional<Movie> movie;
    std::optional<Show> show;
    std::optional<Episode> episode;
};

/**
 * Search result item
 */
struct SearchResult {
    std::string type;  // "movie", "show", "episode", "person", "list"
    double score;
    std::optional<Movie> movie;
    std::optional<Show> show;
    std::optional<Episode> episode;
};

/**
 * OAuth device code response
 */
struct DeviceCode {
    std::string device_code;
    std::string user_code;
    std::string verification_url;
    int expires_in;
    int interval;
};

/**
 * OAuth token response
 */
struct TokenResponse {
    std::string access_token;
    std::string token_type;
    int64_t expires_in;
    std::string refresh_token;
    std::string scope;
    int64_t created_at;
};

/**
 * User settings from Trakt
 */
struct UserSettings {
    std::string username;
    std::optional<std::string> name;
    std::optional<std::string> avatar;
    bool is_vip;
};

/**
 * Trakt configuration stored locally
 */
struct TraktConfig {
    std::string client_id;
    std::string client_secret;
    std::string access_token;
    std::string refresh_token;
    int64_t expires_at;  // Unix timestamp
    bool enabled;
    
    // Sync settings
    bool sync_watchlist;
    bool sync_history;
    bool sync_progress;
    
    // User info (cached)
    std::optional<std::string> username;
    std::optional<std::string> avatar_url;
    
    bool is_authenticated() const {
        return !access_token.empty() && expires_at > 0;
    }
    
    bool is_token_expired() const {
        return std::time(nullptr) >= expires_at;
    }
};

} // namespace Trakt
