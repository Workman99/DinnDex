#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace dinndex {
  namespace fs = std::filesystem;
  using Json = nlohmann::json;

  // REQUIRES: Nothing.
  // MODIFIES: Nothing.
  // EFFECTS: Returns whether path has a JPG, JPEG, PNG, or WebP extension,
  //          ignoring case. Does not inspect file contents.
  bool supported_image(const fs::path &path);

  // REQUIRES: Nothing.
  // MODIFIES: Nothing outside local hashing state; reads the file at path.
  // EFFECTS: Returns its lowercase SHA-256 hash. Throws on read/hash failure.
  std::string sha256(const fs::path &path);

  // REQUIRES: record is an object with an errors array.
  // MODIFIES: record.
  // EFFECTS: Adds available image dimensions and raw EXIF date fields.
  //          On extraction failure, appends an error and sets status to error.
  void extract_metadata(const fs::path &path, Json &record);

  // REQUIRES: emit is callable for each generated record.
  // MODIFIES: Any output or state modified by emit; reads the source tree.
  // EFFECTS: Scans root without following file/directory symlinks below it.
  //          Emits photo/error records and returns a summary (not emitted).
  //          Continues after individual file/traversal errors. Throws for an
  //          invalid root and propagates callback exceptions.
  //          Traversal order is unspecified.
  Json scan(const fs::path &root,
            const std::function<void(const Json &)> &emit);
}  // namespace dinndex
