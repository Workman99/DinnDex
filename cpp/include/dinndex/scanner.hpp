#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace dinndex {
  namespace fs = std::filesystem;
  using Json = nlohmann::json;

  // EFFECTS: Returns true if path has a JPG, JPEG, PNG, or WebP extension,
  //          ignoring case; returns false otherwise. Does not access the file.
  bool supported_image(const fs::path &path);

  // EFFECTS: Returns the SHA-256 hash of the file at path as lowercase hex.
  //          Throws if the file cannot be read or hashing fails.
  std::string sha256(const fs::path &path);

  // REQUIRES: record is a JSON object with an errors array and an
  //           embedded_dates object (or no embedded_dates field).
  // MODIFIES: record.
  // EFFECTS: Adds available image dimensions and raw EXIF dates and offsets.
  //          Missing metadata is left unchanged. If extraction fails, appends
  //          an error and sets status to "error"; fields already added remain.
  void extract_metadata(const fs::path &path, Json &record);

  // REQUIRES: emit is a callable that accepts a JSON record.
  // MODIFIES: Entities modified by emit.
  // EFFECTS: Emits a photo record for each supported regular file under root
  //          and error records for traversal failures. Skips symlinks below
  //          root and unsupported files. Continues after file/traversal errors.
  //          Returns summary counts without emitting the summary. Traversal
  //          order is unspecified. Throws for an inaccessible/nondirectory root
  //          and propagates callback exceptions. Does not alter source files.
  Json scan(const fs::path &root,
            const std::function<void(const Json &)> &emit);
}  // namespace dinndex
