#include "dinndex/scanner.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <unordered_map>
#include <stdexcept>
namespace dinndex {
bool supported_image(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp";
}
Json scan(const fs::path& input, const std::function<void(const Json&)>& emit) {
    const auto root = fs::canonical(input);
    if (!fs::is_directory(root)) throw std::runtime_error("Source must be a directory");
    Json summary = {{"type", "summary"}, {"schema_version", 1}, {"images", 0}, {"duplicates", 0}, {"failed", 0}, {"skipped", 0}};
    std::unordered_map<std::string, std::string> seen;
    auto report = [&](const fs::path& path, const std::string& message) {
        emit(Json{{"type", "error"}, {"schema_version", 1}, {"path", path.string()}, {"message", message}});
        summary["failed"] = summary["failed"].get<int>() + 1;
    };
    // Walk one directory at a time so a permission failure cannot terminate sibling scans.
    std::function<void(const fs::path&)> walk = [&](const fs::path& dir) {
        std::error_code ec;
        fs::directory_iterator it(dir, ec), end;
        if (ec) { report(dir, ec.message()); return; }
        while (it != end) {
            const auto path = it->path();
            auto status = it->symlink_status(ec);
            if (ec) report(path, ec.message());
            else if (fs::is_symlink(status)) summary["skipped"] = summary["skipped"].get<int>() + 1;
            else if (fs::is_directory(status)) walk(path);
            else if (fs::is_regular_file(status) && supported_image(path)) {
                Json r = {{"type", "photo"}, {"schema_version", 1}, {"path", path.string()},
                    {"filename", path.filename().string()}, {"extension", path.extension().string()},
                    {"size_bytes", nullptr}, {"sha256", nullptr}, {"width", nullptr}, {"height", nullptr},
                    {"mtime_epoch", nullptr}, {"embedded_dates", Json::object()},
                    {"duplicate_of", nullptr}, {"status", "ok"}, {"errors", Json::array()}};
                try {
                    const auto size = fs::file_size(path);
                    const auto before = fs::last_write_time(path);
                    r["size_bytes"] = size;
                    const auto system_time = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                        before - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
                    r["mtime_epoch"] = std::chrono::duration_cast<std::chrono::seconds>(system_time.time_since_epoch()).count();
                    r["sha256"] = sha256(path);
                    extract_metadata(path, r);
                    if (fs::file_size(path) != size || fs::last_write_time(path) != before)
                        throw std::runtime_error("File changed during scanning; retry import");
                    if (r["status"] == "ok") {
                        const auto hash = r["sha256"].get<std::string>();
                        const auto found = seen.find(hash);
                        if (found != seen.end()) {
                            r["duplicate_of"] = found->second;
                            summary["duplicates"] = summary["duplicates"].get<int>() + 1;
                        } else seen.emplace(hash, path.string());
                    }
                } catch (const std::exception& e) {
                    r["status"] = "error"; r["errors"].push_back(e.what());
                }
                summary["images"] = summary["images"].get<int>() + 1;
                if (r["status"] == "error") summary["failed"] = summary["failed"].get<int>() + 1;
                emit(r);
            } else summary["skipped"] = summary["skipped"].get<int>() + 1;
            it.increment(ec);
            if (ec) { report(dir, ec.message()); break; }
        }
    };
    walk(root);
    return summary;
}
}
