#include "dinndex/scanner.hpp"
#include <openssl/evp.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
namespace dinndex {
std::string sha256(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read file");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("Cannot initialize SHA-256");
    std::array<char, 65536> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        if (EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<size_t>(input.gcount())) != 1)
            throw std::runtime_error("SHA-256 update failed");
    }
    if (!input.eof()) throw std::runtime_error("File read failed");
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &length) != 1)
        throw std::runtime_error("SHA-256 finalization failed");
    std::ostringstream result;
    for (unsigned int i = 0; i < length; ++i)
        result << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(digest[i]);
    return result.str();
}
}
