#include "stremio_parser.hpp"
#include <memory>
#include <algorithm>

namespace Stremio {

std::string Parser::get_string(JsonObject* obj, const char* member) {
    if (!json_object_has_member(obj, member)) return "";
    JsonNode* node = json_object_get_member(obj, member);
    if (json_node_get_node_type(node) != JSON_NODE_VALUE) return "";
    return json_node_get_string(node) ? json_node_get_string(node) : "";
}

std::optional<std::string> Parser::get_optional_string(JsonObject* obj, const char* member) {
    if (!json_object_has_member(obj, member)) return std::nullopt;
    JsonNode* node = json_object_get_member(obj, member);
    if (json_node_get_node_type(node) != JSON_NODE_VALUE) return std::nullopt;
    const char* str = json_node_get_string(node);
    if (!str) return std::nullopt;
    return std::string(str);
}

std::optional<int> Parser::get_optional_int(JsonObject* obj, const char* member) {
    if (!json_object_has_member(obj, member)) return std::nullopt;
    JsonNode* node = json_object_get_member(obj, member);
    if (json_node_get_node_type(node) != JSON_NODE_VALUE) return std::nullopt;
    return static_cast<int>(json_node_get_int(node));
}

std::optional<int64_t> Parser::get_optional_int64(JsonObject* obj, const char* member) {
    if (!json_object_has_member(obj, member)) return std::nullopt;
    JsonNode* node = json_object_get_member(obj, member);
    if (json_node_get_node_type(node) != JSON_NODE_VALUE) return std::nullopt;
    return json_node_get_int(node);
}

std::optional<bool> Parser::get_optional_bool(JsonObject* obj, const char* member) {
    if (!json_object_has_member(obj, member)) return std::nullopt;
    JsonNode* node = json_object_get_member(obj, member);
    if (json_node_get_node_type(node) != JSON_NODE_VALUE) return std::nullopt;
    return json_node_get_boolean(node);
}

std::vector<std::string> Parser::get_string_array(JsonObject* obj, const char* member) {
    std::vector<std::string> result;
    if (!json_object_has_member(obj, member)) return result;
    
    JsonNode* node = json_object_get_member(obj, member);
    if (json_node_get_node_type(node) != JSON_NODE_ARRAY) return result;
    
    JsonArray* array = json_node_get_array(node);
    guint len = json_array_get_length(array);
    
    for (guint i = 0; i < len; i++) {
        JsonNode* elem = json_array_get_element(array, i);
        if (json_node_get_node_type(elem) == JSON_NODE_VALUE) {
            const char* str = json_node_get_string(elem);
            if (str) result.push_back(str);
        }
    }
    
    return result;
}

MetaLink Parser::parse_meta_link(JsonObject* obj) {
    MetaLink link;
    link.name = get_string(obj, "name");
    link.category = get_string(obj, "category");
    link.url = get_string(obj, "url");
    return link;
}

CatalogDefinition Parser::parse_catalog_definition(JsonObject* obj) {
    CatalogDefinition cat;
    cat.type = get_string(obj, "type");
    cat.id = get_string(obj, "id");
    cat.name = get_string(obj, "name");
    cat.genres = get_string_array(obj, "genres");
    
    // First try the old format: extraSupported/extraRequired as string arrays
    cat.extra_supported = get_string_array(obj, "extraSupported");
    cat.extra_required = get_string_array(obj, "extraRequired");
    
    // Also parse the new format: "extra" as array of objects with {name, isRequired, options}
    if (json_object_has_member(obj, "extra")) {
        JsonNode* extra_node = json_object_get_member(obj, "extra");
        if (extra_node && json_node_get_node_type(extra_node) == JSON_NODE_ARRAY) {
            JsonArray* extra_array = json_node_get_array(extra_node);
            guint len = json_array_get_length(extra_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* item_node = json_array_get_element(extra_array, i);
                if (item_node && json_node_get_node_type(item_node) == JSON_NODE_OBJECT) {
                    JsonObject* item = json_node_get_object(item_node);
                    std::string name = get_string(item, "name");
                    if (!name.empty()) {
                        // Check if isRequired is true
                        bool is_required = false;
                        if (json_object_has_member(item, "isRequired")) {
                            is_required = json_object_get_boolean_member(item, "isRequired");
                        }
                        
                        if (is_required) {
                            // Add to extra_required if not already present
                            if (std::find(cat.extra_required.begin(), cat.extra_required.end(), name) == cat.extra_required.end()) {
                                cat.extra_required.push_back(name);
                            }
                        } else {
                            // Add to extra_supported if not already present
                            if (std::find(cat.extra_supported.begin(), cat.extra_supported.end(), name) == cat.extra_supported.end()) {
                                cat.extra_supported.push_back(name);
                            }
                        }
                    }
                }
            }
        }
    }
    
    return cat;
}

ResourceDefinition Parser::parse_resource_definition(JsonNode* node) {
    ResourceDefinition res;
    
    if (json_node_get_node_type(node) == JSON_NODE_VALUE) {
        // Simple string resource
        res.name = json_node_get_string(node) ? json_node_get_string(node) : "";
    } else if (json_node_get_node_type(node) == JSON_NODE_OBJECT) {
        // Complex resource with types and idPrefixes
        JsonObject* obj = json_node_get_object(node);
        res.name = get_string(obj, "name");
        res.types = get_string_array(obj, "types");
        res.id_prefixes = get_string_array(obj, "idPrefixes");
    }
    
    return res;
}

Subtitle Parser::parse_subtitle(JsonObject* obj) {
    Subtitle sub;
    sub.id = get_string(obj, "id");
    sub.url = get_string(obj, "url");
    sub.lang = get_string(obj, "lang");
    return sub;
}

Stream Parser::parse_stream(JsonObject* obj) {
    Stream stream;
    
    stream.url = get_optional_string(obj, "url");
    stream.yt_id = get_optional_string(obj, "ytId");
    stream.info_hash = get_optional_string(obj, "infoHash");
    stream.file_idx = get_optional_int(obj, "fileIdx");
    stream.external_url = get_optional_string(obj, "externalUrl");
    
    stream.name = get_optional_string(obj, "name");
    stream.title = get_optional_string(obj, "title");
    stream.description = get_optional_string(obj, "description");
    stream.sources = get_string_array(obj, "sources");
    
    // Parse subtitles
    if (json_object_has_member(obj, "subtitles")) {
        JsonNode* subs_node = json_object_get_member(obj, "subtitles");
        if (json_node_get_node_type(subs_node) == JSON_NODE_ARRAY) {
            JsonArray* subs_array = json_node_get_array(subs_node);
            guint len = json_array_get_length(subs_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* sub_node = json_array_get_element(subs_array, i);
                if (json_node_get_node_type(sub_node) == JSON_NODE_OBJECT) {
                    stream.subtitles.push_back(parse_subtitle(json_node_get_object(sub_node)));
                }
            }
        }
    }
    
    // Parse behavior hints
    if (json_object_has_member(obj, "behaviorHints")) {
        JsonNode* hints_node = json_object_get_member(obj, "behaviorHints");
        if (json_node_get_node_type(hints_node) == JSON_NODE_OBJECT) {
            JsonObject* hints = json_node_get_object(hints_node);
            stream.behavior_hints.country_whitelist = get_string_array(hints, "countryWhitelist");
            auto not_web_ready = get_optional_bool(hints, "notWebReady");
            if (not_web_ready) stream.behavior_hints.not_web_ready = *not_web_ready;
            stream.behavior_hints.binge_group = get_optional_string(hints, "bingeGroup");
            stream.behavior_hints.video_hash = get_optional_string(hints, "videoHash");
            stream.behavior_hints.video_size = get_optional_int64(hints, "videoSize");
            stream.behavior_hints.filename = get_optional_string(hints, "filename");
        }
    }
    
    return stream;
}

Video Parser::parse_video(JsonObject* obj) {
    Video video;
    video.id = get_string(obj, "id");
    video.title = get_string(obj, "title");
    // Fallback to "name" field if "title" is empty (some addons use "name" for episode titles)
    if (video.title.empty()) {
        video.title = get_string(obj, "name");
    }
    video.released = get_string(obj, "released");
    video.thumbnail = get_optional_string(obj, "thumbnail");
    video.overview = get_optional_string(obj, "overview");
    video.season = get_optional_int(obj, "season");
    video.episode = get_optional_int(obj, "episode");
    video.available = get_optional_bool(obj, "available");
    
    // Parse embedded streams
    if (json_object_has_member(obj, "streams")) {
        JsonNode* streams_node = json_object_get_member(obj, "streams");
        if (json_node_get_node_type(streams_node) == JSON_NODE_ARRAY) {
            JsonArray* streams_array = json_node_get_array(streams_node);
            guint len = json_array_get_length(streams_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* stream_node = json_array_get_element(streams_array, i);
                if (json_node_get_node_type(stream_node) == JSON_NODE_OBJECT) {
                    video.streams.push_back(parse_stream(json_node_get_object(stream_node)));
                }
            }
        }
    }
    
    return video;
}

MetaPreview Parser::parse_meta_preview(JsonObject* obj) {
    MetaPreview meta;
    meta.id = get_string(obj, "id");
    meta.type = get_string(obj, "type");
    meta.name = get_string(obj, "name");
    meta.poster = get_optional_string(obj, "poster");
    meta.poster_shape = get_optional_string(obj, "posterShape");
    meta.description = get_optional_string(obj, "description");
    meta.release_info = get_optional_string(obj, "releaseInfo");
    meta.imdb_rating = get_optional_string(obj, "imdbRating");
    meta.genres = get_string_array(obj, "genres");
    meta.director = get_string_array(obj, "director");
    meta.cast = get_string_array(obj, "cast");
    
    // Parse links
    if (json_object_has_member(obj, "links")) {
        JsonNode* links_node = json_object_get_member(obj, "links");
        if (json_node_get_node_type(links_node) == JSON_NODE_ARRAY) {
            JsonArray* links_array = json_node_get_array(links_node);
            guint len = json_array_get_length(links_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* link_node = json_array_get_element(links_array, i);
                if (json_node_get_node_type(link_node) == JSON_NODE_OBJECT) {
                    meta.links.push_back(parse_meta_link(json_node_get_object(link_node)));
                }
            }
        }
    }
    
    return meta;
}

Meta Parser::parse_meta_object(JsonObject* obj) {
    Meta meta;
    meta.id = get_string(obj, "id");
    meta.type = get_string(obj, "type");
    meta.name = get_string(obj, "name");
    meta.poster = get_optional_string(obj, "poster");
    meta.poster_shape = get_optional_string(obj, "posterShape");
    meta.background = get_optional_string(obj, "background");
    meta.logo = get_optional_string(obj, "logo");
    meta.description = get_optional_string(obj, "description");
    meta.release_info = get_optional_string(obj, "releaseInfo");
    meta.imdb_rating = get_optional_string(obj, "imdbRating");
    meta.released = get_optional_string(obj, "released");
    meta.runtime = get_optional_string(obj, "runtime");
    meta.language = get_optional_string(obj, "language");
    meta.country = get_optional_string(obj, "country");
    meta.awards = get_optional_string(obj, "awards");
    meta.website = get_optional_string(obj, "website");
    meta.genres = get_string_array(obj, "genres");
    meta.director = get_string_array(obj, "director");
    meta.cast = get_string_array(obj, "cast");
    meta.writer = get_string_array(obj, "writer");
    
    // Parse trailers
    if (json_object_has_member(obj, "trailers")) {
        JsonNode* trailers_node = json_object_get_member(obj, "trailers");
        if (json_node_get_node_type(trailers_node) == JSON_NODE_ARRAY) {
            JsonArray* trailers_array = json_node_get_array(trailers_node);
            guint len = json_array_get_length(trailers_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* trailer_node = json_array_get_element(trailers_array, i);
                if (json_node_get_node_type(trailer_node) == JSON_NODE_OBJECT) {
                    JsonObject* trailer_obj = json_node_get_object(trailer_node);
                    Trailer trailer;
                    trailer.source = get_string(trailer_obj, "source");
                    trailer.type = get_string(trailer_obj, "type");
                    if (!trailer.source.empty()) {
                        meta.trailers.push_back(trailer);
                    }
                }
            }
        }
    }
    
    // Parse links
    if (json_object_has_member(obj, "links")) {
        JsonNode* links_node = json_object_get_member(obj, "links");
        if (json_node_get_node_type(links_node) == JSON_NODE_ARRAY) {
            JsonArray* links_array = json_node_get_array(links_node);
            guint len = json_array_get_length(links_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* link_node = json_array_get_element(links_array, i);
                if (json_node_get_node_type(link_node) == JSON_NODE_OBJECT) {
                    meta.links.push_back(parse_meta_link(json_node_get_object(link_node)));
                }
            }
        }
    }
    
    // Parse videos
    if (json_object_has_member(obj, "videos")) {
        JsonNode* videos_node = json_object_get_member(obj, "videos");
        if (json_node_get_node_type(videos_node) == JSON_NODE_ARRAY) {
            JsonArray* videos_array = json_node_get_array(videos_node);
            guint len = json_array_get_length(videos_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* video_node = json_array_get_element(videos_array, i);
                if (json_node_get_node_type(video_node) == JSON_NODE_OBJECT) {
                    meta.videos.push_back(parse_video(json_node_get_object(video_node)));
                }
            }
        }
    }
    
    // Parse behavior hints
    if (json_object_has_member(obj, "behaviorHints")) {
        JsonNode* hints_node = json_object_get_member(obj, "behaviorHints");
        if (json_node_get_node_type(hints_node) == JSON_NODE_OBJECT) {
            JsonObject* hints = json_node_get_object(hints_node);
            meta.default_video_id = get_optional_string(hints, "defaultVideoId");
        }
    }
    
    return meta;
}

std::optional<Manifest> Parser::parse_manifest(const std::string& json, const std::string& transport_url) {
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_data(parser, json.c_str(), json.length(), &error)) {
        g_warning("Failed to parse manifest JSON: %s", error->message);
        return std::nullopt;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        return std::nullopt;
    }
    
