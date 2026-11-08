#include "dinndex/scanner.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <unordered_map>

namespace dinndex {
  namespace {
    constexpr int MANIFEST_SCHEMA_VERSION = 1;
    using RecordCallback = std::function<void(const Json &)>;

    // Per-scan data shared by traversal and photo processing helpers.
    struct ScanState {
      Json summary = {
          {"type", "summary"}, {"schema_version", MANIFEST_SCHEMA_VERSION},
          {"images", 0},       {"duplicates", 0},
          {"failed", 0},       {"skipped", 0}};
      std::unordered_map<std::string, std::string> first_path_by_hash;
    };

    // REQUIRES: summary[field] is an integer counter.
    // MODIFIES: summary.
    // EFFECTS: Increments the named counter by one.
    void increment_count(Json &summary, const char *field) {
      summary[field] = summary[field].get<int>() + 1;
    }

    // REQUIRES: emit is callable.
    // MODIFIES: state.summary and any state modified by emit.
    // EFFECTS: Emits a traversal error and counts it as a failure.
    void report_error(const fs::path &path, const std::string &message,
                      ScanState &state, const RecordCallback &emit) {
      emit(Json{{"type", "error"},
                {"schema_version", MANIFEST_SCHEMA_VERSION},
                {"path", path.string()},
                {"message", message}});
      increment_count(state.summary, "failed");
    }

    // EFFECTS: Returns a photo record with initially unknown metadata.
    Json make_photo_record(const fs::path &path) {
      return {{"type", "photo"},
              {"schema_version", MANIFEST_SCHEMA_VERSION},
              {"path", path.string()},
              {"filename", path.filename().string()},
              {"extension", path.extension().string()},
              {"size_bytes", nullptr},
              {"sha256", nullptr},
              {"width", nullptr},
              {"height", nullptr},
              {"mtime_epoch", nullptr},
              {"embedded_dates", Json::object()},
              {"duplicate_of", nullptr},
              {"status", "ok"},
              {"errors", Json::array()}};
    }

    // EFFECTS: Converts file-clock time to approximate system epoch seconds.
    auto epoch_seconds(fs::file_time_type file_time) {
      using SystemClock = std::chrono::system_clock;
      const auto system_time =
          std::chrono::time_point_cast<SystemClock::duration>(
              file_time - fs::file_time_type::clock::now() +
              SystemClock::now());
      return std::chrono::duration_cast<std::chrono::seconds>(
                 system_time.time_since_epoch())
          .count();
    }

    // REQUIRES: record was created by make_photo_record.
    // MODIFIES: record; reads the file at path.
    // EFFECTS: Adds file/image metadata and hash. Throws on file access failure
    //          or a size/mtime change during processing.
    void populate_photo_record(const fs::path &path, Json &record) {
      const auto size = fs::file_size(path);
      const auto modified_before = fs::last_write_time(path);
      record["size_bytes"] = size;
      record["mtime_epoch"] = epoch_seconds(modified_before);
      record["sha256"] = sha256(path);
      extract_metadata(path, record);
      if (fs::file_size(path) != size ||
          fs::last_write_time(path) != modified_before) {
        throw std::runtime_error("File changed during scanning; retry import");
      }
    }

    // REQUIRES: record contains a successfully computed SHA-256 string.
    // MODIFIES: record and state.
    // EFFECTS: Remembers the first path for a hash or marks/counts a duplicate.
    void record_duplicate(const fs::path &path, Json &record,
                          ScanState &state) {
      const auto hash = record["sha256"].get<std::string>();
      const auto found = state.first_path_by_hash.find(hash);
      if (found == state.first_path_by_hash.end()) {
        state.first_path_by_hash.emplace(hash, path.string());
        return;
      }
      record["duplicate_of"] = found->second;
      increment_count(state.summary, "duplicates");
    }

    // REQUIRES: emit is callable.
    // MODIFIES: state and any state modified by emit; reads path.
    // EFFECTS: Processes and emits one photo, recording file errors.
    //          Propagates callback exceptions separately from file errors.
    void process_photo(const fs::path &path, ScanState &state,
                       const RecordCallback &emit) {
      Json record = make_photo_record(path);
      try {
        populate_photo_record(path, record);
        if (record["status"] == "ok") {
          record_duplicate(path, record, state);
        }
      } catch (const std::exception &error) {
        record["status"] = "error";
        record["errors"].push_back(error.what());
      }
      increment_count(state.summary, "images");
      if (record["status"] == "error") {
        increment_count(state.summary, "failed");
      }
      emit(record);
    }

    // REQUIRES: emit is callable.
    // MODIFIES: state and any state modified by emit; reads directory.
    // EFFECTS: Recursively visits entries. Reports directory access failures
    //          without terminating the scan of sibling directories.
    void walk_directory(const fs::path &directory, ScanState &state,
                        const RecordCallback &emit);

    // REQUIRES: emit is callable.
    // MODIFIES: state and callback output; reads entry and its descendants.
    // EFFECTS: Skips symlinks/nonimages, descends into directories, and
    //          processes supported files. Reports entry-status errors.
    void process_entry(const fs::directory_entry &entry, ScanState &state,
                       const RecordCallback &emit) {
      std::error_code error_code;
      const auto status = entry.symlink_status(error_code);
      if (error_code) {
        report_error(entry.path(), error_code.message(), state, emit);
        return;
      }
      if (fs::is_symlink(status)) {
        increment_count(state.summary, "skipped");
        return;
      }
      if (fs::is_directory(status)) {
        walk_directory(entry.path(), state, emit);
        return;
      }
      if (fs::is_regular_file(status) && supported_image(entry.path())) {
        process_photo(entry.path(), state, emit);
        return;
      }
      increment_count(state.summary, "skipped");
    }

    void walk_directory(const fs::path &directory, ScanState &state,
                        const RecordCallback &emit) {
      std::error_code error_code;
      fs::directory_iterator entry_iterator(directory, error_code);
      const fs::directory_iterator end;
      if (error_code) {
        report_error(directory, error_code.message(), state, emit);
        return;
      }
      while (entry_iterator != end) {
        process_entry(*entry_iterator, state, emit);
        entry_iterator.increment(error_code);
        if (error_code) {
          report_error(directory, error_code.message(), state, emit);
          break;
        }
      }
    }
  }  // namespace

  bool supported_image(const fs::path &path) {
    auto extension = path.extension().string();
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return std::tolower(character); });
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
           extension == ".webp";
  }

  Json scan(const fs::path &input,
            const std::function<void(const Json &)> &emit) {
    const auto root = fs::canonical(input);
    if (!fs::is_directory(root)) {
      throw std::runtime_error("Source must be a directory");
    }
    ScanState state;
    walk_directory(root, state, emit);
    return state.summary;
  }
}  // namespace dinndex
