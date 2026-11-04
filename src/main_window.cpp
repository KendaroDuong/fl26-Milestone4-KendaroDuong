#include "aiws/gui/main_window.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "aiws/document.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"

namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(tr("AI Workspace"));

    auto* central = new QWidget(this);
    auto* header = new QLabel(tr("AI Workspace"), central);
    header->setStyleSheet(
        "QLabel { background-color: #2c4a7a; color: white;"
        " font-size: 16pt; font-weight: bold; padding: 8px; }");

    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);
    status_label_ = new QLabel(tr("Ready."), central);

    reset_button_ = new QPushButton(tr("Reset"), central);
    reset_button_->setStyleSheet(
        "QPushButton { background-color: #f0c040;"
        " color: black; padding: 6px 18px; }");

    exit_button_ = new QPushButton(tr("Exit"), central);
    exit_button_->setStyleSheet(
        "QPushButton { background-color: #c0392b;"
        " color: white; padding: 6px 18px; }");

    auto* analysis_layout = new QHBoxLayout;
    analysis_layout->addWidget(term_inspector_widget_, 1);
    analysis_layout->addWidget(context_widget_, 2);

    auto* bottom_layout = new QHBoxLayout;
    bottom_layout->addWidget(status_label_, 1);
    bottom_layout->addWidget(reset_button_);
    bottom_layout->addWidget(exit_button_);

    auto* layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(workspace_widget_, 3);
    layout->addWidget(search_widget_, 4);
    layout->addLayout(analysis_layout, 3);
    layout->addLayout(bottom_layout);

    setCentralWidget(central);
    resize(900, 900);

    connect(workspace_widget_, &WorkspaceWidget::add_document_requested,
            this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested,
            this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected,
            this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested,
            this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed,
            this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested,
            this, &MainWindow::source_document_requested);
    connect(search_widget_, &SearchWidget::selected_chunk_changed,
            term_inspector_widget_,
            &TermInspectorWidget::set_selected_chunk);

    connect(term_inspector_widget_,
            &TermInspectorWidget::term_inspection_requested,
            this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested,
            this, &MainWindow::build_context_requested);

    connect(reset_button_, &QPushButton::clicked,
            this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked,
            this, &MainWindow::exit_application);

    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

// TODO(M4): placeholder so the project links; implement per spec Section 5.
bool MainWindow::add_document_file(const QString& /*path*/) {
    return false;
}

// TODO(M4): placeholder so the project links; implement per spec Section 5.
void MainWindow::set_build_delay_ms(int /*ms_per_document*/) {
}

void MainWindow::add_document() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Add Document"), QString(),
        tr("Text files (*.txt);;All files (*)"));

    if (path.isEmpty()) {
        return;
    }

    QFileInfo info(path);
    std::string id = info.absoluteFilePath().toStdString();

    for (const Document& document : workspace_.documents()) {
        if (document.id() == id) {
            status_label_->setText(
                tr("%1 is already in the workspace.")
                    .arg(info.fileName()));
            return;
        }
    }

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        status_label_->setText(
            tr("Could not open %1: %2")
                .arg(info.fileName(), file.errorString()));
        return;
    }

    QString text = QString::fromUtf8(file.readAll());

    workspace_.add_document(
        Document(id, info.fileName().toStdString(),
                 text.toStdString()));

    workspace_widget_->add_document(info.fileName());

    invalidate_corpus();

    status_label_->setText(tr("Added %1.").arg(info.fileName()));
    update_action_states();
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty()) {
        return;
    }

    clear_processing_outputs();

    try {
        processing_core_.rebuild(workspace_);
    } catch (const std::exception& error) {
        corpus_current_ = false;
        workspace_widget_->set_corpus_status(
            tr("Corpus: Build failed"));
        status_label_->setText(
            tr("Build Corpus failed: %1")
                .arg(QString::fromStdString(error.what())));
        update_action_states();
        return;
    }

    corpus_current_ = true;

    workspace_widget_->set_corpus_status(
        tr("Corpus: %1 chunks")
            .arg(static_cast<qulonglong>(
                processing_core_.chunk_count())));

    status_label_->setText(
        tr("Corpus built from %1 document(s).")
            .arg(static_cast<qulonglong>(
                workspace_.documents().size())));

    update_action_states();
}

