#include "trakt_service.hpp"
#include "trakt_types.hpp"

#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <glib.h>
#include <ctime>
#include <vector>

namespace Trakt {

static const char* TRAKT_API_URL = "https://api.trakt.tv";
static const char* TRAKT_API_VERSION = "2";
static const char* TRAKT_CLIENT_ID = "b47864365ac88ecc253c3b0bdf1c82a619c1833e8806f702895a7e8cb06b536a";

// JSON Parsing helpers
static Ids parse_ids(JsonObject* obj) {
    Ids ids;
    if (json_object_has_member(obj, "trakt"))
        ids.trakt = json_object_get_int_member(obj, "trakt");
    if (json_object_has_member(obj, "slug")) {
        const char* s = json_object_get_string_member(obj, "slug");
        if (s) ids.slug = s;
    }
    if (json_object_has_member(obj, "imdb")) {
        const char* s = json_object_get_string_member(obj, "imdb");
        if (s) ids.imdb = s;
    }
    if (json_object_has_member(obj, "tmdb"))
        ids.tmdb = json_object_get_int_member(obj, "tmdb");
    if (json_object_has_member(obj, "tvdb"))
        ids.tvdb = json_object_get_int_member(obj, "tvdb");
    return ids;
}

static Movie parse_movie(JsonObject* obj) {
    Movie movie;
    if (json_object_has_member(obj, "title")) {
        const char* s = json_object_get_string_member(obj, "title");
        if (s) movie.title = s;
    }
    if (json_object_has_member(obj, "year") && !json_object_get_null_member(obj, "year"))
        movie.year = static_cast<int>(json_object_get_int_member(obj, "year"));
    if (json_object_has_member(obj, "ids"))
        movie.ids = parse_ids(json_object_get_object_member(obj, "ids"));
    if (json_object_has_member(obj, "overview")) {
        const char* s = json_object_get_string_member(obj, "overview");
        if (s) movie.overview = s;
    }
    if (json_object_has_member(obj, "released")) {
        const char* s = json_object_get_string_member(obj, "released");
        if (s) movie.released = s;
    }
    if (json_object_has_member(obj, "runtime") && !json_object_get_null_member(obj, "runtime"))
        movie.runtime = static_cast<int>(json_object_get_int_member(obj, "runtime"));
    if (json_object_has_member(obj, "rating") && !json_object_get_null_member(obj, "rating"))
        movie.rating = json_object_get_double_member(obj, "rating");
    if (json_object_has_member(obj, "votes") && !json_object_get_null_member(obj, "votes"))
        movie.votes = json_object_get_int_member(obj, "votes");
    if (json_object_has_member(obj, "genres")) {
        JsonArray* arr = json_object_get_array_member(obj, "genres");
        if (arr) {
            guint len = json_array_get_length(arr);
            for (guint i = 0; i < len; i++) {
                const char* g = json_array_get_string_element(arr, i);
                if (g) movie.genres.push_back(g);
            }
        }
    }
    return movie;
}

static Show parse_show(JsonObject* obj) {
    Show show;
    if (json_object_has_member(obj, "title")) {
        const char* s = json_object_get_string_member(obj, "title");
        if (s) show.title = s;
    }
    if (json_object_has_member(obj, "year") && !json_object_get_null_member(obj, "year"))
        show.year = static_cast<int>(json_object_get_int_member(obj, "year"));
    if (json_object_has_member(obj, "ids"))
        show.ids = parse_ids(json_object_get_object_member(obj, "ids"));
    if (json_object_has_member(obj, "overview")) {
        const char* s = json_object_get_string_member(obj, "overview");
        if (s) show.overview = s;
    }
    if (json_object_has_member(obj, "first_aired")) {
        const char* s = json_object_get_string_member(obj, "first_aired");
        if (s) show.first_aired = s;
    }
    if (json_object_has_member(obj, "runtime") && !json_object_get_null_member(obj, "runtime"))
        show.runtime = static_cast<int>(json_object_get_int_member(obj, "runtime"));
    if (json_object_has_member(obj, "rating") && !json_object_get_null_member(obj, "rating"))
        show.rating = json_object_get_double_member(obj, "rating");
    if (json_object_has_member(obj, "votes") && !json_object_get_null_member(obj, "votes"))
        show.votes = json_object_get_int_member(obj, "votes");
    if (json_object_has_member(obj, "status")) {
        const char* s = json_object_get_string_member(obj, "status");
        if (s) show.status = s;
    }
    if (json_object_has_member(obj, "network")) {
        const char* s = json_object_get_string_member(obj, "network");
        if (s) show.network = s;
    }
    if (json_object_has_member(obj, "genres")) {
        JsonArray* arr = json_object_get_array_member(obj, "genres");
        if (arr) {
            guint len = json_array_get_length(arr);
            for (guint i = 0; i < len; i++) {
                const char* g = json_array_get_string_element(arr, i);
                if (g) show.genres.push_back(g);
            }
        }
    }
    return show;
}

static Episode parse_episode(JsonObject* obj) {
    Episode ep;
    if (json_object_has_member(obj, "season"))
        ep.season = static_cast<int>(json_object_get_int_member(obj, "season"));
    if (json_object_has_member(obj, "number"))
        ep.number = static_cast<int>(json_object_get_int_member(obj, "number"));
    if (json_object_has_member(obj, "title")) {
        const char* s = json_object_get_string_member(obj, "title");
        if (s) ep.title = s;
    }
    if (json_object_has_member(obj, "ids"))
        ep.ids = parse_ids(json_object_get_object_member(obj, "ids"));
    if (json_object_has_member(obj, "overview")) {
        const char* s = json_object_get_string_member(obj, "overview");
        if (s) ep.overview = s;
    }
    if (json_object_has_member(obj, "rating") && !json_object_get_null_member(obj, "rating"))
        ep.rating = json_object_get_double_member(obj, "rating");
    if (json_object_has_member(obj, "first_aired")) {
        const char* s = json_object_get_string_member(obj, "first_aired");
        if (s) ep.first_aired = s;
    }
    return ep;
}

TraktService::TraktService() {
    storage_path_ = get_storage_path();
}

TraktService::~TraktService() {
    save();
}

std::string TraktService::get_storage_path() {
    const char* data_dir = g_get_user_data_dir();
    std::string dir = std::string(data_dir) + "/madari";
    g_mkdir_with_parents(dir.c_str(), 0755);
    return dir + "/trakt.json";
}

void TraktService::load() {
    config_ = TraktConfig{};
    config_.enabled = false;
    config_.sync_watchlist = true;
    config_.sync_history = true;
    config_.sync_progress = true;
    
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_file(parser, storage_path_.c_str(), &error)) {
        g_info("No Trakt config found: %s", error ? error->message : "unknown");
        return;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || !JSON_NODE_HOLDS_OBJECT(root)) return;
    
