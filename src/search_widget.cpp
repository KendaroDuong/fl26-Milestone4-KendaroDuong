#include "aiws/gui/search_widget.hpp"

#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

#include "aiws/gui/search_result_model.hpp"

namespace aiws::gui {

namespace {
constexpr int min_results = 1;
constexpr int max_results = 100;
constexpr int default_results = 3;
}

SearchWidget::SearchWidget(QWidget* parent)
    : QWidget(parent) {
    auto* group = new QGroupBox(tr("Query"), this);

    query_edit_ = new QLineEdit(group);
    query_edit_->setPlaceholderText(tr("Enter a search query"));
    query_edit_->setClearButtonEnabled(true);

    result_count_spin_ = new QSpinBox(group);
    result_count_spin_->setRange(min_results, max_results);
    result_count_spin_->setValue(default_results);

    search_button_ = new QPushButton(tr("Search"), group);

    auto* query_layout = new QHBoxLayout;
    query_layout->addWidget(new QLabel(tr("Query:"), group));
    query_layout->addWidget(query_edit_);
    query_layout->addWidget(new QLabel(tr("Results (k):"), group));
    query_layout->addWidget(result_count_spin_);
    query_layout->addWidget(search_button_);

    auto* results_group = new QGroupBox(tr("Search Results"), group);

    result_model_ = new SearchResultModel(this);
    results_view_ = new QTableView(results_group);
    results_view_->setModel(result_model_);
    results_view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_view_->setSelectionMode(QAbstractItemView::SingleSelection);
    results_view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_view_->setAlternatingRowColors(true);
    results_view_->verticalHeader()->hide();
    results_view_->horizontalHeader()->setStretchLastSection(true);

    auto* results_layout = new QVBoxLayout(results_group);
    results_layout->addWidget(results_view_);

    auto* details_group = new QGroupBox(tr("Selected Result"), group);

    selected_result_text_ = new QPlainTextEdit(details_group);
    selected_result_text_->setReadOnly(true);
    selected_result_text_->setPlaceholderText(
        tr("Select a result to view its details."));

    view_source_button_ =
        new QPushButton(tr("View Source"), details_group);

    auto* details_layout = new QVBoxLayout(details_group);
    details_layout->addWidget(selected_result_text_);
    details_layout->addWidget(view_source_button_);

    auto* output_layout = new QHBoxLayout;
    output_layout->addWidget(results_group, 3);
    output_layout->addWidget(details_group, 2);

    auto* group_layout = new QVBoxLayout(group);
    group_layout->addLayout(query_layout);
    group_layout->addLayout(output_layout);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(group);

    connect(search_button_, &QPushButton::clicked,
            this, &SearchWidget::request_search);

    connect(query_edit_, &QLineEdit::textChanged,
            this, &SearchWidget::query_changed);

    connect(results_view_->selectionModel(),
            &QItemSelectionModel::selectionChanged,
            this, &SearchWidget::show_selected_result);

    connect(view_source_button_, &QPushButton::clicked,
            this, &SearchWidget::request_source_document);

    reset();
}

QString SearchWidget::query() const {
    return query_edit_->text();
}

int SearchWidget::result_count() const {
    return result_count_spin_->value();
}

bool SearchWidget::has_query() const {
    return !query().trimmed().isEmpty();
}

void SearchWidget::set_search_enabled(bool enabled) {
    search_button_->setEnabled(enabled);
}

void SearchWidget::set_results(std::vector<SearchResult> results) {
    result_model_->set_results(std::move(results));
    results_view_->clearSelection();
    results_view_->scrollToTop();
    show_selected_result();
}

void SearchWidget::clear_results() {
    result_model_->clear();
    results_view_->clearSelection();
    show_selected_result();
}

void SearchWidget::reset() {
    query_edit_->clear();
    result_count_spin_->setValue(default_results);
    clear_results();
    set_search_enabled(false);
}

void SearchWidget::request_search() {
    emit search_requested(query(), result_count());
}

void SearchWidget::show_selected_result() {
    const QModelIndexList rows =
        results_view_->selectionModel()->selectedRows();

    const SearchResult* result = nullptr;

    if (!rows.isEmpty()) {
        result = result_model_->result_at(rows.first().row());
    }

    if (result == nullptr) {
        selected_result_text_->clear();
        view_source_button_->setEnabled(false);
        emit selected_chunk_changed(QString());
        return;
    }

    // Multi-argument arg() substitutes in one pass, so a '%' in the
    // path or text cannot be mistaken for a later placeholder.
    selected_result_text_->setPlainText(
        QStringLiteral(
            "Document: %1\n"
            "Chunk: %2 (%3)\n"
            "Score: %4\n"
            "Matched terms: %5\n\n%6")
            .arg(QString::fromStdString(result->document_id),
                 QString::number(result->chunk_sequence),
                 QString::fromStdString(result->chunk_id),
                 QString::number(result->score, 'f', 3),
                 QString::number(result->matched_terms),
                 QString::fromStdString(result->text)));

    view_source_button_->setEnabled(true);

    emit selected_chunk_changed(
        QString::fromStdString(result->chunk_id));
}

void SearchWidget::request_source_document() {
    const QModelIndexList rows =
        results_view_->selectionModel()->selectedRows();

    if (rows.isEmpty()) {
        return;
    }

    const SearchResult* result =
        result_model_->result_at(rows.first().row());

    if (result != nullptr) {
        emit source_document_requested(
            QString::fromStdString(result->document_id));
    }
}

}  // namespace aiws::gui
