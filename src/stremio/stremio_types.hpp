#pragma once

#include <string>
#include <vector>
#include <optional>
#include <map>
#include <json-glib/json-glib.h>

namespace Stremio {

// Forward declarations
struct MetaPreview;
struct Meta;
struct Stream;
struct Subtitle;
struct Video;
struct Catalog;
struct Manifest;

/**
 * Catalog definition in manifest
 */
struct CatalogDefinition {
    std::string type;
    std::string id;
    std::string name;
    std::vector<std::string> genres;
    std::vector<std::string> extra_supported;
    std::vector<std::string> extra_required;
};

/**
 * Resource definition in manifest (can be string or object)
 */
struct ResourceDefinition {
    std::string name;
    std::vector<std::string> types;
    std::vector<std::string> id_prefixes;
};

/**
 * Addon manifest - describes addon capabilities
 */
struct Manifest {
    std::string id;
    std::string version;
    std::string name;
    std::string description;
    std::optional<std::string> logo;
    std::optional<std::string> background;
    
    std::vector<std::string> types;
    std::vector<ResourceDefinition> resources;
    std::vector<CatalogDefinition> catalogs;
    std::vector<std::string> id_prefixes;
    
    // Behavior hints
    bool adult = false;
    bool configurable = false;
    std::optional<std::string> config_url;
    
    // Transport URL (where the addon is hosted)
    std::string transport_url;
    
    bool has_resource(const std::string& resource) const;
    bool has_type(const std::string& type) const;
    bool matches_id_prefix(const std::string& id) const;
};

/**
 * Meta Link object for linking to internal Stremio pages
 */
struct MetaLink {
    std::string name;
    std::string category;
    std::string url;
};

/**
 * Trailer object
 */
struct Trailer {
    std::string source;  // YouTube video ID
    std::string type;    // "Trailer" or "Clip"
};

/**
 * Video object for series/channels
 */
struct Video {
    std::string id;
    std::string title;
    std::string released;
    std::optional<std::string> thumbnail;
    std::optional<std::string> overview;
    std::optional<int> season;
    std::optional<int> episode;
    std::optional<bool> available;
    std::vector<Stream> streams;
};

/**
 * Meta Preview - condensed metadata for catalog listings
 */
struct MetaPreview {
    std::string id;
    std::string type;
    std::string name;
    std::optional<std::string> poster;
    std::optional<std::string> poster_shape; // "square", "poster", "landscape"
    std::optional<std::string> description;
    std::optional<std::string> release_info;
    std::optional<std::string> imdb_rating;
    std::vector<std::string> genres;
    std::vector<std::string> director;
    std::vector<std::string> cast;
    std::vector<MetaLink> links;
};

/**
 * Full Meta object with detailed information
 */
struct Meta {
    std::string id;
    std::string type;
    std::string name;
    std::optional<std::string> poster;
    std::optional<std::string> poster_shape;
    std::optional<std::string> background;
    std::optional<std::string> logo;
    std::optional<std::string> description;
    std::optional<std::string> release_info;
    std::optional<std::string> imdb_rating;
    std::optional<std::string> released;
    std::optional<std::string> runtime;
    std::optional<std::string> language;
    std::optional<std::string> country;
    std::optional<std::string> awards;
    std::optional<std::string> website;
    
    std::vector<std::string> genres;
    std::vector<std::string> director;
    std::vector<std::string> cast;
    std::vector<std::string> writer;
    std::vector<MetaLink> links;
    std::vector<Video> videos;
    std::vector<Trailer> trailers;
    
    // Behavior hints
    std::optional<std::string> default_video_id;
};

/**
 * Subtitle object
 */
struct Subtitle {
    std::string id;
    std::string url;
    std::string lang;
};

/**
 * Stream behavior hints
 */
struct StreamBehaviorHints {
    std::vector<std::string> country_whitelist;
    bool not_web_ready = false;
    std::optional<std::string> binge_group;
    std::optional<std::string> video_hash;
    std::optional<int64_t> video_size;
    std::optional<std::string> filename;
    std::map<std::string, std::string> proxy_headers_request;
    std::map<std::string, std::string> proxy_headers_response;
};

/**
 * Stream object - represents a video stream source
 */
struct Stream {
    // One of these must be set
    std::optional<std::string> url;
    std::optional<std::string> yt_id;
    std::optional<std::string> info_hash;
    std::optional<int> file_idx;
    std::optional<std::string> external_url;
    
    // Additional properties
    std::optional<std::string> name;
    std::optional<std::string> title;
    std::optional<std::string> description;
    std::vector<std::string> sources;
    std::vector<Subtitle> subtitles;
    StreamBehaviorHints behavior_hints;
};

/**
 * Catalog response
 */
struct CatalogResponse {
    std::vector<MetaPreview> metas;
};

/**
 * Meta response
 */
struct MetaResponse {
    Meta meta;
};

/**
 * Streams response
 */
struct StreamsResponse {
    std::vector<Stream> streams;
};

/**
 * Subtitles response
 */
struct SubtitlesResponse {
    std::vector<Subtitle> subtitles;
};

/**
 * Extra arguments for requests (search, skip, etc.)
 */
struct ExtraArgs {
    std::optional<std::string> search;
    std::optional<int> skip;
    std::optional<std::string> genre;
    std::map<std::string, std::string> other;
    
    std::string to_path_segment() const;
};

} // namespace Stremio
