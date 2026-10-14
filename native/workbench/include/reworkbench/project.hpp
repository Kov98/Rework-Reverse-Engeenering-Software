#pragma once
#include "reworkbench/types.hpp"

#include <QLockFile>
#include <memory>
#include <mutex>

namespace reworkbench {

class Project final {
public:
    static std::shared_ptr<Project> create(const QString& root);
    static std::shared_ptr<Project> open(const QString& root);
    ~Project();
    [[nodiscard]] const QString& root() const { return root_; }
    [[nodiscard]] Sample import_sample(const QString& source, std::stop_token stop = {});
    [[nodiscard]] QVector<AnalysisSummary> analyses() const;
    [[nodiscard]] QString save_new_analysis(const Sample& sample, const AnalysisSnapshot& snapshot,
                                            std::stop_token stop = {});
    [[nodiscard]] AnalysisSnapshot load_analysis(const QString& analysis_id,
                                                std::stop_token stop = {}) const;
    void rename_function(const QString& analysis_id, Address address, const QString& name);
    void set_note(const QString& analysis_id, const Note& note);
    [[nodiscard]] QVector<Note> notes(const QString& analysis_id) const;
    void export_report(const QString& analysis_id, const QString& destination,
                       std::stop_token stop = {}) const;
    void export_ir(const QString& analysis_id, Address address, const QString& text,
                   const QString& destination, std::stop_token stop = {}) const;
    [[nodiscard]] std::optional<QString> reusable_analysis(const Sample&, const OpenOptions&, std::stop_token = {}) const;
    [[nodiscard]] std::optional<QJsonObject> cached_result(const QString& key, std::stop_token = {}) const;
    void cache_result(const QString& key, const QJsonObject& result, std::stop_token = {});
    void export_artifact(const QString& analysis_id, const QByteArray& bytes, const QString& destination, std::stop_token = {}) const;
    [[nodiscard]] QStringList protected_sources(std::stop_token = {}) const;
    [[nodiscard]] Sample analysis_sample(const QString& analysis_id, std::stop_token = {}) const;
    void inherit_annotations(const QString& from, const QString& to, std::stop_token = {});
private:
    explicit Project(QString root);
    void initialize(bool create);
    QString root_;
    std::unique_ptr<QLockFile> lock_;
    mutable std::mutex mutex_;
};

} // namespace reworkbench
