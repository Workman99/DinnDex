#include "dinndex/scanner.hpp"

#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  constexpr int SUCCESS = 0;
  constexpr int FATAL_ERROR = 1;
  constexpr int PARTIAL_SCAN = 2;
  constexpr int SCAN_ARGUMENT_COUNT = 3;
  constexpr int OUTPUT_ARGUMENT_COUNT = 5;
  constexpr std::size_t PROGRESS_INTERVAL = 100;
  constexpr const char *USAGE =
      "Usage: dinndex scan SOURCE [--output MANIFEST.jsonl]\n";

  struct FileCloser {
    // REQUIRES: file is a valid open stream.
    // MODIFIES: The stream's resources.
    // EFFECTS: Closes file when its owning smart pointer is destroyed.
    void operator()(FILE *file) const noexcept {
      std::fclose(file);
    }
  };
  using OutputFile = std::unique_ptr<FILE, FileCloser>;

  struct ScanOptions {
    dinndex::fs::path source;
    std::optional<dinndex::fs::path> output;
  };

  // REQUIRES: arguments contains argument_count command-line arguments.
  // MODIFIES: Nothing.
  // EFFECTS: Returns parsed scan options or throws on invalid arguments.
  ScanOptions parse_options(int argument_count, char **arguments) {
    if ((argument_count != SCAN_ARGUMENT_COUNT &&
         argument_count != OUTPUT_ARGUMENT_COUNT) ||
        std::string(arguments[1]) != "scan" ||
        (argument_count == OUTPUT_ARGUMENT_COUNT &&
         std::string(arguments[3]) != "--output")) {
      throw std::invalid_argument(USAGE);
    }
    ScanOptions options;
    options.source = arguments[2];
    if (argument_count == OUTPUT_ARGUMENT_COUNT) {
      options.output = arguments[4];
    }
    return options;
  }

  // REQUIRES: Nothing.
  // MODIFIES: Creates a new file if path is provided.
  // EFFECTS: Creates exclusive output, or uses stdout. Throws on failure.
  OutputFile open_output(const std::optional<dinndex::fs::path> &path) {
    if (!path) {
      return OutputFile{};
    }
    // O_EXCL also rejects symlinks and prevents overwrite races.
    const int descriptor =
        ::open(path->c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
      throw std::runtime_error(
          "Cannot create output (already exists or parent is not writable)");
    }
    OutputFile file(::fdopen(descriptor, "w"));
    if (!file) {
      ::close(descriptor);
      throw std::runtime_error("Cannot open manifest stream");
    }
    return file;
  }

  // REQUIRES: output is null (stdout) or a valid writable stream.
  // MODIFIES: output or stdout.
  // EFFECTS: Writes one JSON line. Throws on output failure.
  void write_record(const dinndex::Json &record, FILE *output) {
    const auto line =
        record.dump(-1, ' ', false, dinndex::Json::error_handler_t::replace) +
        "\n";
    if (output) {
      if (std::fwrite(line.data(), 1, line.size(), output) != line.size()) {
        throw std::runtime_error("Cannot write manifest");
      }
      return;
    }
    std::cout << line;
    if (!std::cout) {
      throw std::runtime_error("Cannot write manifest");
    }
  }

  // REQUIRES: output is null (stdout) or a valid writable stream.
  // MODIFIES: output or stdout.
  // EFFECTS: Flushes pending output. Throws on failure.
  void flush_output(FILE *output) {
    if (output) {
      if (std::fflush(output) != 0) {
        throw std::runtime_error("Cannot flush manifest");
      }
      return;
    }
    std::cout.flush();
    if (!std::cout) {
      throw std::runtime_error("Cannot flush manifest");
    }
  }

  // REQUIRES: Nothing; invalid paths are reported by exceptions.
  // MODIFIES: Output file/stdout and stderr; reads the source tree.
  // EFFECTS: Runs a scan and returns its status. Throws on fatal errors.
  int run_scan(const ScanOptions &options) {
    auto output = open_output(options.output);
    std::size_t processed_photo_count = 0;
    auto emit = [&](const dinndex::Json &record) {
      write_record(record, output.get());
      if (record["type"] == "photo" &&
          ++processed_photo_count % PROGRESS_INTERVAL == 0) {
        std::cerr << "Scanned " << processed_photo_count << " images...\n";
      }
    };
    const auto summary = dinndex::scan(options.source, emit);
    emit(summary);
    flush_output(output.get());
    std::cerr << summary.dump() << '\n';
    return summary["failed"].get<int>() ? PARTIAL_SCAN : SUCCESS;
  }
}  // namespace

// REQUIRES: argc and argv have the usual C++ command-line representation.
// MODIFIES: stdout, stderr, and the requested output file; reads source files.
// EFFECTS: Displays help or runs a scan. Returns a status for fatal errors.
int main(int argc, char **argv) {
  if (argc == 2 &&
      (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
    std::cout << USAGE
              << "Scans JPG/JPEG/PNG/WebP without following symlinks. "
                 "HEIC is unsupported.\n"
              << "JSON Lines on stdout or --output; diagnostics on stderr.\n"
              << "Exit: 0 success, 1 fatal error, "
                 "2 partial scan with failed files.\n";
    return SUCCESS;
  }
  try {
    return run_scan(parse_options(argc, argv));
  } catch (const std::invalid_argument &error) {
    std::cerr << error.what();
    return FATAL_ERROR;
  } catch (const std::exception &error) {
    std::cerr << "dinndex: " << error.what() << '\n';
    return FATAL_ERROR;
  }
}
