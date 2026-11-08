#include "dinndex/scanner.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
  namespace fs = std::filesystem;

  // REQUIRES: message describes the expectation being checked.
  // MODIFIES: Nothing.
  // EFFECTS: Throws with message if the expectation is false.
  void check(bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  // REQUIRES: Nothing.
  // MODIFIES: Nothing.
  // EFFECTS: Verifies extension matching, case handling, and exclusions.
  void test_supported_extensions() {
    check(dinndex::supported_image("Dinner.JpG"), "case-insensitive JPEG");
    check(dinndex::supported_image("a.WEBP"), "WebP");
    check(!dinndex::supported_image("a.jpg.json"), "sidecar exclusion");
    check(!dinndex::supported_image("a.heic"), "explicit unsupported HEIC");
  }

  // REQUIRES: directory is an existing, writable temporary directory.
  // MODIFIES: Creates a synthetic file in directory.
  // EFFECTS: Writes a known hash input to a filename containing a quote.
  //          Returns the file path.
  fs::path make_hash_fixture(const fs::path &directory) {
    const auto path = directory / "quote\".png";
    std::ofstream output_file(path);
    output_file << "abc";
    return path;
  }

  // REQUIRES: path contains exactly the bytes "abc".
  // MODIFIES: Nothing; reads path.
  // EFFECTS: Verifies SHA-256 against an independently known digest.
  void test_sha256(const fs::path &path) {
    const std::string expected_hash =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    check(dinndex::sha256(path) == expected_hash, "SHA-256 known vector");
  }

  // REQUIRES: record describes the corrupt synthetic photo at expected_path.
  // MODIFIES: Nothing.
  // EFFECTS: Verifies JSON path escaping and corrupt-image status.
  void check_photo_record(const dinndex::Json &record,
                          const fs::path &expected_path) {
    const auto decoded = dinndex::Json::parse(record.dump());
    check(decoded["path"] == expected_path.string(), "JSON path escaping");
    check(record["status"] == "error", "corrupt photo reported");
  }

  // REQUIRES: root/nested contains only the synthetic photo at photo_path.
  // MODIFIES: Creates a directory symlink beneath root/nested; reads the tree.
  // EFFECTS: Verifies recursive scanning, symlink avoidance, and serialization.
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

// REQUIRES: A writable system temporary directory with symlink support.
// MODIFIES: Temporary test files, stdout, and stderr.
// EFFECTS: Runs core checks, cleans up fixtures, and returns pass/fail status.
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
