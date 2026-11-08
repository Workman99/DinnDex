#pragma once
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace dinndex
{
  namespace fs = std::filesystem;
  using Json = nlohmann::json;

  bool supported_image(const fs::path &path);

  std::string sha256(const fs::path &path);

  void extract_metadata(const fs::path &path, Json &record);

  // No directory or file symlinks are followed. Callback receives one record at a time.
  Json scan(const fs::path &root, const std::function<void(const Json &)> &emit);
}
