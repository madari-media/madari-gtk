#pragma once

#include "stremio_types.hpp"
#include "stremio_client.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace Stremio {

/**
 * Installed addon info stored in settings
 */
struct InstalledAddon {
    Manifest manifest;
    bool enabled = true;
    int order = 0; // For ordering in the list
    std::string installed_at; // ISO 8601 date
};

/**
 * Service for managing Stremio addons
 * Handles addon installation, removal, and data persistence
 */
class AddonService {
public:
    using AddonsChangedCallback = std::function<void()>;
    using InstallCallback = std::function<void(bool success, const std::string& error)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    AddonService();
    ~AddonService();
    
    /**
     * Load installed addons from storage
     */
    void load();
    
    /**
     * Save installed addons to storage
     */
    void save();
    
    /**
     * Get all installed addons
     */
    const std::vector<InstalledAddon>& get_installed_addons() const;
    
    /**
     * Get enabled addons only
     */
    std::vector<InstalledAddon> get_enabled_addons() const;
    
    /**
     * Check if an addon is installed by ID
     */
    bool is_installed(const std::string& addon_id) const;
    
    /**
     * Install addon from URL
     * @param url The addon manifest URL
     * @param callback Called when installation completes
     */
    void install_addon(const std::string& url, InstallCallback callback);
    
    /**
     * Uninstall addon by ID
     */
    bool uninstall_addon(const std::string& addon_id);
    
    /**
     * Enable/disable addon
     */
    bool set_addon_enabled(const std::string& addon_id, bool enabled);
    
    /**
     * Reorder addon (move up or down)
     */
    bool move_addon(const std::string& addon_id, int direction);
    
    /**
     * Get addon by ID
     */
    std::optional<InstalledAddon> get_addon(const std::string& addon_id) const;
    
    /**
     * Subscribe to addon list changes
     */
    void on_addons_changed(AddonsChangedCallback callback);
    
    // ============ Addon API Methods ============
    
    /**
     * Get all catalogs from all enabled addons
     */
    std::vector<std::pair<Manifest, CatalogDefinition>> get_all_catalogs() const;
    
    /**
     * Get catalogs filtered by type
     */
    std::vector<std::pair<Manifest, CatalogDefinition>> get_catalogs_by_type(const std::string& type) const;
    
    /**
     * Fetch catalog content
     */
    void fetch_catalog(const std::string& addon_id,
                       const std::string& type,
                       const std::string& catalog_id,
                       const ExtraArgs& extra,
                       Client::CatalogCallback callback);
    
    /**
     * Fetch metadata from first matching addon
     */
    void fetch_meta(const std::string& type,
                    const std::string& id,
                    Client::MetaCallback callback);
    
    /**
     * Fetch streams from all matching addons
     * @param type Content type
     * @param video_id Video ID
     * @param callback Called for each addon's streams
     * @param done_callback Called when all addons have responded
     */
    void fetch_all_streams(const std::string& type,
                           const std::string& video_id,
                           std::function<void(const Manifest& addon, const std::vector<Stream>& streams)> callback,
                           std::function<void()> done_callback);
    
    /**
     * Fetch subtitles from all matching addons
     */
    void fetch_all_subtitles(const std::string& type,
                             const std::string& id,
                             const std::string& video_id,
                             std::optional<int64_t> video_size,
                             std::function<void(const Manifest& addon, const std::vector<Subtitle>& subtitles)> callback,
                             std::function<void()> done_callback);
    
    /**
     * Search across all addons that support search
     * @param query Search query
     * @param callback Called for each addon's search results
     * @param done_callback Called when all addons have responded
     */
    void search(const std::string& query,
                std::function<void(const Manifest& addon, const CatalogDefinition& catalog, const std::vector<MetaPreview>& results)> callback,
                std::function<void()> done_callback);
    
    /**
     * Get catalogs that support search
     */
    std::vector<std::pair<Manifest, CatalogDefinition>> get_searchable_catalogs() const;

private:
    std::vector<InstalledAddon> installed_addons_;
    std::unique_ptr<Client> client_;
    std::vector<AddonsChangedCallback> change_callbacks_;
    std::string storage_path_;
    
    void notify_change();
    std::string get_storage_path();
    
    // Get addons that support a specific resource and type
    std::vector<InstalledAddon> get_addons_for_resource(const std::string& resource,
                                                         const std::string& type,
                                                         const std::string& id = "") const;
};

} // namespace Stremio
