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

    // REQUIRES: summary[field] is an integer counter that can be incremented
    //           without overflowing an int.
    // MODIFIES: summary.
    // EFFECTS: Increases the named counter by one.
    void increment_count(Json &summary, const char *field) {
      summary[field] = summary[field].get<int>() + 1;
    }

    // REQUIRES: emit is callable and state is a valid scan state.
    // MODIFIES: state.summary and entities modified by emit.
    // EFFECTS: Emits a traversal-error record for path and message, then
    //          increases the failure count. Propagates callback exceptions.
    void report_error(const fs::path &path, const std::string &message,
                      ScanState &state, const RecordCallback &emit) {
      emit(Json{{"type", "error"},
                {"schema_version", MANIFEST_SCHEMA_VERSION},
                {"path", path.string()},
                {"message", message}});
      increment_count(state.summary, "failed");
    }

    // EFFECTS: Returns an initial photo record identifying path, with unknown
    //          metadata, status "ok", and no errors. Does not access the file.
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

    // EFFECTS: Returns approximate whole seconds since the system-clock epoch
    //          corresponding to file_time.
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

    // REQUIRES: record has the structure returned by make_photo_record.
    // MODIFIES: record.
    // EFFECTS: Adds file size, modification time, SHA-256, and available image
    //          metadata. Records metadata-extraction failures in record.
    //          Throws on file/hash failure or a detected size/mtime change;
    //          fields already added remain. Does not alter the file.
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

    // REQUIRES: record is an object with a SHA-256 string; state is a valid
    //           scan state.
    // MODIFIES: record, state.
    // EFFECTS: Remembers path as the first source if the hash is new.
    //          Otherwise,
    //          sets duplicate_of to the first source and increments duplicates.
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

    // REQUIRES: emit is callable and state is a valid scan state.
    // MODIFIES: state and entities modified by emit.
    // EFFECTS: Emits one photo record for path, including metadata/hash or
    //          file errors. Updates image, failure, and duplicate counts.
    //          Propagates callback exceptions. Does not alter the file.
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

    // REQUIRES: emit is callable and state is a valid scan state.
    // MODIFIES: state and entities modified by emit.
    // EFFECTS: Visits directory and descendants, emitting photo/error records.
    //          Skips symlinks/nonimages and updates summary counts.
    //          Reports access failures without abandoning sibling directories.
    //          Propagates callback exceptions. Does not alter source files.
    void walk_directory(const fs::path &directory, ScanState &state,
                        const RecordCallback &emit);

    // REQUIRES: emit is callable and state is a valid scan state.
    // MODIFIES: state and entities modified by emit.
    // EFFECTS: Processes a supported file or scans a directory recursively.
    //          Counts symlinks/nonimages as skipped and reports status errors.
    //          Propagates callback exceptions. Does not alter source files.
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
