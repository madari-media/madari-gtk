#include "stremio_addon_service.hpp"
#include "stremio_parser.hpp"
#include <json-glib/json-glib.h>
#include <glib.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <ctime>

namespace Stremio {

AddonService::AddonService() : client_(std::make_unique<Client>()) {
    storage_path_ = get_storage_path();
}

AddonService::~AddonService() = default;

std::string AddonService::get_storage_path() {
    const char* data_dir = g_get_user_data_dir();
    std::string dir = std::string(data_dir) + "/madari";
    g_mkdir_with_parents(dir.c_str(), 0755);
    return dir + "/addons.json";
}

void AddonService::load() {
    installed_addons_.clear();
    
    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(GError) error = nullptr;
    
    if (!json_parser_load_from_file(parser, storage_path_.c_str(), &error)) {
        // File doesn't exist or is invalid, that's fine
        g_info("No addons file found or failed to load: %s", 
               error ? error->message : "unknown error");
        return;
    }
    
    JsonNode* root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        return;
    }
    
    JsonObject* obj = json_node_get_object(root);
    if (!json_object_has_member(obj, "addons")) {
        return;
    }
    
    JsonNode* addons_node = json_object_get_member(obj, "addons");
    if (json_node_get_node_type(addons_node) != JSON_NODE_ARRAY) {
        return;
    }
    
    JsonArray* addons_array = json_node_get_array(addons_node);
    guint len = json_array_get_length(addons_array);
    
    for (guint i = 0; i < len; i++) {
        JsonNode* addon_node = json_array_get_element(addons_array, i);
        if (json_node_get_node_type(addon_node) != JSON_NODE_OBJECT) {
            continue;
        }
        
        JsonObject* addon_obj = json_node_get_object(addon_node);
        
        InstalledAddon addon;
        
        // Parse manifest
        if (json_object_has_member(addon_obj, "manifest")) {
            JsonNode* manifest_node = json_object_get_member(addon_obj, "manifest");
            if (json_node_get_node_type(manifest_node) == JSON_NODE_OBJECT) {
                g_autoptr(JsonGenerator) gen = json_generator_new();
                json_generator_set_root(gen, manifest_node);
                gchar* manifest_json = json_generator_to_data(gen, nullptr);
                
                std::string transport_url;
                if (json_object_has_member(addon_obj, "transport_url")) {
                    const char* url = json_object_get_string_member(addon_obj, "transport_url");
                    if (url) transport_url = url;
                }
                
                auto manifest = Parser::parse_manifest(manifest_json, transport_url);
                g_free(manifest_json);
                
                if (manifest) {
                    addon.manifest = *manifest;
                } else {
                    continue; // Skip invalid addon
                }
            }
        } else {
            continue; // Skip addon without manifest
        }
        
        // Parse other fields
        if (json_object_has_member(addon_obj, "enabled")) {
            addon.enabled = json_object_get_boolean_member(addon_obj, "enabled");
        }
        if (json_object_has_member(addon_obj, "order")) {
            addon.order = static_cast<int>(json_object_get_int_member(addon_obj, "order"));
        }
        if (json_object_has_member(addon_obj, "installed_at")) {
            const char* date = json_object_get_string_member(addon_obj, "installed_at");
            if (date) addon.installed_at = date;
        }
        
        installed_addons_.push_back(addon);
    }
    
    // Sort by order
    std::sort(installed_addons_.begin(), installed_addons_.end(),
              [](const InstalledAddon& a, const InstalledAddon& b) {
                  return a.order < b.order;
              });
}