    JsonObject* obj = json_node_get_object(root);
    
    if (json_object_has_member(obj, "client_id")) {
        const char* s = json_object_get_string_member(obj, "client_id");
        if (s) config_.client_id = s;
    }
    if (json_object_has_member(obj, "client_secret")) {
        const char* s = json_object_get_string_member(obj, "client_secret");
        if (s) config_.client_secret = s;
    }
    if (json_object_has_member(obj, "access_token")) {
        const char* s = json_object_get_string_member(obj, "access_token");
        if (s) config_.access_token = s;
    }
    if (json_object_has_member(obj, "refresh_token")) {
        const char* s = json_object_get_string_member(obj, "refresh_token");
        if (s) config_.refresh_token = s;
    }
    if (json_object_has_member(obj, "expires_at"))
        config_.expires_at = json_object_get_int_member(obj, "expires_at");
    if (json_object_has_member(obj, "enabled"))
        config_.enabled = json_object_get_boolean_member(obj, "enabled");
    if (json_object_has_member(obj, "sync_watchlist"))
        config_.sync_watchlist = json_object_get_boolean_member(obj, "sync_watchlist");
    if (json_object_has_member(obj, "sync_history"))
        config_.sync_history = json_object_get_boolean_member(obj, "sync_history");
    if (json_object_has_member(obj, "sync_progress"))
        config_.sync_progress = json_object_get_boolean_member(obj, "sync_progress");
    if (json_object_has_member(obj, "username")) {
        const char* s = json_object_get_string_member(obj, "username");
        if (s && strlen(s) > 0) config_.username = s;
    }
    if (json_object_has_member(obj, "avatar_url")) {
        const char* s = json_object_get_string_member(obj, "avatar_url");
        if (s && strlen(s) > 0) config_.avatar_url = s;
    }
}

void TraktService::save() {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    
    json_builder_begin_object(builder);
    
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, config_.client_id.c_str());
    
    json_builder_set_member_name(builder, "client_secret");
    json_builder_add_string_value(builder, config_.client_secret.c_str());
    
    json_builder_set_member_name(builder, "access_token");
    json_builder_add_string_value(builder, config_.access_token.c_str());
    
    json_builder_set_member_name(builder, "refresh_token");
    json_builder_add_string_value(builder, config_.refresh_token.c_str());
    
    json_builder_set_member_name(builder, "expires_at");
    json_builder_add_int_value(builder, config_.expires_at);
    
    json_builder_set_member_name(builder, "enabled");
    json_builder_add_boolean_value(builder, config_.enabled);
    
    json_builder_set_member_name(builder, "sync_watchlist");
    json_builder_add_boolean_value(builder, config_.sync_watchlist);
    
    json_builder_set_member_name(builder, "sync_history");
    json_builder_add_boolean_value(builder, config_.sync_history);
    
    json_builder_set_member_name(builder, "sync_progress");
    json_builder_add_boolean_value(builder, config_.sync_progress);
    
    if (config_.username.has_value()) {
        json_builder_set_member_name(builder, "username");
        json_builder_add_string_value(builder, config_.username->c_str());
    }
    
    if (config_.avatar_url.has_value()) {
        json_builder_set_member_name(builder, "avatar_url");
        json_builder_add_string_value(builder, config_.avatar_url->c_str());
    }
    
    json_builder_end_object(builder);
    
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    json_generator_set_root(gen, root);
    
    g_autoptr(GError) error = nullptr;
    if (!json_generator_to_file(gen, storage_path_.c_str(), &error)) {
        g_warning("Failed to save Trakt config: %s", error->message);
    }
}

const TraktConfig& TraktService::get_config() const {
    return config_;
}

void TraktService::set_config(const TraktConfig& config) {
    config_ = config;
    save();
    notify_change();
}

void TraktService::set_credentials(const std::string& client_id, const std::string& client_secret) {
    config_.client_id = client_id;
    config_.client_secret = client_secret;
    save();
    notify_change();
}

bool TraktService::is_configured() const {
    // Always configured since we use hardcoded client_id
    return true;
}

bool TraktService::is_authenticated() const {
    return config_.is_authenticated() && !config_.is_token_expired();
}

void TraktService::on_config_changed(ConfigChangedCallback callback) {
    change_callbacks_.push_back(std::move(callback));
}

void TraktService::notify_change() {
    for (const auto& cb : change_callbacks_) {
        cb();
    }
}

