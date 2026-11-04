#include "aiws/gui/search_result_model.hpp"

#include <QString>
#include <utility>

namespace aiws::gui {

namespace {
// Search-result table columns required by the M3 spec.
constexpr int rank_column = 0;
constexpr int document_column = 1;
constexpr int chunk_column = 2;
constexpr int score_column = 3;
constexpr int text_column = 4;
constexpr int column_count = 5;
}

SearchResultModel::SearchResultModel(QObject* parent)
    : QAbstractTableModel(parent) {
}

int SearchResultModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return static_cast<int>(results_.size());
}

int SearchResultModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }

    return column_count;
}

QVariant SearchResultModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }

    const SearchResult* result = result_at(index.row());

    if (result == nullptr) {
        return {};
    }

    switch (index.column()) {
    case rank_column:
        return index.row() + 1;
    case document_column:
        return QString::fromStdString(result->document_id);
    case chunk_column:
        return static_cast<qulonglong>(result->chunk_sequence);
    case score_column:
        return result->score;
    case text_column:
        return QString::fromStdString(result->text);
    default:
        return {};
    }
}

QVariant SearchResultModel::headerData(
    int section, Qt::Orientation orientation, int role) const {

    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }

    switch (section) {
    case rank_column:
        return QStringLiteral("Rank");
    case document_column:
        return QStringLiteral("Document");
    case chunk_column:
        return QStringLiteral("Chunk");
    case score_column:
        return QStringLiteral("Score");
    case text_column:
        return QStringLiteral("Text");
    default:
        return {};
    }
}

void SearchResultModel::set_results(std::vector<SearchResult> results) {
    beginResetModel();
    results_ = std::move(results);
    endResetModel();
}

void SearchResultModel::clear() {
    beginResetModel();
    results_.clear();
    endResetModel();
}

const SearchResult* SearchResultModel::result_at(int row) const noexcept {
    if (row < 0 || row >= static_cast<int>(results_.size())) {
        return nullptr;
    }

    return &results_[static_cast<std::size_t>(row)];
}

}  // namespace aiws::gui
