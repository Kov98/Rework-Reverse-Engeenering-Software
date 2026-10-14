#pragma once
#include "reworkbench/types.hpp"
#include <QMap>
#include <QList>

#include <fstream>
#include <memory>
#include <mutex>

namespace reworkbench {

class BinaryFile final {
public:
    explicit BinaryFile(QString path);
    [[nodiscard]] const QString& path() const { return path_; }
    [[nodiscard]] quint64 size() const { return size_; }
    // Reject reads beyond EOF or above16MiB. Empty reads at EOF are valid.
    [[nodiscard]] QByteArray read(quint64 offset, quint64 length) const;
private:
    QString path_;
    quint64 size_ = 0;
    mutable std::ifstream stream_;
    mutable std::mutex mutex_;
    mutable QMap<quint64, QByteArray> pages_;
    mutable QList<quint64> recent_pages_;
};

[[nodiscard]] Probe probe_binary(const std::shared_ptr<BinaryFile>& file);
[[nodiscard]] BinaryMetadata load_binary(const std::shared_ptr<BinaryFile>& file,
                                         const OpenOptions& options,
                                         std::stop_token stop = {});
[[nodiscard]] std::optional<quint64> file_offset(const BinaryMetadata& metadata,
                                                Address address,
                                                quint64 length = 1);
[[nodiscard]] bool executable_address(const BinaryMetadata& metadata, Address address);

} // namespace reworkbench