void TraktService::make_request(const std::string& method, const std::string& endpoint,
                                 const std::string& body, bool require_auth,
                                 std::function<void(const std::string&, int, const std::string&)> callback) {
    std::string url = std::string(TRAKT_API_URL) + endpoint;
    
    SoupMessage* msg = soup_message_new(method.c_str(), url.c_str());
    if (!msg) {
        g_warning("[Trakt] Failed to create HTTP request for %s", endpoint.c_str());
        callback("", 0, "Failed to create HTTP request");
        return;
    }
    
    // Add required headers - use hardcoded client_id
    SoupMessageHeaders* headers = soup_message_get_request_headers(msg);
    soup_message_headers_append(headers, "Content-Type", "application/json");
    soup_message_headers_append(headers, "trakt-api-key", TRAKT_CLIENT_ID);
    soup_message_headers_append(headers, "trakt-api-version", TRAKT_API_VERSION);
    soup_message_headers_append(headers, "User-Agent", "Madari/1.0 (Linux; GTK4/Libadwaita)");
    
    if (require_auth && !config_.access_token.empty()) {
        std::string auth = "Bearer " + config_.access_token;
        soup_message_headers_append(headers, "Authorization", auth.c_str());
    }
    
    if (!body.empty()) {
        GBytes* bytes = g_bytes_new(body.c_str(), body.size());
        soup_message_set_request_body_from_bytes(msg, "application/json", bytes);
        g_bytes_unref(bytes);
    }
    
    // Create session for request
    SoupSession* session = soup_session_new();
    
    struct RequestData {
        std::function<void(const std::string&, int, const std::string&)> callback;
        SoupSession* session;
        SoupMessage* msg;
    };
    
    RequestData* data = new RequestData{callback, session, msg};
    
    soup_session_send_and_read_async(session, msg, G_PRIORITY_DEFAULT, nullptr,
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            RequestData* data = static_cast<RequestData*>(user_data);
            
            g_autoptr(GError) error = nullptr;
            GBytes* bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
            
            if (error) {
                g_warning("[Trakt] Request error: %s", error->message);
                data->callback("", 0, error->message);
            } else {
                gsize size;
                const char* response_data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
                std::string response(response_data, size);
                
                guint status = soup_message_get_status(data->msg);
                
                if (status >= 200 && status < 300) {
                    data->callback(response, status, "");
                } else {
                    std::string err = "HTTP " + std::to_string(status);
                    // Try to parse error message from response
                    g_autoptr(JsonParser) parser = json_parser_new();
                    if (json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                        JsonNode* root = json_parser_get_root(parser);
                        if (root && JSON_NODE_HOLDS_OBJECT(root)) {
                            JsonObject* obj = json_node_get_object(root);
                            if (json_object_has_member(obj, "error")) {
                                const char* e = json_object_get_string_member(obj, "error");
                                if (e) err = e;
                            }
                            if (json_object_has_member(obj, "error_description")) {
                                const char* d = json_object_get_string_member(obj, "error_description");
                                if (d) err += ": " + std::string(d);
                            }
                        }
                    }
                    g_warning("[Trakt] Request failed: %s", err.c_str());
                    data->callback(response, status, err);
                }
                
                g_bytes_unref(bytes);
            }
            
            g_object_unref(data->session);
            g_object_unref(data->msg);
            delete data;
        }, data);
}

void TraktService::ensure_valid_token(std::function<void(bool valid)> callback) {
    if (!is_authenticated()) {
        if (!config_.refresh_token.empty()) {
            refresh_token([callback](bool success, const std::string&) {
                callback(success);
            });
        } else {
            callback(false);
        }
    } else {
        callback(true);
    }
}

// ============ Authentication ============

void TraktService::start_device_auth(DeviceCodeCallback callback) {
    std::string body = "{\"client_id\":\"";
    body += TRAKT_CLIENT_ID;
    body += "\"}";
    
    make_request("POST", "/oauth/device/code", body, false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            JsonObject* obj = json_node_get_object(root);
            
            DeviceCode code;
            if (json_object_has_member(obj, "device_code")) {
                const char* s = json_object_get_string_member(obj, "device_code");
                if (s) code.device_code = s;
            }
            if (json_object_has_member(obj, "user_code")) {
                const char* s = json_object_get_string_member(obj, "user_code");
                if (s) code.user_code = s;
            }
            if (json_object_has_member(obj, "verification_url")) {
                const char* s = json_object_get_string_member(obj, "verification_url");
                if (s) code.verification_url = s;
            }
            if (json_object_has_member(obj, "expires_in"))
                code.expires_in = static_cast<int>(json_object_get_int_member(obj, "expires_in"));
            if (json_object_has_member(obj, "interval"))
                code.interval = static_cast<int>(json_object_get_int_member(obj, "interval"));
            
            callback(code, "");
        });
}

void TraktService::poll_device_token(const std::string& device_code, TokenPollCallback callback) {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "code");
    json_builder_add_string_value(builder, device_code.c_str());
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, TRAKT_CLIENT_ID);
    json_builder_set_member_name(builder, "client_secret");
    json_builder_add_string_value(builder, config_.client_secret.c_str());
    json_builder_end_object(builder);
    
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar* body = json_generator_to_data(gen, nullptr);
    
    make_request("POST", "/oauth/device/token", body, false,
        [this, callback](const std::string& response, int status, const std::string& error) {
            if (status == 400) {
                // Still pending - user hasn't entered code yet
                callback(false, true, "Waiting for user authorization");
                return;
            }
            
            if (status == 404 || status == 409 || status == 410 || status == 418) {
                // Expired or denied
                callback(false, false, "Authorization expired or denied");
                return;
            }
            
            if (!error.empty() && status != 200) {
                callback(false, false, error);
                return;
            }
            
            // Parse token response
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(false, false, "Failed to parse token response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
                callback(false, false, "Invalid token response");
                return;
            }
            
            JsonObject* obj = json_node_get_object(root);
            
            if (json_object_has_member(obj, "access_token")) {
                const char* s = json_object_get_string_member(obj, "access_token");
                if (s) config_.access_token = s;
            }
            if (json_object_has_member(obj, "refresh_token")) {
                const char* s = json_object_get_string_member(obj, "refresh_token");
                if (s) config_.refresh_token = s;
            }
            if (json_object_has_member(obj, "expires_in")) {
                int64_t expires_in = json_object_get_int_member(obj, "expires_in");
                config_.expires_at = std::time(nullptr) + expires_in;
            }
            if (json_object_has_member(obj, "created_at")) {
                int64_t created_at = json_object_get_int_member(obj, "created_at");
                if (json_object_has_member(obj, "expires_in")) {
                    int64_t expires_in = json_object_get_int_member(obj, "expires_in");
                    config_.expires_at = created_at + expires_in;
                }
            }
            
            config_.enabled = true;
            save();
            notify_change();
            
            // Fetch user info
            get_user_settings([this](std::optional<UserSettings> settings, const std::string&) {
                if (settings) {
                    config_.username = settings->username;
                    if (!settings->avatar.has_value() || settings->avatar->empty()) {
                        config_.avatar_url = std::nullopt;
                    } else {
                        config_.avatar_url = settings->avatar;
                    }
                    save();
                    notify_change();
                }
            });
            
            callback(true, false, "");
        });
    
    g_free(body);
}