void AddonService::save() {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "version");
    json_builder_add_int_value(builder, 1);
    
    json_builder_set_member_name(builder, "addons");
    json_builder_begin_array(builder);
    
    for (const auto& addon : installed_addons_) {
        json_builder_begin_object(builder);
        
        // Store transport URL
        json_builder_set_member_name(builder, "transport_url");
        json_builder_add_string_value(builder, addon.manifest.transport_url.c_str());
        
        // Store manifest
        json_builder_set_member_name(builder, "manifest");
        json_builder_begin_object(builder);
        
        json_builder_set_member_name(builder, "id");
        json_builder_add_string_value(builder, addon.manifest.id.c_str());
        
        json_builder_set_member_name(builder, "version");
        json_builder_add_string_value(builder, addon.manifest.version.c_str());
        
        json_builder_set_member_name(builder, "name");
        json_builder_add_string_value(builder, addon.manifest.name.c_str());
        
        json_builder_set_member_name(builder, "description");
        json_builder_add_string_value(builder, addon.manifest.description.c_str());
        
        if (addon.manifest.logo) {
            json_builder_set_member_name(builder, "logo");
            json_builder_add_string_value(builder, addon.manifest.logo->c_str());
        }
        
        if (addon.manifest.background) {
            json_builder_set_member_name(builder, "background");
            json_builder_add_string_value(builder, addon.manifest.background->c_str());
        }
        
        // Types
        json_builder_set_member_name(builder, "types");
        json_builder_begin_array(builder);
        for (const auto& type : addon.manifest.types) {
            json_builder_add_string_value(builder, type.c_str());
        }
        json_builder_end_array(builder);
        
        // ID prefixes
        json_builder_set_member_name(builder, "idPrefixes");
        json_builder_begin_array(builder);
        for (const auto& prefix : addon.manifest.id_prefixes) {
            json_builder_add_string_value(builder, prefix.c_str());
        }
        json_builder_end_array(builder);
        
        // Resources
        json_builder_set_member_name(builder, "resources");
        json_builder_begin_array(builder);
        for (const auto& res : addon.manifest.resources) {
            if (res.types.empty() && res.id_prefixes.empty()) {
                json_builder_add_string_value(builder, res.name.c_str());
            } else {
                json_builder_begin_object(builder);
                json_builder_set_member_name(builder, "name");
                json_builder_add_string_value(builder, res.name.c_str());
                
                if (!res.types.empty()) {
                    json_builder_set_member_name(builder, "types");
                    json_builder_begin_array(builder);
                    for (const auto& t : res.types) {
                        json_builder_add_string_value(builder, t.c_str());
                    }
                    json_builder_end_array(builder);
                }
                
                if (!res.id_prefixes.empty()) {
                    json_builder_set_member_name(builder, "idPrefixes");
                    json_builder_begin_array(builder);
                    for (const auto& p : res.id_prefixes) {
                        json_builder_add_string_value(builder, p.c_str());
                    }
                    json_builder_end_array(builder);
                }
                
                json_builder_end_object(builder);
            }
        }
        json_builder_end_array(builder);
        
        // Catalogs
        json_builder_set_member_name(builder, "catalogs");
        json_builder_begin_array(builder);
        for (const auto& cat : addon.manifest.catalogs) {
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "type");
            json_builder_add_string_value(builder, cat.type.c_str());
            json_builder_set_member_name(builder, "id");
            json_builder_add_string_value(builder, cat.id.c_str());
            json_builder_set_member_name(builder, "name");
            json_builder_add_string_value(builder, cat.name.c_str());
            json_builder_end_object(builder);
        }
        json_builder_end_array(builder);
        
        json_builder_end_object(builder); // manifest
        
        // Other fields
        json_builder_set_member_name(builder, "enabled");
        json_builder_add_boolean_value(builder, addon.enabled);
        
        json_builder_set_member_name(builder, "order");
        json_builder_add_int_value(builder, addon.order);
        
        json_builder_set_member_name(builder, "installed_at");
        json_builder_add_string_value(builder, addon.installed_at.c_str());
        
        json_builder_end_object(builder);
    }
    
    json_builder_end_array(builder);
    json_builder_end_object(builder);
    
    g_autoptr(JsonNode) root = json_builder_get_root(builder);
    g_autoptr(JsonGenerator) gen = json_generator_new();
    json_generator_set_pretty(gen, TRUE);
    json_generator_set_root(gen, root);
    
    g_autoptr(GError) error = nullptr;
    if (!json_generator_to_file(gen, storage_path_.c_str(), &error)) {
        g_warning("Failed to save addons: %s", error->message);
    }
}

