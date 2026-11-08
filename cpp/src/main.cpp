#include "dinndex/scanner.hpp"
#include <iostream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <memory>
namespace {
struct FileCloser {
    void operator()(FILE* file) const noexcept { std::fclose(file); }
};
}
int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage: dinndex scan SOURCE [--output MANIFEST.jsonl]\n"
                     "Scans JPG/JPEG/PNG/WebP without following symlinks. HEIC is unsupported.\n"
                     "JSON Lines on stdout or --output; diagnostics on stderr.\n"
                     "Exit: 0 success, 1 fatal error, 2 partial scan with failed files.\n";
        return 0;
    }
    if ((argc != 3 && argc != 5) || std::string(argv[1]) != "scan" ||
        (argc == 5 && std::string(argv[3]) != "--output")) {
        std::cerr << "Usage: dinndex scan SOURCE [--output MANIFEST.jsonl]\n"; return 1;
    }
    try {
        std::unique_ptr<FILE, FileCloser> file;
        if (argc == 5) {
            // O_EXCL also rejects symlinks and prevents overwrite races.
            int fd = ::open(argv[4], O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0) throw std::runtime_error("Cannot create output (already exists or parent is not writable)");
            file.reset(::fdopen(fd, "w"));
            if (!file) { ::close(fd); throw std::runtime_error("Cannot open manifest stream"); }
        }
        size_t processed = 0;
        auto emit = [&](const dinndex::Json& r) {
            const auto line = r.dump(-1, ' ', false, dinndex::Json::error_handler_t::replace) + "\n";
            if (file) {
                if (std::fwrite(line.data(), 1, line.size(), file.get()) != line.size())
                    throw std::runtime_error("Cannot write manifest");
            } else {
                std::cout << line;
                if (!std::cout) throw std::runtime_error("Cannot write manifest");
            }
            if (r["type"] == "photo" && ++processed % 100 == 0)
                std::cerr << "Scanned " << processed << " images...\n";
        };
        auto summary = dinndex::scan(argv[2], emit);
        emit(summary);
        if (file) {
            if (std::fflush(file.get()) != 0) throw std::runtime_error("Cannot flush manifest");
        } else {
            std::cout.flush();
            if (!std::cout) throw std::runtime_error("Cannot flush manifest");
        }
        std::cerr << summary.dump() << '\n';
        return summary["failed"].get<int>() ? 2 : 0;
    } catch (const std::exception& e) { std::cerr << "dinndex: " << e.what() << '\n'; return 1; }
}