void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();

    if (row < 0 ||
        static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document();
        return;
    }

    const Document& document = documents[row];

    workspace_widget_->show_selected_document(
        QString::fromStdString(document.title()),
        QString::fromStdString(document.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    QString text = query.trimmed();

    if (!corpus_current_ || text.isEmpty()) {
        return;
    }

    std::vector<SearchResult> results;

    try {
        results = processing_core_.search(text.toStdString(), k);
    } catch (const std::exception& error) {
        status_label_->setText(
            tr("Search failed: %1")
                .arg(QString::fromStdString(error.what())));
        return;
    }

    auto count = results.size();

    search_widget_->set_results(std::move(results));
    context_widget_->clear_context();

    status_label_->setText(
        tr("Search returned %1 result(s).")
            .arg(static_cast<qulonglong>(count)));

    update_action_states();
}

void MainWindow::query_changed(const QString&) {
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    QString query = search_widget_->query().trimmed();

    if (!corpus_current_ || query.isEmpty()) {
        return;
    }

    std::vector<ContextItem> items;

    try {
        items = processing_core_.build_context(
            query.toStdString(),
            search_widget_->result_count(),
            static_cast<std::size_t>(token_budget));
    } catch (const std::exception& error) {
        status_label_->setText(
            tr("Build Context failed: %1")
                .arg(QString::fromStdString(error.what())));
        return;
    }

    context_widget_->set_context(items);

    status_label_->setText(
        tr("Context built: %1 item(s), budget %2 tokens.")
            .arg(static_cast<qulonglong>(items.size()))
            .arg(token_budget));
}

void MainWindow::source_document_requested(
    const QString& document_id) {
    const auto& documents = workspace_.documents();

    for (std::size_t i = 0; i < documents.size(); ++i) {
        if (documents[i].id() == document_id.toStdString()) {
            int row = static_cast<int>(i);

            workspace_widget_->select_document(row);
            document_selected(row);

            status_label_->setText(
                tr("Showing source document %1.")
                    .arg(QString::fromStdString(
                        documents[i].title())));
            return;
        }
    }

    status_label_->setText(
        tr("Source document is not in the workspace."));
}

void MainWindow::term_inspection_requested(
    const QString& term, const QString& chunk_id) {
    QString text = term.trimmed();

    if (!corpus_current_ || text.isEmpty()) {
        return;
    }

    try {
        std::string word = text.toStdString();

        std::size_t df =
            processing_core_.document_frequency(word);

        bool has_chunk = !chunk_id.isEmpty();
        std::size_t tf = 0;

        if (has_chunk) {
            tf = processing_core_.term_frequency(
                word, chunk_id.toStdString());
        }

        term_inspector_widget_->set_statistics(df, has_chunk, tf);

        status_label_->setText(
            tr("Inspected term \"%1\".").arg(text));
    } catch (const std::exception& error) {
        term_inspector_widget_->clear_statistics();

        status_label_->setText(
            tr("Inspect failed: %1")
                .arg(QString::fromStdString(error.what())));
    }
}

void MainWindow::reset_application() {
    workspace_.clear();
    processing_core_ = ProcessingCore{};
    corpus_current_ = false;

    Resettable* widgets[] = {
        workspace_widget_,
        search_widget_,
        term_inspector_widget_,
        context_widget_
    };

    for (Resettable* widget : widgets) {
        widget->reset();
    }

    status_label_->setText(tr("Application reset."));
    update_action_states();
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    bool has_documents = !workspace_.documents().empty();
    bool has_query = search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(has_documents);
    search_widget_->set_search_enabled(
        corpus_current_ && has_query);
    term_inspector_widget_->set_inspection_enabled(corpus_current_);
    context_widget_->set_build_context_enabled(
        corpus_current_ && has_query);
}

void MainWindow::invalidate_corpus() {
    if (corpus_current_) {
        workspace_widget_->set_corpus_status(
            tr("Corpus: Out of date (Build Corpus required)"));
    }

    corpus_current_ = false;
    clear_processing_outputs();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    context_widget_->clear_context();
    term_inspector_widget_->clear_statistics();
}

}  // namespace aiws::gui
