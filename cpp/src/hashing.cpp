#include "dinndex/scanner.hpp"

#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>

namespace dinndex {
  namespace {
    constexpr std::size_t HASH_BUFFER_SIZE = 65536;
    constexpr int HEX_BYTE_WIDTH = 2;
    using DigestContext =
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

    // REQUIRES: input is open and context has been initialized for SHA-256.
    // MODIFIES: input and the digest context.
    // EFFECTS: Hashes the stream in chunks. Throws on read or hash failure.
    void hash_stream(std::istream &input, EVP_MD_CTX *context) {
      std::array<char, HASH_BUFFER_SIZE> buffer{};
      while (input) {
        input.read(buffer.data(), buffer.size());
        const auto bytes_read = static_cast<std::size_t>(input.gcount());
        if (EVP_DigestUpdate(context, buffer.data(), bytes_read) != 1) {
          throw std::runtime_error("SHA-256 update failed");
        }
      }
      // EOF is expected; another stream failure means the hash is incomplete.
      if (!input.eof()) {
        throw std::runtime_error("File read failed");
      }
    }

    // REQUIRES: context holds an initialized, updated SHA-256 computation.
    // MODIFIES: The digest context.
    // EFFECTS: Finalizes the hash and returns lowercase hex. Throws on failure.
    std::string finish_hash(EVP_MD_CTX *context) {
      unsigned char digest[EVP_MAX_MD_SIZE];
      unsigned int digest_length = 0;
      if (EVP_DigestFinal_ex(context, digest, &digest_length) != 1) {
        throw std::runtime_error("SHA-256 finalization failed");
      }
      std::ostringstream hex_digest;
      for (unsigned int i = 0; i < digest_length; ++i) {
        hex_digest << std::hex << std::setfill('0') << std::setw(HEX_BYTE_WIDTH)
                   << static_cast<int>(digest[i]);
      }
      return hex_digest.str();
    }
  }  // namespace

  std::string sha256(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
      throw std::runtime_error("Cannot read file");
    }
    DigestContext digest_context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!digest_context ||
        EVP_DigestInit_ex(digest_context.get(), EVP_sha256(), nullptr) != 1) {
      throw std::runtime_error("Cannot initialize SHA-256");
    }
    hash_stream(input, digest_context.get());
    return finish_hash(digest_context.get());
  }
}  // namespace dinndex
