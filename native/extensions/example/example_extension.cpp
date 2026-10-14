#include "reworkbench/plugin.hpp"
#include <stdexcept>

class ExampleExtension final : public QObject, public reworkbench::NativeExtension {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID REWORKBENCH_EXTENSION_IID FILE "example_extension.json")
    Q_INTERFACES(reworkbench::NativeExtension)
public:
    int api_version() const noexcept override { return reworkbench::extension_api_version; }
    QJsonObject invoke(const QString& command, const QJsonObject& arguments) override {
        if (command != QStringLiteral("describe")) throw std::runtime_error("Unknown extension command.");
        return {{"id", "org.reworkbench.example"}, {"api_version", api_version()},
            {"description", "Minimal independent native extension; no sample or process execution."},
            {"received_argument_fields", int(arguments.size())}};
    }
};

#include "example_extension.moc"
