#pragma once

#include "stremio_types.hpp"
#include <json-glib/json-glib.h>
#include <string>
#include <optional>

namespace Stremio {

/**
 * JSON Parser for Stremio addon responses
 */
class Parser {
public:
    /**
     * Parse manifest from JSON string
     */
    static std::optional<Manifest> parse_manifest(const std::string& json, const std::string& transport_url);
    
    /**
     * Parse catalog response from JSON string
     */
    static std::optional<CatalogResponse> parse_catalog(const std::string& json);
    
    /**
     * Parse meta response from JSON string
     */
    static std::optional<MetaResponse> parse_meta(const std::string& json);
    
    /**
     * Parse streams response from JSON string
     */
    static std::optional<StreamsResponse> parse_streams(const std::string& json);
    
    /**
     * Parse subtitles response from JSON string
     */
    static std::optional<SubtitlesResponse> parse_subtitles(const std::string& json);

private:
    // Helper functions
    static std::string get_string(JsonObject* obj, const char* member);
    static std::optional<std::string> get_optional_string(JsonObject* obj, const char* member);
    static std::optional<int> get_optional_int(JsonObject* obj, const char* member);
    static std::optional<int64_t> get_optional_int64(JsonObject* obj, const char* member);
    static std::optional<bool> get_optional_bool(JsonObject* obj, const char* member);
    static std::vector<std::string> get_string_array(JsonObject* obj, const char* member);
    
    static MetaPreview parse_meta_preview(JsonObject* obj);
    static Meta parse_meta_object(JsonObject* obj);
    static Video parse_video(JsonObject* obj);
    static Stream parse_stream(JsonObject* obj);
    static Subtitle parse_subtitle(JsonObject* obj);
    static MetaLink parse_meta_link(JsonObject* obj);
    static CatalogDefinition parse_catalog_definition(JsonObject* obj);
    static ResourceDefinition parse_resource_definition(JsonNode* node);
};

} // namespace Stremio
