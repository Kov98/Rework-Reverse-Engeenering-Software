#pragma once
#include "reworkbench/types.hpp"
#include <QMap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTextCursor>

class QLabel;
class QLineEdit;
class QShortcut;
class QToolButton;

namespace reworkbench::ui {
class CodeEditor final : public QPlainTextEdit {
public:
    explicit CodeEditor(QWidget* parent = nullptr);
    void set_source_addresses(const QMap<int, reworkbench::Address>& addresses);
    void select_address(reworkbench::Address address);
    std::optional<reworkbench::Address> current_address() const;
    std::function<void(reworkbench::Address)> address_selected;
    int gutter_width() const;
    void paint_gutter(QPaintEvent* event);
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
private:
    void update_margin();
    void update_current_line();
    void observe_document();
    void create_find_overlay();
    void position_find_overlay();
    void open_find();
    void close_find();
    void refresh_find(bool select_match);
    void find_next(bool backwards);
    void update_find_count();
    QWidget* gutter_;
    QWidget* find_overlay_ = nullptr;
    QLineEdit* find_query_ = nullptr;
    QLabel* find_count_ = nullptr;
    QToolButton* find_case_ = nullptr;
    QToolButton* find_previous_ = nullptr;
    QToolButton* find_next_ = nullptr;
    QShortcut* close_find_shortcut_ = nullptr;
    QVector<QTextCursor> find_matches_;
    bool find_matches_capped_ = false;
    bool updating_find_ = false;
    QMap<int, reworkbench::Address> source_addresses_;
    QPointer<QTextDocument> observed_document_;
    QMetaObject::Connection source_document_connection_;
    int source_revision_ = -1;
    bool selecting_address_ = false;
};
}
