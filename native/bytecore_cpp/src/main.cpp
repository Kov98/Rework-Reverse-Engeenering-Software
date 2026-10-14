#include "reworkbench/bytecore.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string_view>

namespace {

using reworkbench::bytecore::ScanOptions;
using reworkbench::bytecore::ScanResult;

struct Arguments {
    std::filesystem::path path;
    ScanOptions scan;
};

template <class Character>
bool equals_ascii(std::basic_string_view<Character> value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != static_cast<Character>(expected[index])) {
            return false;
        }
    }
    return true;
}

template <class Character>
std::uint64_t unsigned_decimal(std::basic_string_view<Character> value) {
    if (value.empty()) {
        throw std::invalid_argument("Numeric options require unsigned decimal integers.");
    }
    std::uint64_t result = 0;
    for (const auto character : value) {
        if (character < static_cast<Character>('0') || character > static_cast<Character>('9')) {
            throw std::invalid_argument("Numeric options require unsigned decimal integers.");
        }
        const auto digit = static_cast<std::uint64_t>(character - static_cast<Character>('0'));
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            throw std::overflow_error("Numeric option exceeds the 64-bit range.");
        }
        result = result * 10 + digit;
    }
    return result;
}

template <class Character>
Arguments parse_arguments(int argc, Character* argv[]) {
    if (argc < 2) {
        throw std::invalid_argument("Usage: bytecore_cpp <file> [--min-length N] [--max-strings N]");
    }
    Arguments result{std::filesystem::path(argv[1]), {}};
    bool has_min_length = false;
    bool has_max_strings = false;
    for (int index = 2; index < argc; ++index) {
        const auto flag = std::basic_string_view<Character>(argv[index]);
        const bool minimum = equals_ascii(flag, "--min-length");
        const bool maximum = equals_ascii(flag, "--max-strings");
        if ((!minimum && !maximum) || (minimum && has_min_length) || (maximum && has_max_strings)) {
            throw std::invalid_argument("Unknown or repeated command-line option.");
        }
        if (++index >= argc) {
            throw std::invalid_argument("A command-line option is missing its numeric value.");
        }
        const auto value = unsigned_decimal(std::basic_string_view<Character>(argv[index]));
        if (minimum) {
            if (value == 0) {
                throw std::invalid_argument("Minimum string length must be positive.");
            }
            has_min_length = true;
            result.scan.min_length = value;
        } else {
            if (value > std::numeric_limits<std::size_t>::max()) {
                throw std::overflow_error("String limit exceeds this platform's capacity.");
            }
            has_max_strings = true;
            result.scan.max_strings = static_cast<std::size_t>(value);
        }
    }
    return result;
}

void json_string(std::ostream& output, std::string_view value) {
    constexpr std::string_view digits = "0123456789abcdef";
    output.put('"');
    for (const unsigned char byte : value) {
        if (byte == '"' || byte == '\\') {
            output.put('\\');
            output.put(static_cast<char>(byte));
        } else if (byte >= 0x20 && byte <= 0x7e) {
            output.put(static_cast<char>(byte));
        } else {
            output << "\\u00" << digits[byte >> 4] << digits[byte & 0x0f];
        }
    }
    output.put('"');
}

void write_result(const ScanResult& result) {
    // JSON numeric syntax must remain independent of the user's locale.
    std::cout.imbue(std::locale::classic());
    std::cout << "{\"size\":" << result.size
              << ",\"entropy\":" << std::setprecision(std::numeric_limits<double>::max_digits10)
              << result.entropy << ",\"strings\":[";
    bool first = true;
    for (const auto& string : result.strings) {
        if (!first) {
            std::cout.put(',');
        }
        first = false;
        std::cout << "{\"offset\":" << string.offset << ",\"value\":";
        json_string(std::cout, string.value);
        std::cout.put('}');
    }
    std::cout << "],\"strings_truncated\":"
              << (result.strings_truncated ? "true" : "false") << "}\n";
    std::cout.flush();
    if (!std::cout) {
        throw std::runtime_error("Cannot write the scan result.");
    }
}

void report_error(std::string_view message) noexcept {
    try {
        std::cerr << "{\"error\":";
        json_string(std::cerr, message);
        std::cerr << "}\n";
        std::cerr.flush();
    } catch (...) {
        // Keep a reporting failure from replacing the original nonzero exit.
    }
}

template <class Character>
int run(int argc, Character* argv[]) {
    try {
        const auto arguments = parse_arguments(argc, argv);
        const auto result = reworkbench::bytecore::scan_file(arguments.path, arguments.scan);
        write_result(result);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        report_error(error.what());
    } catch (...) {
        report_error("Unexpected native scanner failure.");
    }
    return EXIT_FAILURE;
}

} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[]) {
    return run(argc, argv);
}
#else
int main(int argc, char* argv[]) {
    return run(argc, argv);
}
#endif