void TraktService::refresh_token(AuthCallback callback) {
    if (config_.refresh_token.empty()) {
        callback(false, "No refresh token available");
        return;
    }
    
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "refresh_token");
    json_builder_add_string_value(builder, config_.refresh_token.c_str());
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, TRAKT_CLIENT_ID);
    json_builder_set_member_name(builder, "client_secret");
    json_builder_add_string_value(builder, config_.client_secret.c_str());
    json_builder_set_member_name(builder, "redirect_uri");
    json_builder_add_string_value(builder, "urn:ietf:wg:oauth:2.0:oob");
    json_builder_set_member_name(builder, "grant_type");
    json_builder_add_string_value(builder, "refresh_token");
    json_builder_end_object(builder);
    
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar* body = json_generator_to_data(gen, nullptr);
    
    make_request("POST", "/oauth/token", body, false,
        [this, callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(false, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(false, "Failed to parse token response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
                callback(false, "Invalid token response");
                return;
            }
            
            JsonObject* obj = json_node_get_object(root);
            
            if (json_object_has_member(obj, "access_token")) {
                const char* s = json_object_get_string_member(obj, "access_token");
                if (s) config_.access_token = s;
            }
            if (json_object_has_member(obj, "refresh_token")) {
                const char* s = json_object_get_string_member(obj, "refresh_token");
                if (s) config_.refresh_token = s;
            }
            if (json_object_has_member(obj, "expires_in")) {
                int64_t expires_in = json_object_get_int_member(obj, "expires_in");
                config_.expires_at = std::time(nullptr) + expires_in;
            }
            
            save();
            notify_change();
            callback(true, "");
        });
    
    g_free(body);
}

void TraktService::logout(AuthCallback callback) {
    if (config_.access_token.empty()) {
        config_.access_token = "";
        config_.refresh_token = "";
        config_.expires_at = 0;
        config_.enabled = false;
        config_.username = std::nullopt;
        config_.avatar_url = std::nullopt;
        save();
        notify_change();
        callback(true, "");
        return;
    }
    
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "token");
    json_builder_add_string_value(builder, config_.access_token.c_str());
    json_builder_set_member_name(builder, "client_id");
    json_builder_add_string_value(builder, TRAKT_CLIENT_ID);
    json_builder_set_member_name(builder, "client_secret");
    json_builder_add_string_value(builder, config_.client_secret.c_str());
    json_builder_end_object(builder);
    
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar* body = json_generator_to_data(gen, nullptr);
    
    make_request("POST", "/oauth/revoke", body, false,
        [this, callback](const std::string&, int, const std::string&) {
            // Clear tokens regardless of revoke result
            config_.access_token = "";
            config_.refresh_token = "";
            config_.expires_at = 0;
            config_.enabled = false;
            config_.username = std::nullopt;
            config_.avatar_url = std::nullopt;
            save();
            notify_change();
            callback(true, "");
        });
    
    g_free(body);
}

void TraktService::get_user_settings(UserSettingsCallback callback) {
    make_request("GET", "/users/settings", "", true,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_OBJECT(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            JsonObject* obj = json_node_get_object(root);
            UserSettings settings;
            
            if (json_object_has_member(obj, "user")) {
                JsonObject* user = json_object_get_object_member(obj, "user");
                if (json_object_has_member(user, "username")) {
                    const char* s = json_object_get_string_member(user, "username");
                    if (s) settings.username = s;
                }
                if (json_object_has_member(user, "name")) {
                    const char* s = json_object_get_string_member(user, "name");
                    if (s && strlen(s) > 0) settings.name = s;
                }
                if (json_object_has_member(user, "vip"))
                    settings.is_vip = json_object_get_boolean_member(user, "vip");
                if (json_object_has_member(user, "images")) {
                    JsonObject* images = json_object_get_object_member(user, "images");
                    if (json_object_has_member(images, "avatar")) {
                        JsonObject* avatar = json_object_get_object_member(images, "avatar");
                        if (json_object_has_member(avatar, "full")) {
                            const char* s = json_object_get_string_member(avatar, "full");
                            if (s) settings.avatar = s;
                        }
                    }
                }
            }
            
            callback(settings, "");
        });
}

// ============ Catalog Methods ============

void TraktService::get_trending_movies(int page, int limit, MoviesCallback callback) {
    std::string endpoint = "/movies/trending?page=" + std::to_string(page) + 
                           "&limit=" + std::to_string(limit) + "&extended=full";
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<Movie> movies;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                
                if (json_object_has_member(obj, "movie")) {
                    JsonObject* movie_obj = json_object_get_object_member(obj, "movie");
                    movies.push_back(parse_movie(movie_obj));
                }
            }
            
            callback(movies, "");
        });
}

