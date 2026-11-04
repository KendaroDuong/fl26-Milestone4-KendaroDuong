#include "aiws/gui/workspace_widget.hpp"

#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace aiws::gui {

WorkspaceWidget::WorkspaceWidget(QWidget* parent)
    : QWidget(parent) {
    auto* workspace = new QGroupBox(tr("Workspace"), this);

    document_list_ = new QListWidget(workspace);
    document_list_->setSelectionMode(
        QAbstractItemView::SingleSelection);

    add_document_button_ =
        new QPushButton(tr("Add Document"), workspace);
    build_corpus_button_ =
        new QPushButton(tr("Build Corpus"), workspace);

    corpus_status_label_ = new QLabel(workspace);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(add_document_button_);
    buttons->addWidget(build_corpus_button_);

    auto* workspace_layout = new QVBoxLayout(workspace);
    workspace_layout->addWidget(document_list_);
    workspace_layout->addLayout(buttons);
    workspace_layout->addWidget(corpus_status_label_);

    auto* selected_document =
        new QGroupBox(tr("Selected Document"), this);

    document_title_label_ = new QLabel(selected_document);
    document_title_label_->setWordWrap(true);

    QFont font = document_title_label_->font();
    font.setBold(true);
    document_title_label_->setFont(font);

    document_text_ = new QPlainTextEdit(selected_document);
    document_text_->setReadOnly(true);
    document_text_->setPlaceholderText(
        tr("Select a document to view its text."));

    auto* document_layout = new QVBoxLayout(selected_document);
    document_layout->addWidget(document_title_label_);
    document_layout->addWidget(document_text_);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(workspace, 1);
    layout->addWidget(selected_document, 2);

    connect(add_document_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::add_document_requested);

    connect(build_corpus_button_, &QPushButton::clicked,
            this, &WorkspaceWidget::build_corpus_requested);

    connect(document_list_, &QListWidget::currentRowChanged,
            this, &WorkspaceWidget::document_selected);

    reset();
}

void WorkspaceWidget::add_document(const QString& title) {
    document_list_->addItem(title);
}

void WorkspaceWidget::select_document(int row) {
    document_list_->setCurrentRow(row);
}

void WorkspaceWidget::show_selected_document(
    const QString& title, const QString& text) {
    document_title_label_->setText(title);
    document_text_->setPlainText(text);
}

void WorkspaceWidget::clear_selected_document() {
    document_title_label_->clear();
    document_text_->clear();
}

void WorkspaceWidget::set_corpus_status(const QString& text) {
    corpus_status_label_->setText(text);
}

void WorkspaceWidget::set_build_corpus_enabled(bool enabled) {
    build_corpus_button_->setEnabled(enabled);
}

void WorkspaceWidget::reset() {
    document_list_->clear();
    clear_selected_document();
    set_corpus_status(tr("Corpus: Not built"));
    set_build_corpus_enabled(false);
}

}  // namespace aiws::gui