const std::vector<InstalledAddon>& AddonService::get_installed_addons() const {
    return installed_addons_;
}

std::vector<InstalledAddon> AddonService::get_enabled_addons() const {
    std::vector<InstalledAddon> enabled;
    for (const auto& addon : installed_addons_) {
        if (addon.enabled) {
            enabled.push_back(addon);
        }
    }
    return enabled;
}

bool AddonService::is_installed(const std::string& addon_id) const {
    for (const auto& addon : installed_addons_) {
        if (addon.manifest.id == addon_id) {
            return true;
        }
    }
    return false;
}

void AddonService::install_addon(const std::string& url, InstallCallback callback) {
    client_->fetch_manifest(url, [this, callback](std::optional<Manifest> manifest, const std::string& error) {
        if (!manifest) {
            callback(false, error.empty() ? "Failed to fetch manifest" : error);
            return;
        }
        
        // Check if already installed
        if (is_installed(manifest->id)) {
            // Update existing addon
            for (auto& addon : installed_addons_) {
                if (addon.manifest.id == manifest->id) {
                    addon.manifest = *manifest;
                    break;
                }
            }
        } else {
            // Add new addon
            InstalledAddon addon;
            addon.manifest = *manifest;
            addon.enabled = true;
            addon.order = static_cast<int>(installed_addons_.size());
            
            // Get current timestamp
            std::time_t now = std::time(nullptr);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
            addon.installed_at = buf;
            
            installed_addons_.push_back(addon);
        }
        
        save();
        notify_change();
        callback(true, "");
    });
}

bool AddonService::uninstall_addon(const std::string& addon_id) {
    auto it = std::find_if(installed_addons_.begin(), installed_addons_.end(),
                           [&addon_id](const InstalledAddon& addon) {
                               return addon.manifest.id == addon_id;
                           });
    
    if (it == installed_addons_.end()) {
        return false;
    }
    
    installed_addons_.erase(it);
    
    // Re-number orders
    for (size_t i = 0; i < installed_addons_.size(); i++) {
        installed_addons_[i].order = static_cast<int>(i);
    }
    
    save();
    notify_change();
    return true;
}

bool AddonService::set_addon_enabled(const std::string& addon_id, bool enabled) {
    for (auto& addon : installed_addons_) {
        if (addon.manifest.id == addon_id) {
            addon.enabled = enabled;
            save();
            notify_change();
            return true;
        }
    }
    return false;
}

bool AddonService::move_addon(const std::string& addon_id, int direction) {
    auto it = std::find_if(installed_addons_.begin(), installed_addons_.end(),
                           [&addon_id](const InstalledAddon& addon) {
                               return addon.manifest.id == addon_id;
                           });
    
    if (it == installed_addons_.end()) {
        return false;
    }
    
    size_t index = std::distance(installed_addons_.begin(), it);
    size_t new_index = index;
    
    if (direction < 0 && index > 0) {
        new_index = index - 1;
    } else if (direction > 0 && index < installed_addons_.size() - 1) {
        new_index = index + 1;
    } else {
        return false; // Can't move
    }
    
    std::swap(installed_addons_[index], installed_addons_[new_index]);
    
    // Update orders
    for (size_t i = 0; i < installed_addons_.size(); i++) {
        installed_addons_[i].order = static_cast<int>(i);
    }
    
    save();
    notify_change();
    return true;
}

std::optional<InstalledAddon> AddonService::get_addon(const std::string& addon_id) const {
    for (const auto& addon : installed_addons_) {
        if (addon.manifest.id == addon_id) {
            return addon;
        }
    }
    return std::nullopt;
}

void AddonService::on_addons_changed(AddonsChangedCallback callback) {
    change_callbacks_.push_back(std::move(callback));
}

void AddonService::notify_change() {
    for (const auto& callback : change_callbacks_) {
        callback();
    }
}

