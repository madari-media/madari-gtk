#include "stremio_types.hpp"
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace Stremio {

// URL encode a string
static std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    
    for (char c : value) {
        // Keep alphanumeric and other accepted characters intact
        if (isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            // Any other characters are percent-encoded
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << int(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }
    
    return escaped.str();
}

bool Manifest::has_resource(const std::string& resource) const {
    for (const auto& res : resources) {
        if (res.name == resource) {
            return true;
        }
    }
    return false;
}

bool Manifest::has_type(const std::string& type) const {
    return std::find(types.begin(), types.end(), type) != types.end();
}

bool Manifest::matches_id_prefix(const std::string& id) const {
    if (id_prefixes.empty()) {
        return true; // No prefix restriction
    }
    for (const auto& prefix : id_prefixes) {
        if (id.rfind(prefix, 0) == 0) { // starts with
            return true;
        }
    }
    return false;
}

std::string ExtraArgs::to_path_segment() const {
    std::ostringstream oss;
    bool first = true;
    
    auto append_param = [&](const std::string& key, const std::string& value) {
        if (!first) oss << "&";
        first = false;
        // URL encode the value
        oss << key << "=" << url_encode(value);
    };
    
    if (search.has_value()) {
        append_param("search", search.value());
    }
    if (skip.has_value()) {
        append_param("skip", std::to_string(skip.value()));
    }
    if (genre.has_value()) {
        append_param("genre", genre.value());
    }
    for (const auto& [key, value] : other) {
        append_param(key, value);
    }
    
    return oss.str();
}

} // namespace Stremio