    JsonObject* obj = json_node_get_object(root);
    
    Manifest manifest;
    manifest.id = get_string(obj, "id");
    manifest.version = get_string(obj, "version");
    manifest.name = get_string(obj, "name");
    manifest.description = get_string(obj, "description");
    manifest.logo = get_optional_string(obj, "logo");
    manifest.background = get_optional_string(obj, "background");
    manifest.types = get_string_array(obj, "types");
    manifest.id_prefixes = get_string_array(obj, "idPrefixes");
    manifest.transport_url = transport_url;
    
    // Parse behavior hints
    if (json_object_has_member(obj, "behaviorHints")) {
        JsonNode* hints_node = json_object_get_member(obj, "behaviorHints");
        if (json_node_get_node_type(hints_node) == JSON_NODE_OBJECT) {
            JsonObject* hints = json_node_get_object(hints_node);
            auto adult = get_optional_bool(hints, "adult");
            if (adult) manifest.adult = *adult;
            auto configurable = get_optional_bool(hints, "configurable");
            if (configurable) manifest.configurable = *configurable;
            manifest.config_url = get_optional_string(hints, "configurationURL");
        }
    }
    
    // Parse resources
    if (json_object_has_member(obj, "resources")) {
        JsonNode* resources_node = json_object_get_member(obj, "resources");
        if (json_node_get_node_type(resources_node) == JSON_NODE_ARRAY) {
            JsonArray* resources_array = json_node_get_array(resources_node);
            guint len = json_array_get_length(resources_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* res_node = json_array_get_element(resources_array, i);
                manifest.resources.push_back(parse_resource_definition(res_node));
            }
        }
    }
    