std::vector<std::pair<Manifest, CatalogDefinition>> AddonService::get_all_catalogs() const {
    std::vector<std::pair<Manifest, CatalogDefinition>> result;
    
    for (const auto& addon : installed_addons_) {
        if (!addon.enabled || !addon.manifest.has_resource("catalog")) {
            continue;
        }
        
        for (const auto& catalog : addon.manifest.catalogs) {
            result.emplace_back(addon.manifest, catalog);
        }
    }
    
    return result;
}

std::vector<std::pair<Manifest, CatalogDefinition>> AddonService::get_catalogs_by_type(const std::string& type) const {
    std::vector<std::pair<Manifest, CatalogDefinition>> result;
    
    for (const auto& addon : installed_addons_) {
        if (!addon.enabled || !addon.manifest.has_resource("catalog")) {
            continue;
        }
        
        for (const auto& catalog : addon.manifest.catalogs) {
            if (catalog.type == type) {
                result.emplace_back(addon.manifest, catalog);
            }
        }
    }
    
    return result;
}

std::vector<InstalledAddon> AddonService::get_addons_for_resource(
    const std::string& resource,
    const std::string& type,
    const std::string& id) const {
    
    std::vector<InstalledAddon> result;
    
    for (const auto& addon : installed_addons_) {
        if (!addon.enabled) continue;
        
        bool resource_matches = false;
        bool type_matches = addon.manifest.types.empty();
        bool id_matches = addon.manifest.id_prefixes.empty() || id.empty();
        
        for (const auto& res : addon.manifest.resources) {
            if (res.name != resource) continue;
            
            resource_matches = true;
            
            // Check type filtering
            if (!res.types.empty()) {
                type_matches = std::find(res.types.begin(), res.types.end(), type) != res.types.end();
            } else if (!addon.manifest.types.empty()) {
                type_matches = std::find(addon.manifest.types.begin(), 
                                         addon.manifest.types.end(), type) != addon.manifest.types.end();
            } else {
                type_matches = true;
            }
            
            // Check ID prefix filtering
            if (!id.empty()) {
                if (!res.id_prefixes.empty()) {
                    id_matches = false;
                    for (const auto& prefix : res.id_prefixes) {
                        if (id.rfind(prefix, 0) == 0) {
                            id_matches = true;
                            break;
                        }
                    }
                } else if (!addon.manifest.id_prefixes.empty()) {
                    id_matches = false;
                    for (const auto& prefix : addon.manifest.id_prefixes) {
                        if (id.rfind(prefix, 0) == 0) {
                            id_matches = true;
                            break;
                        }
                    }
                }
            }
            
            if (resource_matches && type_matches && id_matches) {
                result.push_back(addon);
                break;
            }
        }
    }
    
    return result;
}

void AddonService::fetch_catalog(const std::string& addon_id,
                                  const std::string& type,
                                  const std::string& catalog_id,
                                  const ExtraArgs& extra,
                                  Client::CatalogCallback callback) {
    auto addon = get_addon(addon_id);
    if (!addon) {
        callback(std::nullopt, "Addon not found: " + addon_id);
        return;
    }
    
    client_->fetch_catalog(addon->manifest, type, catalog_id, extra, callback);
}

void AddonService::fetch_meta(const std::string& type,
                               const std::string& id,
                               Client::MetaCallback callback) {
    auto addons = get_addons_for_resource("meta", type, id);
    
    if (addons.empty()) {
        callback(std::nullopt, "No addon supports meta for type: " + type);
        return;
    }
    
    // Try first matching addon
    client_->fetch_meta(addons[0].manifest, type, id, callback);
}

void AddonService::fetch_all_streams(const std::string& type,
                                      const std::string& video_id,
                                      std::function<void(const Manifest&, const std::vector<Stream>&)> callback,
                                      std::function<void()> done_callback) {
    auto addons = get_addons_for_resource("stream", type, video_id);
    
    if (addons.empty()) {
        done_callback();
        return;
    }
    
    // Create a counter to track completion
    auto pending = std::make_shared<int>(static_cast<int>(addons.size()));
    
    for (const auto& addon : addons) {
        client_->fetch_streams(addon.manifest, type, video_id,
            [callback, done_callback, pending, manifest = addon.manifest]
            (std::optional<StreamsResponse> response, const std::string& error) {
                if (response && !response->streams.empty()) {
                    callback(manifest, response->streams);
                }
                
                (*pending)--;
                if (*pending == 0) {
                    done_callback();
                }
            });
    }
}

