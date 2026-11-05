#include "aiws/gui/main_window.hpp"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <exception>
#include <memory>
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

    poll_timer_ = new QTimer(this);
    poll_timer_->setInterval(kPollIntervalMs);
    connect(poll_timer_, &QTimer::timeout,
            this, &MainWindow::poll_build);

    update_action_states();
}

MainWindow::~MainWindow() {
    // Stop serving first: nothing may queue new work while the window goes away.
    ipc_server_.reset();

    // A plain store, not cancel_running_build(): shutdown must stay prompt.
    if (build_cancel_) {
        build_cancel_->store(true);
    }
    if (build_future_.valid()) {
        build_future_.wait();
    }
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

bool MainWindow::add_document_file(const QString& path) {
    QFileInfo info(path);
    std::string id = info.absoluteFilePath().toStdString();

    for (const Document& document : workspace_.documents()) {
        if (document.id() == id) {
            status_label_->setText(
                tr("%1 is already in the workspace.")
                    .arg(info.fileName()));
            return true;  // Readable, and M3 treats a duplicate as a no-op.
        }
    }

    QFile file(path);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        status_label_->setText(
            tr("Could not open %1: %2")
                .arg(info.fileName(), file.errorString()));
        return false;
    }

    QString text = QString::fromUtf8(file.readAll());

    workspace_.add_document(
        Document(id, info.fileName().toStdString(),
                 text.toStdString()));

    workspace_widget_->add_document(info.fileName());

    invalidate_corpus();

    status_label_->setText(tr("Added %1.").arg(info.fileName()));
    update_action_states();
    return true;
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = ms_per_document;
}

void MainWindow::set_core_factory(CoreFactory factory) {
    core_factory_ = std::move(factory);
}

bool MainWindow::build_in_progress() const noexcept {
    return build_future_.valid();
}

bool MainWindow::start_ipc_server(const QString& socket_path, QString* error) {
    auto server = std::make_unique<IpcServer>(
        socket_path.toStdString(),
        [this](const std::string& path) {
            // Runs on the server thread. A queued call copies the path and
            // returns at once; the slot runs later on the GUI thread.
            QMetaObject::invokeMethod(
                this, "remote_add_document", Qt::QueuedConnection,
                Q_ARG(QString, QString::fromStdString(path)));
        });

    std::string message;
    if (!server->start(message)) {
        if (error != nullptr) {
            *error = QString::fromStdString(message);
        }
        return false;
    }
    ipc_server_ = std::move(server);
    return true;
}

void MainWindow::remote_add_document(const QString& path) {
    add_document_file(path);
}

void MainWindow::add_document() {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Add Document"), QString(),
        tr("Text files (*.txt);;All files (*)"));

    if (!path.isEmpty()) {
        add_document_file(path);
    }
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty() ||
        state_ == CorpusState::Building) {
        return;
    }

    clear_processing_outputs();
    state_ = CorpusState::Building;
    workspace_widget_->set_corpus_status(tr("Corpus: Building..."));
    status_label_->setText(tr("Building corpus..."));

    if (build_future_.valid()) {
        // A cancelled build is still finishing. Only one worker at a time:
        // poll_build() starts this build as soon as that one is collected.
        rebuild_requested_ = true;
    } else {
        start_build_job();
    }

    update_action_states();
}

void MainWindow::start_build_job() {
    build_generation_ = generation_;
    build_cancel_ = std::make_shared<std::atomic<bool>>(false);

    // Workspace is passed by value: the worker gets its own copy.
    build_future_ = std::async(std::launch::async, run_corpus_build,
                               workspace_, build_delay_ms_,
                               build_cancel_, core_factory_);
    poll_timer_->start();
}

void MainWindow::poll_build() {
    if (build_future_.wait_for(std::chrono::seconds(0)) !=
        std::future_status::ready) {
        return;
    }

    poll_timer_->stop();
    const bool current = build_is_current();

    try {
        BuildResult result = build_future_.get();
        if (current) {
            accept_build(std::move(result.core));
        }
    } catch (const std::exception& error) {
        if (current) {
            report_build_failure(QString::fromStdString(error.what()));
        }
    } catch (...) {
        if (current) {
            report_build_failure(tr("unknown error"));
        }
    }

    if (rebuild_requested_) {
        rebuild_requested_ = false;
        start_build_job();
    }
    update_action_states();
}

bool MainWindow::build_is_current() const noexcept {
#ifdef AIWS_FAULT_ACCEPT_STALE
    return true;
#else
    return build_generation_ == generation_;
#endif
}

void MainWindow::accept_build(ProcessingCore core) {
    processing_core_ = std::move(core);
    state_ = CorpusState::Current;

    workspace_widget_->set_corpus_status(
        tr("Corpus: %1 chunks")
            .arg(static_cast<qulonglong>(
                processing_core_.chunk_count())));

    status_label_->setText(
        tr("Corpus built from %1 document(s).")
            .arg(static_cast<qulonglong>(
                workspace_.documents().size())));
}

void MainWindow::report_build_failure(const QString& reason) {
#ifdef AIWS_FAULT_LOSE_FAILURE
    (void)reason;
    return;
#else
    state_ = CorpusState::Failed;
    workspace_widget_->set_corpus_status(tr("Corpus: Build failed"));
    status_label_->setText(tr("Build Corpus failed: %1").arg(reason));
#endif
}

void MainWindow::cancel_running_build() {
#ifndef AIWS_FAULT_ACCEPT_STALE
    if (build_cancel_) {
        build_cancel_->store(true);
    }
#endif
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

    if (!corpus_is_current() || text.isEmpty()) {
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

    if (!corpus_is_current() || query.isEmpty()) {
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

    if (!corpus_is_current() || text.isEmpty()) {
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
    ++generation_;
    cancel_running_build();
    rebuild_requested_ = false;

    workspace_.clear();
    processing_core_ = ProcessingCore{};
    state_ = CorpusState::NotBuilt;

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

bool MainWindow::corpus_is_current() const noexcept {
    return state_ == CorpusState::Current;
}

void MainWindow::update_action_states() {
    bool has_documents = !workspace_.documents().empty();
    bool has_query = search_widget_->has_query();
    bool current = corpus_is_current();

    workspace_widget_->set_build_corpus_enabled(
        has_documents && state_ != CorpusState::Building);
    search_widget_->set_search_enabled(current && has_query);
    term_inspector_widget_->set_inspection_enabled(current);
    context_widget_->set_build_context_enabled(current && has_query);
}

void MainWindow::invalidate_corpus() {
    ++generation_;
    cancel_running_build();
    rebuild_requested_ = false;

    if (state_ != CorpusState::NotBuilt) {
        workspace_widget_->set_corpus_status(
            tr("Corpus: Out of date (Build Corpus required)"));
    }

    state_ = CorpusState::NotBuilt;
    clear_processing_outputs();
}

void MainWindow::clear_processing_outputs() {
    search_widget_->clear_results();
    context_widget_->clear_context();
    term_inspector_widget_->clear_statistics();
}

}  // namespace aiws::gui