void TraktService::get_popular_movies(int page, int limit, MoviesCallback callback) {
    std::string endpoint = "/movies/popular?page=" + std::to_string(page) + 
                           "&limit=" + std::to_string(limit) + "&extended=full";
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<Movie> movies;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                movies.push_back(parse_movie(obj));
            }
            
            callback(movies, "");
        });
}

void TraktService::get_anticipated_movies(int page, int limit, MoviesCallback callback) {
    std::string endpoint = "/movies/anticipated?page=" + std::to_string(page) + 
                           "&limit=" + std::to_string(limit) + "&extended=full";
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<Movie> movies;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                
                if (json_object_has_member(obj, "movie")) {
                    JsonObject* movie_obj = json_object_get_object_member(obj, "movie");
                    movies.push_back(parse_movie(movie_obj));
                }
            }
            
            callback(movies, "");
        });
}

void TraktService::get_trending_shows(int page, int limit, ShowsCallback callback) {
    std::string endpoint = "/shows/trending?page=" + std::to_string(page) + 
                           "&limit=" + std::to_string(limit) + "&extended=full";
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<Show> shows;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                
                if (json_object_has_member(obj, "show")) {
                    JsonObject* show_obj = json_object_get_object_member(obj, "show");
                    shows.push_back(parse_show(show_obj));
                }
            }
            
            callback(shows, "");
        });
}

void TraktService::get_popular_shows(int page, int limit, ShowsCallback callback) {
    std::string endpoint = "/shows/popular?page=" + std::to_string(page) + 
                           "&limit=" + std::to_string(limit) + "&extended=full";
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<Show> shows;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                shows.push_back(parse_show(obj));
            }
            
            callback(shows, "");
        });
}

void TraktService::get_anticipated_shows(int page, int limit, ShowsCallback callback) {
    std::string endpoint = "/shows/anticipated?page=" + std::to_string(page) + 
                           "&limit=" + std::to_string(limit) + "&extended=full";
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<Show> shows;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                
                if (json_object_has_member(obj, "show")) {
                    JsonObject* show_obj = json_object_get_object_member(obj, "show");
                    shows.push_back(parse_show(show_obj));
                }
            }
            
            callback(shows, "");
        });
}

void TraktService::search(const std::string& query, const std::string& type, SearchCallback callback) {
    // URL encode query
    gchar* encoded = g_uri_escape_string(query.c_str(), nullptr, TRUE);
    std::string endpoint = "/search/" + type + "?query=" + encoded + "&extended=full";
    g_free(encoded);
    
    make_request("GET", endpoint, "", false,
        [callback](const std::string& response, int status, const std::string& error) {
            if (!error.empty()) {
                callback(std::nullopt, error);
                return;
            }
            
            g_autoptr(JsonParser) parser = json_parser_new();
            if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                callback(std::nullopt, "Failed to parse response");
                return;
            }
            
            JsonNode* root = json_parser_get_root(parser);
            if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                callback(std::nullopt, "Invalid response format");
                return;
            }
            
            std::vector<SearchResult> results;
            JsonArray* arr = json_node_get_array(root);
            guint len = json_array_get_length(arr);
            
            for (guint i = 0; i < len; i++) {
                JsonNode* item = json_array_get_element(arr, i);
                if (!JSON_NODE_HOLDS_OBJECT(item)) continue;
                JsonObject* obj = json_node_get_object(item);
                
                SearchResult result;
                if (json_object_has_member(obj, "type")) {
                    const char* s = json_object_get_string_member(obj, "type");
                    if (s) result.type = s;
                }
                if (json_object_has_member(obj, "score"))
                    result.score = json_object_get_double_member(obj, "score");
                
                if (json_object_has_member(obj, "movie")) {
                    result.movie = parse_movie(json_object_get_object_member(obj, "movie"));
                }
                if (json_object_has_member(obj, "show")) {
                    result.show = parse_show(json_object_get_object_member(obj, "show"));
                }
                if (json_object_has_member(obj, "episode")) {
                    result.episode = parse_episode(json_object_get_object_member(obj, "episode"));
                }
                
                results.push_back(result);
            }
            
            callback(results, "");
        });
}

// ============ Sync Methods ============

void TraktService::get_playback(PlaybackCallback callback) {
    ensure_valid_token([this, callback](bool valid) {
        if (!valid) {
            callback(std::nullopt, "Not authenticated");
            return;
        }
        
        make_request("GET", "/sync/playback?extended=full", "", true,
            [callback](const std::string& response, int status, const std::string& error) {
                if (!error.empty()) {
                    callback(std::nullopt, error);
                    return;
                }
                
                g_autoptr(JsonParser) parser = json_parser_new();
                if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                    callback(std::nullopt, "Failed to parse response");
                    return;
                }
                
                JsonNode* root = json_parser_get_root(parser);
                if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                    callback(std::nullopt, "Invalid response format");
                    return;
                }
                
                std::vector<PlaybackProgress> items;
                JsonArray* arr = json_node_get_array(root);
                guint len = json_array_get_length(arr);
                
                for (guint i = 0; i < len; i++) {
                    JsonNode* item_node = json_array_get_element(arr, i);
                    if (!JSON_NODE_HOLDS_OBJECT(item_node)) continue;
                    JsonObject* obj = json_node_get_object(item_node);
                    
                    PlaybackProgress item;
                    if (json_object_has_member(obj, "id"))
                        item.id = json_object_get_int_member(obj, "id");
                    if (json_object_has_member(obj, "progress"))
                        item.progress = json_object_get_double_member(obj, "progress");
                    if (json_object_has_member(obj, "paused_at")) {
                        const char* s = json_object_get_string_member(obj, "paused_at");
                        if (s) item.paused_at = s;
                    }
                    if (json_object_has_member(obj, "type")) {
                        const char* s = json_object_get_string_member(obj, "type");
                        if (s) item.type = s;
                    }
                    
                    if (json_object_has_member(obj, "movie")) {
                        item.movie = parse_movie(json_object_get_object_member(obj, "movie"));
                    }
                    if (json_object_has_member(obj, "show")) {
                        item.show = parse_show(json_object_get_object_member(obj, "show"));
                    }
                    if (json_object_has_member(obj, "episode")) {
                        item.episode = parse_episode(json_object_get_object_member(obj, "episode"));
                    }
                    
                    items.push_back(item);
                }
                
                callback(items, "");
            });
    });
}

