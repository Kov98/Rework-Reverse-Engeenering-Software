#include "reworkbench/bytecore.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

namespace reworkbench::bytecore {
namespace {

void check_cancelled(std::stop_token stop) {
    if (stop.stop_requested()) {
        throw ScanCancelled{};
    }
}

class StringCollector {
public:
    StringCollector(ScanResult& result, const ScanOptions& options)
        : result_(result), options_(options) {}

    void accept(unsigned char byte, std::uint64_t offset) {
        if (byte >= 0x20 && byte <= 0x7e) {
            if (run_length_ == 0) {
                run_start_ = offset;
            }
            ++run_length_;
            if (result_.strings.size() < options_.max_strings &&
                prefix_.size() < string_preview_limit) {
                prefix_.push_back(static_cast<char>(byte));
            }
        } else {
            finish();
        }
    }

    void finish() {
        if (run_length_ >= options_.min_length) {
            if (result_.strings.size() < options_.max_strings) {
                result_.strings.push_back({run_start_, prefix_});
                if (run_length_ > string_preview_limit) {
                    result_.strings_truncated = true;
                }
            } else {
                result_.strings_truncated = true;
            }
        }
        // Keep the small preview allocation for the next run; the complete
        // input is never buffered. A read boundary does not finish a run.
        prefix_.clear();
        run_length_ = 0;
    }

private:
    ScanResult& result_;
    const ScanOptions& options_;
    std::uint64_t run_start_ = 0;
    std::uint64_t run_length_ = 0;
    std::string prefix_;
};

} // namespace

ScanResult scan_file(const std::filesystem::path& path,
                     const ScanOptions& options,
                     std::stop_token stop) {
    if (options.min_length == 0) {
        throw std::invalid_argument("Minimum string length must be positive.");
    }
    check_cancelled(stop);

    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::runtime_error("Input must be a readable regular file.");
    }
    // filesystem::path preserves native UTF-16 paths on Windows.
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open input file for reading.");
    }

    ScanResult result;
    std::array<std::uint64_t, 256> histogram{};
    std::array<char, read_chunk_size> buffer{};
    StringCollector strings(result, options);

    for (;;) {
        check_cancelled(stop);
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (input.bad() || (input.fail() && !input.eof())) {
            throw std::runtime_error("Cannot read input file completely.");
        }
        if (count < 0 || static_cast<std::uint64_t>(count) >
                             std::numeric_limits<std::uint64_t>::max() - result.size) {
            throw std::overflow_error("Input exceeds the 64-bit offset range.");
        }
        for (std::streamsize index = 0; index < count; ++index) {
            const auto byte = static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)]);
            ++histogram[byte];
            strings.accept(byte, result.size);
            ++result.size;
        }
        if (input.eof()) {
            break;
        }
    }

    check_cancelled(stop);
    strings.finish();
    if (result.size != 0) {
        long double entropy = 0.0L;
        for (const auto count : histogram) {
            if (count == 0) {
                continue;
            }
            const auto probability = static_cast<long double>(count) /
                                     static_cast<long double>(result.size);
            entropy -= probability * std::log2(probability);
        }
        result.entropy = std::clamp(static_cast<double>(entropy), 0.0, 8.0);
        if (!std::isfinite(result.entropy)) {
            throw std::runtime_error("Cannot calculate finite input entropy.");
        }
    }
    check_cancelled(stop);
    return result;
}

} // namespace reworkbench::bytecore