void AddonService::fetch_all_subtitles(const std::string& type,
                                        const std::string& id,
                                        const std::string& video_id,
                                        std::optional<int64_t> video_size,
                                        std::function<void(const Manifest&, const std::vector<Subtitle>&)> callback,
                                        std::function<void()> done_callback) {
    auto addons = get_addons_for_resource("subtitles", type, id);
    
    if (addons.empty()) {
        done_callback();
        return;
    }
    
    auto pending = std::make_shared<int>(static_cast<int>(addons.size()));
    
    for (const auto& addon : addons) {
        client_->fetch_subtitles(addon.manifest, type, id, video_id, video_size,
            [callback, done_callback, pending, manifest = addon.manifest]
            (std::optional<SubtitlesResponse> response, const std::string& error) {
                if (response && !response->subtitles.empty()) {
                    callback(manifest, response->subtitles);
                }
                
                (*pending)--;
                if (*pending == 0) {
                    done_callback();
                }
            });
    }
}

std::vector<std::pair<Manifest, CatalogDefinition>> AddonService::get_searchable_catalogs() const {
    std::vector<std::pair<Manifest, CatalogDefinition>> result;
    
    for (const auto& addon : installed_addons_) {
        if (!addon.enabled || !addon.manifest.has_resource("catalog")) {
            continue;
        }
        
        for (const auto& catalog : addon.manifest.catalogs) {
            // Check if catalog supports search in extra_supported
            bool supports_search = false;
            for (const auto& extra : catalog.extra_supported) {
                if (extra == "search") {
                    supports_search = true;
                    break;
                }
            }
            
            if (supports_search) {
                result.emplace_back(addon.manifest, catalog);
            }
        }
    }
    
    return result;
}

void AddonService::search(const std::string& query,
                          std::function<void(const Manifest&, const CatalogDefinition&, const std::vector<MetaPreview>&)> callback,
                          std::function<void()> done_callback) {
    auto catalogs = get_searchable_catalogs();
    
    g_print("[SEARCH] Searching for '%s' across %zu catalogs\n", query.c_str(), catalogs.size());
    
    if (catalogs.empty()) {
        g_print("[SEARCH] No searchable catalogs found\n");
        done_callback();
        return;
    }
    
    for (const auto& [manifest, catalog] : catalogs) {
        g_print("[SEARCH] Searchable catalog: %s/%s from addon %s\n", 
                catalog.type.c_str(), catalog.id.c_str(), manifest.name.c_str());
    }
    
    auto pending = std::make_shared<int>(static_cast<int>(catalogs.size()));
    
    for (const auto& [manifest, catalog] : catalogs) {
        ExtraArgs extra;
        extra.search = query;
        
        client_->fetch_catalog(manifest, catalog.type, catalog.id, extra,
            [callback, done_callback, pending, manifest, catalog, query]
            (std::optional<CatalogResponse> response, const std::string& error) {
                if (!error.empty()) {
                    g_print("[SEARCH] Error from %s/%s: %s\n", 
                            manifest.name.c_str(), catalog.id.c_str(), error.c_str());
                }
                
                if (response && !response->metas.empty()) {
                    g_print("[SEARCH] Got %zu results from %s/%s\n", 
                            response->metas.size(), manifest.name.c_str(), catalog.id.c_str());
                    callback(manifest, catalog, response->metas);
                } else {
                    g_print("[SEARCH] No results from %s/%s\n", 
                            manifest.name.c_str(), catalog.id.c_str());
                }
                
                (*pending)--;
                if (*pending == 0) {
                    done_callback();
                }
            });
    }
}

} // namespace Stremio