void TraktService::remove_playback(int64_t playback_id, AuthCallback callback) {
    ensure_valid_token([this, playback_id, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        std::string endpoint = "/sync/playback/" + std::to_string(playback_id);
        make_request("DELETE", endpoint, "", true,
            [callback](const std::string&, int status, const std::string& error) {
                if (!error.empty() && status != 204) {
                    callback(false, error);
                } else {
                    callback(true, "");
                }
            });
    });
}

void TraktService::get_watchlist(const std::string& type, WatchlistCallback callback) {
    ensure_valid_token([this, type, callback](bool valid) {
        if (!valid) {
            callback(std::nullopt, "Not authenticated");
            return;
        }
        
        std::string endpoint = "/sync/watchlist";
        if (!type.empty()) {
            endpoint += "/" + type;
        }
        endpoint += "?extended=full";
        
        make_request("GET", endpoint, "", true,
            [callback](const std::string& response, int status, const std::string& error) {
                if (!error.empty()) {
                    callback(std::nullopt, error);
                    return;
                }
                
                g_autoptr(JsonParser) parser = json_parser_new();
                if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                    callback(std::nullopt, "Failed to parse response");
                    return;
                }
                
                JsonNode* root = json_parser_get_root(parser);
                if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                    callback(std::nullopt, "Invalid response format");
                    return;
                }
                
                std::vector<WatchlistItem> items;
                JsonArray* arr = json_node_get_array(root);
                guint len = json_array_get_length(arr);
                
                for (guint i = 0; i < len; i++) {
                    JsonNode* item_node = json_array_get_element(arr, i);
                    if (!JSON_NODE_HOLDS_OBJECT(item_node)) continue;
                    JsonObject* obj = json_node_get_object(item_node);
                    
                    WatchlistItem item;
                    if (json_object_has_member(obj, "rank"))
                        item.rank = json_object_get_int_member(obj, "rank");
                    if (json_object_has_member(obj, "listed_at")) {
                        const char* s = json_object_get_string_member(obj, "listed_at");
                        if (s) item.listed_at = s;
                    }
                    if (json_object_has_member(obj, "type")) {
                        const char* s = json_object_get_string_member(obj, "type");
                        if (s) item.type = s;
                    }
                    if (json_object_has_member(obj, "notes")) {
                        const char* s = json_object_get_string_member(obj, "notes");
                        if (s && strlen(s) > 0) item.notes = s;
                    }
                    
                    if (json_object_has_member(obj, "movie")) {
                        item.movie = parse_movie(json_object_get_object_member(obj, "movie"));
                    }
                    if (json_object_has_member(obj, "show")) {
                        item.show = parse_show(json_object_get_object_member(obj, "show"));
                    }
                    if (json_object_has_member(obj, "episode")) {
                        item.episode = parse_episode(json_object_get_object_member(obj, "episode"));
                    }
                    
                    items.push_back(item);
                }
                
                callback(items, "");
            });
    });
}

void TraktService::add_to_watchlist(const std::string& type, const std::string& imdb_id, AuthCallback callback) {
    ensure_valid_token([this, type, imdb_id, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        g_autoptr(JsonBuilder) builder = json_builder_new();
        json_builder_begin_object(builder);
        
        // Build the appropriate structure based on type
        std::string key = type + "s";  // movies, shows, etc.
        json_builder_set_member_name(builder, key.c_str());
        json_builder_begin_array(builder);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "ids");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "imdb");
        json_builder_add_string_value(builder, imdb_id.c_str());
        json_builder_end_object(builder);
        json_builder_end_object(builder);
        json_builder_end_array(builder);
        
        json_builder_end_object(builder);
        
        g_autoptr(JsonNode) root = json_builder_get_root(builder);
        g_autoptr(JsonGenerator) gen = json_generator_new();
        json_generator_set_root(gen, root);
        gchar* body = json_generator_to_data(gen, nullptr);
        
        make_request("POST", "/sync/watchlist", body, true,
            [callback](const std::string&, int status, const std::string& error) {
                if (!error.empty() && status != 201) {
                    callback(false, error);
                } else {
                    callback(true, "");
                }
            });
        
        g_free(body);
    });
}

void TraktService::remove_from_watchlist(const std::string& type, const std::string& imdb_id, AuthCallback callback) {
    ensure_valid_token([this, type, imdb_id, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        g_autoptr(JsonBuilder) builder = json_builder_new();
        json_builder_begin_object(builder);
        
        std::string key = type + "s";
        json_builder_set_member_name(builder, key.c_str());
        json_builder_begin_array(builder);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "ids");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "imdb");
        json_builder_add_string_value(builder, imdb_id.c_str());
        json_builder_end_object(builder);
        json_builder_end_object(builder);
        json_builder_end_array(builder);
        
        json_builder_end_object(builder);
        
        g_autoptr(JsonNode) root = json_builder_get_root(builder);
        g_autoptr(JsonGenerator) gen = json_generator_new();
        json_generator_set_root(gen, root);
        gchar* body = json_generator_to_data(gen, nullptr);
        
        make_request("POST", "/sync/watchlist/remove", body, true,
            [callback](const std::string&, int status, const std::string& error) {
                if (!error.empty() && status != 200) {
                    callback(false, error);
                } else {
                    callback(true, "");
                }
            });
        
        g_free(body);
    });
}

