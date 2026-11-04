#include "aiws/gui/context_widget.hpp"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QString>
#include <QStringList>
#include <QVBoxLayout>

namespace aiws::gui {

namespace {
// Token-budget slider settings required by the M3 spec.
constexpr int min_budget = 50;
constexpr int max_budget = 1000;
constexpr int budget_step = 50;
constexpr int default_budget = 300;
}

ContextWidget::ContextWidget(QWidget* parent)
    : QWidget(parent) {
    auto* group = new QGroupBox(tr("Context"), this);

    auto* label = new QLabel(tr("Token budget:"), group);

    context_budget_slider_ = new QSlider(Qt::Horizontal, group);
    context_budget_slider_->setRange(min_budget, max_budget);
    context_budget_slider_->setSingleStep(budget_step);
    context_budget_slider_->setPageStep(budget_step);
    context_budget_slider_->setTickInterval(budget_step);
    context_budget_slider_->setTickPosition(QSlider::TicksBelow);

    budget_value_label_ = new QLabel(group);
    budget_value_label_->setMinimumWidth(
        budget_value_label_->fontMetrics().horizontalAdvance("0000"));
    budget_value_label_->setAlignment(
        Qt::AlignRight | Qt::AlignVCenter);

    build_context_button_ = new QPushButton(tr("Build Context"), group);

    context_text_ = new QPlainTextEdit(group);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText(
        tr("Built context will appear here."));

    auto* row = new QHBoxLayout;
    row->addWidget(label);
    row->addWidget(context_budget_slider_, 1);
    row->addWidget(budget_value_label_);
    row->addWidget(build_context_button_);

    auto* group_layout = new QVBoxLayout(group);
    group_layout->addLayout(row);
    group_layout->addWidget(context_text_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group);

    connect(context_budget_slider_, &QSlider::valueChanged,
            this, &ContextWidget::budget_changed);

    connect(build_context_button_, &QPushButton::clicked,
            this, &ContextWidget::request_context);

    reset();
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    if (items.empty()) {
        context_text_->setPlainText(
            tr("No context items were returned."));
        return;
    }

    QStringList blocks;

    for (const ContextItem& item : items) {
        // Multi-argument arg() substitutes in one pass, so a '%' in the
        // path or text cannot be mistaken for a later placeholder.
        QString text = QStringLiteral(
            "Document: %1\n"
            "Chunk: %2\n"
            "Tokens: %3\n"
            "Score: %4\n"
            "Truncated: %5\n"
            "Text: %6")
            .arg(QString::fromStdString(item.document_id),
                 QString::number(item.chunk_sequence),
                 QString::number(item.token_count),
                 QString::number(item.score, 'f', 3),
                 item.truncated ? tr("Yes") : tr("No"),
                 QString::fromStdString(item.text));

        blocks.append(text);
    }

    context_text_->setPlainText(blocks.join("\n\n"));
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(default_budget);
    // setValue() emits nothing when the value is already 300,
    // so refresh the label directly.
    budget_changed(context_budget_slider_->value());

    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    // Dragging can land between steps; snap to the nearest multiple of 50.
    int budget = ((value + budget_step / 2) / budget_step) * budget_step;

    if (budget != value) {
        context_budget_slider_->setValue(budget);
        return;
    }

    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