    // Parse catalogs
    if (json_object_has_member(obj, "catalogs")) {
        JsonNode* catalogs_node = json_object_get_member(obj, "catalogs");
        if (json_node_get_node_type(catalogs_node) == JSON_NODE_ARRAY) {
            JsonArray* catalogs_array = json_node_get_array(catalogs_node);
            guint len = json_array_get_length(catalogs_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* cat_node = json_array_get_element(catalogs_array, i);
                if (json_node_get_node_type(cat_node) == JSON_NODE_OBJECT) {
                    manifest.catalogs.push_back(parse_catalog_definition(json_node_get_object(cat_node)));
                }
            }
        }
    }
    
    return manifest;
}

std::optional<CatalogResponse> Parser::parse_catalog(const std::string& json) {
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_data(parser, json.c_str(), json.length(), &error)) {
        g_warning("Failed to parse catalog JSON: %s", error->message);
        return std::nullopt;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        return std::nullopt;
    }
    
    JsonObject* obj = json_node_get_object(root);
    CatalogResponse response;
    
    if (json_object_has_member(obj, "metas")) {
        JsonNode* metas_node = json_object_get_member(obj, "metas");
        if (json_node_get_node_type(metas_node) == JSON_NODE_ARRAY) {
            JsonArray* metas_array = json_node_get_array(metas_node);
            guint len = json_array_get_length(metas_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* meta_node = json_array_get_element(metas_array, i);
                if (json_node_get_node_type(meta_node) == JSON_NODE_OBJECT) {
                    response.metas.push_back(parse_meta_preview(json_node_get_object(meta_node)));
                }
            }
        }
    }
    
    return response;
}