void TraktService::get_history(const std::string& type, int page, int limit, HistoryCallback callback) {
    ensure_valid_token([this, type, page, limit, callback](bool valid) {
        if (!valid) {
            callback(std::nullopt, "Not authenticated");
            return;
        }
        
        std::string endpoint = "/sync/history";
        if (!type.empty()) {
            endpoint += "/" + type;
        }
        endpoint += "?page=" + std::to_string(page) + "&limit=" + std::to_string(limit) + "&extended=full";
        
        make_request("GET", endpoint, "", true,
            [callback](const std::string& response, int status, const std::string& error) {
                if (!error.empty()) {
                    callback(std::nullopt, error);
                    return;
                }
                
                g_autoptr(JsonParser) parser = json_parser_new();
                if (!json_parser_load_from_data(parser, response.c_str(), -1, nullptr)) {
                    callback(std::nullopt, "Failed to parse response");
                    return;
                }
                
                JsonNode* root = json_parser_get_root(parser);
                if (!root || !JSON_NODE_HOLDS_ARRAY(root)) {
                    callback(std::nullopt, "Invalid response format");
                    return;
                }
                
                std::vector<HistoryItem> items;
                JsonArray* arr = json_node_get_array(root);
                guint len = json_array_get_length(arr);
                
                for (guint i = 0; i < len; i++) {
                    JsonNode* item_node = json_array_get_element(arr, i);
                    if (!JSON_NODE_HOLDS_OBJECT(item_node)) continue;
                    JsonObject* obj = json_node_get_object(item_node);
                    
                    HistoryItem item;
                    if (json_object_has_member(obj, "id"))
                        item.id = json_object_get_int_member(obj, "id");
                    if (json_object_has_member(obj, "watched_at")) {
                        const char* s = json_object_get_string_member(obj, "watched_at");
                        if (s) item.watched_at = s;
                    }
                    if (json_object_has_member(obj, "action")) {
                        const char* s = json_object_get_string_member(obj, "action");
                        if (s) item.action = s;
                    }
                    if (json_object_has_member(obj, "type")) {
                        const char* s = json_object_get_string_member(obj, "type");
                        if (s) item.type = s;
                    }
                    
                    if (json_object_has_member(obj, "movie")) {
                        item.movie = parse_movie(json_object_get_object_member(obj, "movie"));
                    }
                    if (json_object_has_member(obj, "show")) {
                        item.show = parse_show(json_object_get_object_member(obj, "show"));
                    }
                    if (json_object_has_member(obj, "episode")) {
                        item.episode = parse_episode(json_object_get_object_member(obj, "episode"));
                    }
                    
                    items.push_back(item);
                }
                
                callback(items, "");
            });
    });
}

void TraktService::add_to_history(const std::string& type, const std::string& imdb_id, 
                                   const std::string& watched_at, AuthCallback callback) {
    ensure_valid_token([this, type, imdb_id, watched_at, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        g_autoptr(JsonBuilder) builder = json_builder_new();
        json_builder_begin_object(builder);
        
        std::string key = type + "s";
        json_builder_set_member_name(builder, key.c_str());
        json_builder_begin_array(builder);
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "ids");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "imdb");
        json_builder_add_string_value(builder, imdb_id.c_str());
        json_builder_end_object(builder);
        if (!watched_at.empty()) {
            json_builder_set_member_name(builder, "watched_at");
            json_builder_add_string_value(builder, watched_at.c_str());
        }
        json_builder_end_object(builder);
        json_builder_end_array(builder);
        
        json_builder_end_object(builder);
        
        g_autoptr(JsonNode) root = json_builder_get_root(builder);
        g_autoptr(JsonGenerator) gen = json_generator_new();
        json_generator_set_root(gen, root);
        gchar* body = json_generator_to_data(gen, nullptr);
        
        make_request("POST", "/sync/history", body, true,
            [callback](const std::string&, int status, const std::string& error) {
                if (!error.empty() && status != 201) {
                    callback(false, error);
                } else {
                    callback(true, "");
                }
            });
        
        g_free(body);
    });
}

// ============ ID Parsing ============

