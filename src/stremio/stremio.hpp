#pragma once

/**
 * Stremio Addon SDK for Madari
 * 
 * This module provides a complete SDK for interacting with Stremio addons.
 * 
 * Components:
 * - stremio_types.hpp: Data structures for manifest, meta, streams, subtitles
 * - stremio_parser.hpp: JSON parser for Stremio responses
 * - stremio_client.hpp: HTTP client for addon API calls
 * - stremio_addon_service.hpp: Service for managing installed addons
 * 
 * Usage:
 * 
 * 1. Create an AddonService instance:
 *    Stremio::AddonService service;
 *    service.load();
 * 
 * 2. Install addons:
 *    service.install_addon("https://example.com/manifest.json", 
 *        [](bool success, const std::string& error) {
 *            if (success) g_print("Addon installed!\n");
 *        });
 * 
 * 3. Get catalogs:
 *    auto catalogs = service.get_all_catalogs();
 * 
 * 4. Fetch catalog content:
 *    service.fetch_catalog("addon.id", "movie", "top", {},
 *        [](auto response, auto error) {
 *            if (response) {
 *                for (const auto& meta : response->metas) {
 *                    g_print("Movie: %s\n", meta.name.c_str());
 *                }
 *            }
 *        });
 * 
 * 5. Fetch streams:
 *    service.fetch_all_streams("movie", "tt1234567",
 *        [](const auto& addon, const auto& streams) {
 *            for (const auto& stream : streams) {
 *                g_print("Stream from %s: %s\n", 
 *                    addon.name.c_str(),
 *                    stream.url.value_or("torrent").c_str());
 *            }
 *        },
 *        []() { g_print("Done fetching streams\n"); });
 */

#include "stremio_types.hpp"
#include "stremio_parser.hpp"
#include "stremio_client.hpp"
#include "stremio_addon_service.hpp"