std::optional<MetaResponse> Parser::parse_meta(const std::string& json) {
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_data(parser, json.c_str(), json.length(), &error)) {
        g_warning("Failed to parse meta JSON: %s", error->message);
        return std::nullopt;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        return std::nullopt;
    }
    
    JsonObject* obj = json_node_get_object(root);
    
    if (!json_object_has_member(obj, "meta")) {
        return std::nullopt;
    }
    
    JsonNode* meta_node = json_object_get_member(obj, "meta");
    if (json_node_get_node_type(meta_node) != JSON_NODE_OBJECT) {
        return std::nullopt;
    }
    
    MetaResponse response;
    response.meta = parse_meta_object(json_node_get_object(meta_node));
    return response;
}

std::optional<StreamsResponse> Parser::parse_streams(const std::string& json) {
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_data(parser, json.c_str(), json.length(), &error)) {
        g_warning("Failed to parse streams JSON: %s", error->message);
        return std::nullopt;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        return std::nullopt;
    }
    
    JsonObject* obj = json_node_get_object(root);
    StreamsResponse response;
    
    if (json_object_has_member(obj, "streams")) {
        JsonNode* streams_node = json_object_get_member(obj, "streams");
        if (json_node_get_node_type(streams_node) == JSON_NODE_ARRAY) {
            JsonArray* streams_array = json_node_get_array(streams_node);
            guint len = json_array_get_length(streams_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* stream_node = json_array_get_element(streams_array, i);
                if (json_node_get_node_type(stream_node) == JSON_NODE_OBJECT) {
                    response.streams.push_back(parse_stream(json_node_get_object(stream_node)));
                }
            }
        }
    }
    
    return response;
}

std::optional<SubtitlesResponse> Parser::parse_subtitles(const std::string& json) {
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_data(parser, json.c_str(), json.length(), &error)) {
        g_warning("Failed to parse subtitles JSON: %s", error->message);
        return std::nullopt;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        return std::nullopt;
    }
    
    JsonObject* obj = json_node_get_object(root);
    SubtitlesResponse response;
    
    if (json_object_has_member(obj, "subtitles")) {
        JsonNode* subs_node = json_object_get_member(obj, "subtitles");
        if (json_node_get_node_type(subs_node) == JSON_NODE_ARRAY) {
            JsonArray* subs_array = json_node_get_array(subs_node);
            guint len = json_array_get_length(subs_array);
            for (guint i = 0; i < len; i++) {
                JsonNode* sub_node = json_array_get_element(subs_array, i);
                if (json_node_get_node_type(sub_node) == JSON_NODE_OBJECT) {
                    response.subtitles.push_back(parse_subtitle(json_node_get_object(sub_node)));
                }
            }
        }
    }
    
    return response;
}

} // namespace Stremio
