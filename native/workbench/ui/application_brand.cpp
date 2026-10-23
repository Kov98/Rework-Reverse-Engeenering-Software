#include "application_brand.hpp"

#include <QResource>

// This global reference keeps RCC linked when reworkbench_ui is static.
static void initialize_brand_resource() {
    Q_INIT_RESOURCE(brand);
}

namespace reworkbench::ui {

QIcon application_icon() {
    static const bool initialized = [] {
        initialize_brand_resource();
        return true;
    }();
    Q_UNUSED(initialized);
    QIcon icon(QStringLiteral(":/brand/rework.ico"));
    icon.addFile(QStringLiteral(":/brand/rework-logo.png"));
    return icon;
}

} // namespace reworkbench::ui
