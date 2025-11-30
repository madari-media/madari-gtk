#pragma once

#include "stremio_types.hpp"
#include <libsoup/soup.h>
#include <functional>
#include <memory>
#include <string>

namespace Stremio {

/**
 * HTTP Client for interacting with Stremio addons
 */
class Client {
public:
    using ManifestCallback = std::function<void(std::optional<Manifest>, const std::string& error)>;
    using CatalogCallback = std::function<void(std::optional<CatalogResponse>, const std::string& error)>;
    using MetaCallback = std::function<void(std::optional<MetaResponse>, const std::string& error)>;
    using StreamsCallback = std::function<void(std::optional<StreamsResponse>, const std::string& error)>;
    using SubtitlesCallback = std::function<void(std::optional<SubtitlesResponse>, const std::string& error)>;

    Client();
    ~Client();
    
    /**
     * Fetch manifest from addon URL
     * @param url Base URL of the addon (e.g., "https://example.com/manifest.json" or "https://example.com")
     * @param callback Called with the parsed manifest or error
     */
    void fetch_manifest(const std::string& url, ManifestCallback callback);
    
    /**
     * Fetch catalog from addon
     * @param manifest The addon manifest
     * @param type Content type (movie, series, etc.)
     * @param catalog_id The catalog ID
     * @param extra Optional extra arguments (search, skip, etc.)
     * @param callback Called with the catalog response or error
     */
    void fetch_catalog(const Manifest& manifest, 
                       const std::string& type, 
                       const std::string& catalog_id,
                       const ExtraArgs& extra,
                       CatalogCallback callback);
    
    /**
     * Fetch metadata for an item
     * @param manifest The addon manifest
     * @param type Content type
     * @param id Item ID
     * @param callback Called with the meta response or error
     */
    void fetch_meta(const Manifest& manifest,
                    const std::string& type,
                    const std::string& id,
                    MetaCallback callback);
    
    /**
     * Fetch streams for an item
     * @param manifest The addon manifest
     * @param type Content type
     * @param video_id Video ID (for movies, same as item ID; for series, includes season/episode)
     * @param callback Called with the streams response or error
     */
    void fetch_streams(const Manifest& manifest,
                       const std::string& type,
                       const std::string& video_id,
                       StreamsCallback callback);
    
    /**
     * Fetch subtitles for a video
     * @param manifest The addon manifest
     * @param type Content type
     * @param id Open Subtitles file hash
     * @param video_id Video ID
     * @param video_size Video file size in bytes (optional)
     * @param callback Called with the subtitles response or error
     */
    void fetch_subtitles(const Manifest& manifest,
                         const std::string& type,
                         const std::string& id,
                         const std::string& video_id,
                         std::optional<int64_t> video_size,
                         SubtitlesCallback callback);

private:
    SoupSession* session_;
    
    std::string build_url(const std::string& base_url, const std::string& path);
    std::string get_base_url(const std::string& transport_url);
    
    void make_request(const std::string& url, 
                      std::function<void(const std::string& body, const std::string& error)> callback);
};

} // namespace Stremio
