#include "aiws/gui/term_inspector_widget.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

TermInspectorWidget::TermInspectorWidget(QWidget* parent)
    : QWidget(parent) {
    auto* group = new QGroupBox(tr("Term Inspector"), this);

    term_edit_ = new QLineEdit(group);
    term_edit_->setPlaceholderText(tr("Enter a single term"));
    term_edit_->setClearButtonEnabled(true);

    inspect_button_ = new QPushButton(tr("Inspect"), group);

    document_frequency_value_ = new QLabel("-", group);
    chunk_frequency_value_ = new QLabel("-", group);

    auto* input_layout = new QHBoxLayout;
    input_layout->addWidget(term_edit_);
    input_layout->addWidget(inspect_button_);

    auto* form = new QFormLayout;
    form->addRow(tr("Document frequency (DF):"),
                 document_frequency_value_);
    form->addRow(tr("Selected-chunk TF:"),
                 chunk_frequency_value_);

    auto* group_layout = new QVBoxLayout(group);
    group_layout->addLayout(input_layout);
    group_layout->addLayout(form);
    group_layout->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group);

    connect(inspect_button_, &QPushButton::clicked,
            this, &TermInspectorWidget::request_inspection);

    reset();
}

void TermInspectorWidget::set_inspection_enabled(bool enabled) {
    inspect_button_->setEnabled(enabled);
}

void TermInspectorWidget::set_selected_chunk(const QString& chunk_id) {
    selected_chunk_id_ = chunk_id;
}

void TermInspectorWidget::set_statistics(
    std::size_t document_frequency,
    bool has_chunk_frequency,
    std::size_t chunk_frequency) {

    document_frequency_value_->setText(
        QString::number(document_frequency));

    if (has_chunk_frequency) {
        chunk_frequency_value_->setText(
            QString::number(chunk_frequency));
    } else {
        chunk_frequency_value_->setText("-");
    }
}

void TermInspectorWidget::clear_statistics() {
    document_frequency_value_->setText("-");
    chunk_frequency_value_->setText("-");
}

void TermInspectorWidget::reset() {
    term_edit_->clear();
    selected_chunk_id_.clear();
    clear_statistics();
    set_inspection_enabled(false);
}

void TermInspectorWidget::request_inspection() {
    QString term = term_edit_->text().trimmed();

    if (term.isEmpty()) {
        return;
    }

    emit term_inspection_requested(term, selected_chunk_id_);
}

}  // namespace aiws::gui
