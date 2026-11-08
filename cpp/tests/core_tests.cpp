#include "dinndex/scanner.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
  namespace fs = std::filesystem;

  // REQUIRES: message is a non-null, null-terminated string.
  // EFFECTS: Returns normally if condition is true; otherwise throws
  //          std::runtime_error containing message.
  void check(bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  // EFFECTS: Checks supported extensions, case handling, and exclusions.
  //          Throws if a check fails.
  void test_supported_extensions() {
    check(dinndex::supported_image("Dinner.JpG"), "case-insensitive JPEG");
    check(dinndex::supported_image("a.WEBP"), "WebP");
    check(!dinndex::supported_image("a.jpg.json"), "sidecar exclusion");
    check(!dinndex::supported_image("a.heic"), "explicit unsupported HEIC");
  }

  // REQUIRES: directory is a test directory with no entry named quote".png.
  // MODIFIES: The file directory/quote".png.
  // EFFECTS: Attempts to create that file with bytes "abc" and returns its
  //          path. Does not report file creation or writing failures.
  fs::path make_hash_fixture(const fs::path &directory) {
    const auto path = directory / "quote\".png";
    std::ofstream output_file(path);
    output_file << "abc";
    return path;
  }

  // EFFECTS: Checks that the file at path has the known SHA-256 hash of "abc".
  //          Throws if the contents differ or the file cannot be hashed.
  void test_sha256(const fs::path &path) {
    const std::string expected_hash =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    check(dinndex::sha256(path) == expected_hash, "SHA-256 known vector");
  }

  // REQUIRES: record is a JSON object with path and status fields.
  // EFFECTS: Checks that a JSON serialization round trip preserves the
  //          expected_path and that status is "error". Throws if a check fails.
  void check_photo_record(const dinndex::Json &record,
                          const fs::path &expected_path) {
    const auto decoded = dinndex::Json::parse(record.dump());
    check(decoded["path"] == expected_path.string(), "JSON path escaping");
    check(record["status"] == "error", "corrupt photo reported");
  }

  // REQUIRES: root/nested contains only the corrupt synthetic photo at
  //           photo_path and supports creation of a directory symlink.
  // MODIFIES: The filesystem entry root/nested/loop.
  // EFFECTS: Creates a symlink to root and checks recursive scanning, symlink
  //          avoidance, and serialization. Throws if setup or a check fails.
  void test_recursive_scan(const fs::path &root, const fs::path &photo_path) {
    fs::create_directory_symlink(root, root / "nested" / "loop");
    int photo_count = 0;
    const auto summary = dinndex::scan(root, [&](const dinndex::Json &record) {
      check_photo_record(record, photo_path);
      ++photo_count;
    });
    check(photo_count == 1 && summary["failed"] == 1,
          "recursive scan and corrupt file handling");
  }
}  // namespace

// MODIFIES: Temporary test files, std::cout, std::cerr.
// EFFECTS: Runs the core checks and removes fixtures after checks finish or
//          throw. Returns 0 if all checks pass, otherwise reports the caught
//          error and returns 1. Setup or cleanup failures may propagate.
int main() {
  const auto unique_suffix = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto root =
      fs::temp_directory_path() / ("dinndex-test-" + unique_suffix);
  fs::create_directories(root / "nested");
  try {
    test_supported_extensions();
    const auto photo_path = make_hash_fixture(root / "nested");
    test_sha256(photo_path);
    test_recursive_scan(root, photo_path);
    fs::remove_all(root);
    std::cout << "Core checks passed\n";
    return 0;
  } catch (const std::exception &error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