ContentIds parse_stremio_id(const std::string& id) {
    ContentIds result;
    
    if (id.empty()) {
        return result;
    }
    
    // Split by ':' to handle various formats
    std::vector<std::string> parts;
    std::string current;
    for (char c : id) {
        if (c == ':') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    
    if (parts.empty()) {
        return result;
    }
    
    // Determine ID type based on first part
    const std::string& first = parts[0];
    
    if (first.substr(0, 2) == "tt") {
        // IMDB ID (e.g., "tt1234567" or "tt1234567:2:5")
        result.imdb = first;
        
        // Check for episode format: tt1234567:season:episode
        if (parts.size() >= 3) {
            try {
                result.season = std::stoi(parts[1]);
                result.episode = std::stoi(parts[2]);
                result.is_episode = true;
            } catch (...) {
                // Not valid episode numbers, ignore
            }
        }
    } else if (first == "tmdb" && parts.size() >= 2) {
        // TMDB ID (e.g., "tmdb:12345" or "tmdb:12345:2:5")
        try {
            result.tmdb = std::stoll(parts[1]);
            
            // Check for episode format: tmdb:id:season:episode
            if (parts.size() >= 4) {
                result.season = std::stoi(parts[2]);
                result.episode = std::stoi(parts[3]);
                result.is_episode = true;
            }
        } catch (...) {
            // Invalid number
        }
    } else if (first == "tvdb" && parts.size() >= 2) {
        // TVDB ID (e.g., "tvdb:67890" or "tvdb:67890:2:5")
        try {
            result.tvdb = std::stoll(parts[1]);
            
            // Check for episode format: tvdb:id:season:episode
            if (parts.size() >= 4) {
                result.season = std::stoi(parts[2]);
                result.episode = std::stoi(parts[3]);
                result.is_episode = true;
            }
        } catch (...) {
            // Invalid number
        }
    } else if (first == "kitsu" && parts.size() >= 2) {
        // Kitsu ID (e.g., "kitsu:12345" or "kitsu:12345:2:5")
        try {
            result.kitsu = std::stoll(parts[1]);
            
            // Check for episode format: kitsu:id:season:episode
            if (parts.size() >= 4) {
                result.season = std::stoi(parts[2]);
                result.episode = std::stoi(parts[3]);
                result.is_episode = true;
            }
        } catch (...) {
            // Invalid number
        }
    }
    
    return result;
}

// ============ Scrobble Methods ============

// Helper to add IDs object to JSON builder
static void add_ids_to_builder(JsonBuilder* builder, const ContentIds& ids) {
    json_builder_set_member_name(builder, "ids");
    json_builder_begin_object(builder);
    
    if (ids.imdb.has_value()) {
        json_builder_set_member_name(builder, "imdb");
        json_builder_add_string_value(builder, ids.imdb->c_str());
    }
    if (ids.tmdb.has_value()) {
        json_builder_set_member_name(builder, "tmdb");
        json_builder_add_int_value(builder, *ids.tmdb);
    }
    if (ids.tvdb.has_value()) {
        json_builder_set_member_name(builder, "tvdb");
        json_builder_add_int_value(builder, *ids.tvdb);
    }
    
    json_builder_end_object(builder);
}

// Build scrobble body with support for multiple ID types and episodes
static std::string build_scrobble_body(const std::string& content_type, const ContentIds& ids, double progress) {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    
    // Determine if this is an episode based on content_type and episode info
    bool is_episode = (content_type == "series" || content_type == "episode") && ids.is_episode;
    
    if (is_episode) {
        // Episode scrobble format:
        // { "show": { "ids": {...} }, "episode": { "season": N, "number": N }, "progress": P }
        
        // Show object with IDs
        json_builder_set_member_name(builder, "show");
        json_builder_begin_object(builder);
        add_ids_to_builder(builder, ids);
        json_builder_end_object(builder);
        
        // Episode object with season/number
        json_builder_set_member_name(builder, "episode");
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "season");
        json_builder_add_int_value(builder, ids.season);
        json_builder_set_member_name(builder, "number");
        json_builder_add_int_value(builder, ids.episode);
        json_builder_end_object(builder);
    } else {
        // Movie scrobble format:
        // { "movie": { "ids": {...} }, "progress": P }
        json_builder_set_member_name(builder, "movie");
        json_builder_begin_object(builder);
        add_ids_to_builder(builder, ids);
        json_builder_end_object(builder);
    }
    
    // Progress
    json_builder_set_member_name(builder, "progress");
    json_builder_add_double_value(builder, progress);
    
    json_builder_end_object(builder);
    
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    json_generator_set_root(gen, root);
    gchar* body = json_generator_to_data(gen, nullptr);
    std::string result(body);
    g_free(body);
    return result;
}

void TraktService::scrobble_start(const std::string& content_type, const ContentIds& ids,
                                   double progress, AuthCallback callback) {
    if (!ids.has_id()) {
        callback(false, "No valid ID found for scrobbling");
        return;
    }
    
    ensure_valid_token([this, content_type, ids, progress, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        std::string body = build_scrobble_body(content_type, ids, progress);
        g_print("[Trakt] Scrobble start: %s\n", body.c_str());
        
        make_request("POST", "/scrobble/start", body, true,
            [callback](const std::string& response, int status, const std::string& error) {
                if (!error.empty() && status != 201) {
                    g_warning("[Trakt] Scrobble start failed: %s (status: %d)", error.c_str(), status);
                    callback(false, error);
                } else {
                    g_print("[Trakt] Scrobble start success\n");
                    callback(true, "");
                }
            });
    });
}

void TraktService::scrobble_pause(const std::string& content_type, const ContentIds& ids,
                                   double progress, AuthCallback callback) {
    if (!ids.has_id()) {
        callback(false, "No valid ID found for scrobbling");
        return;
    }
    
    ensure_valid_token([this, content_type, ids, progress, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        std::string body = build_scrobble_body(content_type, ids, progress);
        g_print("[Trakt] Scrobble pause: %s\n", body.c_str());
        
        make_request("POST", "/scrobble/pause", body, true,
            [callback](const std::string& response, int status, const std::string& error) {
                if (!error.empty() && status != 201) {
                    g_warning("[Trakt] Scrobble pause failed: %s (status: %d)", error.c_str(), status);
                    callback(false, error);
                } else {
                    g_print("[Trakt] Scrobble pause success\n");
                    callback(true, "");
                }
            });
    });
}

void TraktService::scrobble_stop(const std::string& content_type, const ContentIds& ids,
                                  double progress, AuthCallback callback) {
    if (!ids.has_id()) {
        callback(false, "No valid ID found for scrobbling");
        return;
    }
    
    ensure_valid_token([this, content_type, ids, progress, callback](bool valid) {
        if (!valid) {
            callback(false, "Not authenticated");
            return;
        }
        
        std::string body = build_scrobble_body(content_type, ids, progress);
        g_print("[Trakt] Scrobble stop (progress: %.1f%%): %s\n", progress, body.c_str());
        
        make_request("POST", "/scrobble/stop", body, true,
            [callback, progress](const std::string& response, int status, const std::string& error) {
                if (!error.empty() && status != 201) {
                    g_warning("[Trakt] Scrobble stop failed: %s (status: %d)", error.c_str(), status);
                    callback(false, error);
                } else {
                    if (progress >= 80.0) {
                        g_print("[Trakt] Scrobble stop success - marked as watched\n");
                    } else {
                        g_print("[Trakt] Scrobble stop success - saved progress\n");
                    }
                    callback(true, "");
                }
            });
    });
}

} // namespace Trakt
