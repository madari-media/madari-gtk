#include "stremio_client.hpp"
#include "stremio_parser.hpp"
#include <sstream>

namespace Stremio {

Client::Client() {
    session_ = soup_session_new();
    // Set a reasonable timeout
    g_object_set(session_, "timeout", 30, nullptr);
}

Client::~Client() {
    if (session_) {
        g_object_unref(session_);
    }
}

std::string Client::get_base_url(const std::string& transport_url) {
    // Remove /manifest.json if present
    std::string base = transport_url;
    const std::string suffix = "/manifest.json";
    if (base.length() >= suffix.length() && 
        base.substr(base.length() - suffix.length()) == suffix) {
        base = base.substr(0, base.length() - suffix.length());
    }
    // Remove trailing slash
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

std::string Client::build_url(const std::string& base_url, const std::string& path) {
    std::string base = get_base_url(base_url);
    if (path.empty() || path[0] != '/') {
        return base + "/" + path;
    }
    return base + path;
}

void Client::make_request(const std::string& url, 
                          std::function<void(const std::string& body, const std::string& error)> callback) {
    SoupMessage* msg = soup_message_new("GET", url.c_str());
    if (!msg) {
        callback("", "Invalid URL: " + url);
        return;
    }
    
    // Add headers
    SoupMessageHeaders* headers = soup_message_get_request_headers(msg);
    soup_message_headers_append(headers, "Accept", "application/json");
    soup_message_headers_append(headers, "User-Agent", "Madari/1.0");
    
    // Create callback data
    struct RequestData {
        std::function<void(const std::string&, const std::string&)> callback;
    };
    auto* data = new RequestData{std::move(callback)};
    
    soup_session_send_and_read_async(
        session_,
        msg,
        G_PRIORITY_DEFAULT,
        nullptr, // cancellable
        [](GObject* source, GAsyncResult* result, gpointer user_data) {
            auto* data = static_cast<RequestData*>(user_data);
            g_autoptr(GError) error = nullptr;
            
            GBytes* bytes = soup_session_send_and_read_finish(
                SOUP_SESSION(source), result, &error);
            
            if (error) {
                data->callback("", std::string("Request failed: ") + error->message);
                delete data;
                return;
            }
            
            SoupMessage* msg = soup_session_get_async_result_message(
                SOUP_SESSION(source), result);
            guint status = soup_message_get_status(msg);
            
            if (status < 200 || status >= 300) {
                data->callback("", "HTTP error: " + std::to_string(status));
                g_bytes_unref(bytes);
                delete data;
                return;
            }
            
            gsize size;
            const char* body_data = static_cast<const char*>(g_bytes_get_data(bytes, &size));
            std::string body(body_data, size);
            
            g_bytes_unref(bytes);
            data->callback(body, "");
            delete data;
        },
        data
    );
    
    g_object_unref(msg);
}

void Client::fetch_manifest(const std::string& url, ManifestCallback callback) {
    std::string manifest_url = url;
    
    // Ensure URL ends with /manifest.json
    if (manifest_url.find("/manifest.json") == std::string::npos) {
        manifest_url = get_base_url(url) + "/manifest.json";
    }
    
    make_request(manifest_url, [callback, manifest_url](const std::string& body, const std::string& error) {
        if (!error.empty()) {
            callback(std::nullopt, error);
            return;
        }
        
        auto manifest = Parser::parse_manifest(body, manifest_url);
        if (!manifest) {
            callback(std::nullopt, "Failed to parse manifest");
            return;
        }
        
        callback(manifest, "");
    });
}

void Client::fetch_catalog(const Manifest& manifest,
                           const std::string& type,
                           const std::string& catalog_id,
                           const ExtraArgs& extra,
                           CatalogCallback callback) {
    std::ostringstream path;
    path << "/catalog/" << type << "/" << catalog_id;
    
    std::string extra_segment = extra.to_path_segment();
    if (!extra_segment.empty()) {
        path << "/" << extra_segment;
    }
    path << ".json";
    
    std::string url = build_url(manifest.transport_url, path.str());
    
    make_request(url, [callback](const std::string& body, const std::string& error) {
        if (!error.empty()) {
            callback(std::nullopt, error);
            return;
        }
        
        auto response = Parser::parse_catalog(body);
        if (!response) {
            callback(std::nullopt, "Failed to parse catalog response");
            return;
        }
        
        callback(response, "");
    });
}

void Client::fetch_meta(const Manifest& manifest,
                        const std::string& type,
                        const std::string& id,
                        MetaCallback callback) {
    std::ostringstream path;
    path << "/meta/" << type << "/" << id << ".json";
    
    std::string url = build_url(manifest.transport_url, path.str());
    
    make_request(url, [callback](const std::string& body, const std::string& error) {
        if (!error.empty()) {
            callback(std::nullopt, error);
            return;
        }
        
        auto response = Parser::parse_meta(body);
        if (!response) {
            callback(std::nullopt, "Failed to parse meta response");
            return;
        }
        
        callback(response, "");
    });
}

void Client::fetch_streams(const Manifest& manifest,
                           const std::string& type,
                           const std::string& video_id,
                           StreamsCallback callback) {
    std::ostringstream path;
    path << "/stream/" << type << "/" << video_id << ".json";
    
    std::string url = build_url(manifest.transport_url, path.str());
    
    make_request(url, [callback](const std::string& body, const std::string& error) {
        if (!error.empty()) {
            callback(std::nullopt, error);
            return;
        }
        
        auto response = Parser::parse_streams(body);
        if (!response) {
            callback(std::nullopt, "Failed to parse streams response");
            return;
        }
        
        callback(response, "");
    });
}

void Client::fetch_subtitles(const Manifest& manifest,
                             const std::string& type,
                             const std::string& id,
                             const std::string& video_id,
                             std::optional<int64_t> video_size,
                             SubtitlesCallback callback) {
    std::ostringstream path;
    path << "/subtitles/" << type << "/" << id;
    
    // Add extra args
    std::ostringstream extra;
    extra << "videoID=" << video_id;
    if (video_size) {
        extra << "&videoSize=" << *video_size;
    }
    path << "/" << extra.str() << ".json";
    
    std::string url = build_url(manifest.transport_url, path.str());
    
    make_request(url, [callback](const std::string& body, const std::string& error) {
        if (!error.empty()) {
            callback(std::nullopt, error);
            return;
        }
        
        auto response = Parser::parse_subtitles(body);
        if (!response) {
            callback(std::nullopt, "Failed to parse subtitles response");
            return;
        }
        
        callback(response, "");
    });
}

} // namespace Stremio
