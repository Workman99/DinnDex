#include "dinndex/scanner.hpp"

#include <exiv2/exiv2.hpp>
#include <stdexcept>

namespace dinndex {
  void extract_metadata(const fs::path &path, Json &record) {
    try {
      auto image = Exiv2::ImageFactory::open(path.string());
      if (!image.get()) {
        throw std::runtime_error("Unsupported image content");
      }
      image->readMetadata();
      if (image->pixelWidth() > 0) {
        record["width"] = image->pixelWidth();
      }
      if (image->pixelHeight() > 0) {
        record["height"] = image->pixelHeight();
      }
      auto &exif = image->exifData();
      for (const auto *key :
           {"Exif.Photo.DateTimeOriginal", "Exif.Photo.DateTimeDigitized",
            "Exif.Image.DateTime", "Exif.Photo.OffsetTimeOriginal",
            "Exif.Photo.OffsetTimeDigitized", "Exif.Photo.OffsetTime"}) {
        const auto entry = exif.findKey(Exiv2::ExifKey(key));
        if (entry != exif.end()) {
          record["embedded_dates"][key] = entry->toString();
        }
      }
    } catch (const std::exception &error) {
      record["errors"].push_back(std::string("metadata: ") + error.what());
      record["status"] = "error";
    }
  }
}  // namespace dinndex
