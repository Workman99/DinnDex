#include "dinndex/scanner.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    namespace fs = std::filesystem;
    auto temp = fs::temp_directory_path() / ("dinndex-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp / "nested");
    try {
        check(dinndex::supported_image("Dinner.JpG"), "case-insensitive JPEG");
        check(dinndex::supported_image("a.WEBP"), "WebP");
        check(!dinndex::supported_image("a.jpg.json"), "sidecar exclusion");
        check(!dinndex::supported_image("a.heic"), "explicit unsupported HEIC");
        { std::ofstream f(temp / "nested" / "quote\".png"); f << "abc"; }
        check(dinndex::sha256(temp / "nested" / "quote\".png") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 known vector");
        fs::create_directory_symlink(temp, temp / "nested" / "loop");
        int photos = 0;
        auto summary = dinndex::scan(temp, [&](const dinndex::Json& r) {
            auto decoded = dinndex::Json::parse(r.dump());
            check(decoded["path"] == (temp / "nested" / "quote\".png").string(), "JSON path escaping");
            check(r["status"] == "error", "corrupt photo reported"); ++photos;
        });
        check(photos == 1 && summary["failed"] == 1, "recursive scan and corrupt file handling");
        fs::remove_all(temp);
        std::cout << "Core checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        fs::remove_all(temp); std::cerr << e.what() << '\n'; return 1;
    }
}
