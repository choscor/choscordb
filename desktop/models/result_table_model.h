#pragma once
#include <QAbstractTableModel>
#include <QByteArray>
#include <QItemSelection>
#include <QString>
#include <QStringList>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>
namespace choscordb {
struct DeferredValue {
    quint64 handle;
    quint64 bytes;
    QString type;
    bool fallback = false;
};
struct FallbackText {
    QString text;
    QString databaseType;
};
struct UnavailableValue {
    QString databaseType;
    QString reason;
};
struct DecimalValue {
    QString text;
};
using Cell = std::variant<std::monostate, bool, qint64, double, DecimalValue, QString, QByteArray,
                          DeferredValue, FallbackText, UnavailableValue>;
struct ResultColumn {
    QString name;
    QString databaseType;
    std::optional<quint32> precision;
    std::optional<qint32> scale;
    QString timezone;
    std::optional<bool> nullable;
};
// How a staged grid cell is represented; Rust owns what each kind permits.
enum class GridCell : uint8_t { Value, Binary, Deferred, FallbackText, Unavailable };
inline constexpr std::size_t GridCellKinds = std::size_t(GridCell::Unavailable) + 1;
struct GridCellPolicy {
    bool inlineEditable = false, blocksRowDelete = false, blocksRowDuplicate = false,
         duplicateRequiresLoad = false;
};
struct ResultCellMetadata {
    QString sourceColumn;
    QString sourceObject;
    QString sourceQualifiedName;
    std::optional<bool> nullable;
    bool boolean = false;
    QStringList enumChoices;
    QString targetObject;
    QString targetQualifiedName;
    QString targetColumn;
    bool operator==(const ResultCellMetadata&) const = default;
};
class ResultTableModel final : public QAbstractTableModel {
    Q_OBJECT
  public:
    static constexpr int HeaderTypeRole = Qt::UserRole + 1;
    static constexpr int HeaderKeyRole = Qt::UserRole + 2;
    static constexpr int HeaderNameRole = Qt::UserRole + 3;
    static constexpr int ResultValueKindRole = Qt::UserRole + 4;
    // Bounded single-line DisplayRole previews keep layout and elision cost per cell constant.
    // EditRole and cellValue() keep the complete value.
    static constexpr qsizetype DisplayPreviewChars = 512;
    using Row = std::vector<Cell>;
    using ResolvedCells = std::map<std::pair<int, int>, Cell>;
    enum class JsonViewScope { Cell, Row, Page };
    enum class JsonViewState { Ready, NeedsDeferred, Invalid, Unavailable };
    struct JsonViewSnapshot {
        JsonViewScope scope = JsonViewScope::Row;
        std::vector<ResultColumn> columns;
        std::vector<Row> rows;
        std::vector<std::vector<bool>> touched;
        std::vector<bool> inserted;
        ResolvedCells resolved;
        std::size_t byteBudget = 0;
        int column = 0;
    };
    struct JsonViewEvaluation {
        JsonViewState state = JsonViewState::Invalid;
        QString json;
        QString error;
    };
    struct CopyCellSnapshot {
        Cell original;
        std::optional<Cell> resolved;
        bool insertedOmitted = false;
    };
    struct CopyResolutionSnapshot {
        std::optional<Cell> original;
        Cell resolved;
    };
    struct CopySnapshot {
        std::vector<std::vector<std::optional<CopyCellSnapshot>>> rows;
        std::vector<CopyResolutionSnapshot> resolutions;
        std::size_t byteBudget = 0;
    };
    struct CopyEvaluation {
        QString text;
        QString error;
    };
    enum class CellEditState { Ready, TypeRejected, ResourceRefused, Ineligible };
    struct CellEditSnapshot {
        QString databaseType;
        QString text;
        std::size_t byteBudget = 0;
        bool eligible = false;
    };
    struct CellEditEvaluation {
        CellEditState state = CellEditState::Ineligible;
        std::optional<Cell> value;
        QString error;
        std::size_t bytes = 0;
    };
    // Rust's result-view budget, read once per process.
    static std::size_t defaultBytes();
    // Budget includes owned page allocations, excluding this fixed QObject and
    // caller-owned in-flight transfers. Shared Qt buffers are charged in full.
    explicit ResultTableModel(QObject* parent = nullptr, std::size_t byteBudget = defaultBytes());
    std::size_t residentBytes() const { return residentBytes_; }
    std::size_t byteBudget() const { return byteBudget_; }
    bool setByteBudget(std::size_t bytes) {
        if (bytes < residentBytes_ + stagedBytes_)
            return false;
        byteBudget_ = bytes;
        return true;
    }
    // A page with its display strings, link filters and header types, built on a worker.
    struct PreparedPage {
        std::vector<ResultColumn> columns;
        std::vector<Row> rows;
        // One entry per page cell, row-major.
        std::vector<QString> display;
        std::vector<bool> filterable;
        std::vector<QString> headerTypes;
        quint64 firstRow = 0;
        // The owned allocations; empty when the page shape is not valid.
        std::optional<std::size_t> bytes;
    };
    // Safe on a worker thread. Rust gives `filterable`, whether a foreign-key link can filter
    // by each cell; empty means no cell can.
    static PreparedPage preparePage(std::vector<ResultColumn> columns, std::vector<Row> rows,
                                    std::vector<bool> filterable, quint64 firstRow);
    std::optional<DeferredValue> deferredValue(const QModelIndex& index) const;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void multiData(const QModelIndex& index, QModelRoleDataSpan roleDataSpan) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    // `metadata`, when it is not empty, is the cell metadata that arrives with the page.
    bool setPage(PreparedPage page, std::vector<ResultCellMetadata> metadata = {});
    bool setPage(std::vector<ResultColumn> columns, std::vector<Row> rows, quint64 firstRow,
                 std::vector<ResultCellMetadata> metadata = {});
    bool setCellMetadata(std::vector<ResultCellMetadata> metadata);
    std::optional<ResultCellMetadata> linkedColumn(const QModelIndex& index) const;
    std::optional<Cell> cellValue(const QModelIndex& index) const;
    void setEditableColumns(std::vector<bool> editable, bool canInsert, bool canDelete,
                            std::vector<bool> insertEditable = {});
    void setKeyColumns(std::vector<bool> keys);
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    // Capture only the target's type and staging allowance on the model thread.
    // Evaluate explicit draft text on a worker, then stage on the model thread.
    CellEditSnapshot cellEditSnapshot(const QModelIndex& index, const QString& text) const;
    static CellEditEvaluation evaluateCellEdit(CellEditSnapshot snapshot);
    CellEditEvaluation stageCellEdit(const QModelIndex& index, CellEditEvaluation evaluation);
    bool setNull(const QModelIndex& index);
    // Stages NULL in every editable selected cell with one dataChanged per range and a single
    // pendingEditsChanged notification.
    bool setNull(const QItemSelection& selection);
    bool hasEditableCell(const QItemSelection& selection) const;
    // Sorted, unique, in-range rows covered by the selection.
    std::vector<int> selectedRows(const QItemSelection& selection) const;
    bool addRow();
    bool duplicateRow(int row, QString* error = nullptr);
    bool duplicateRow(int row, const std::vector<bool>& copyable, QString* error = nullptr);
    void markDeleted(const QModelIndexList& selection, bool deleted);
    void markRowsDeleted(std::vector<int> rows, bool deleted);
    void discardEdits();
    bool hasPendingEdits() const;
    bool canInsert() const { return canInsert_; }
    bool canDelete() const { return canDelete_; }
    bool hasInsertedRows() const { return insertedCount_ != 0; }
    bool hasDeletedRows() const { return deletedCount_ != 0; }
    const std::vector<ResultColumn>& columns() const { return columns_; }
    // Rows before originalRowCount() came from the page; staged inserts follow them.
    std::size_t originalRowCount() const { return originalRowCount_; }
    const Row& originalRow(std::size_t row) const;
    const std::vector<Row>& rows() const { return rows_; }
    // Whether the cell has a staged value.
    bool touched(std::size_t row, std::size_t column) const;
    std::vector<bool> touchedRow(std::size_t row) const;
    const std::vector<bool>& inserted() const { return inserted_; }
    const std::vector<bool>& deleted() const { return deleted_; }
    // Scope 0 copies a rectangle covering the selected cells, leaving unselected cells blank.
    // Scope 1 copies every selected row; scope 2 copies the page and ignores the selection.
    std::optional<CopySnapshot> copySnapshot(const QItemSelection& selection, int scope,
                                             const ResolvedCells& resolved = {}) const;
    // Deferred cells that a copy of the same selection and scope must load first.
    std::vector<std::pair<int, int>> copyDeferredCells(const QItemSelection& selection,
                                                       int scope) const;
    static CopyEvaluation evaluateCopy(CopySnapshot snapshot);
    // Capture Qt model state on its owning thread. The returned value has no QObject references
    // and can be evaluated on a worker thread.
    std::optional<JsonViewSnapshot> jsonViewSnapshot(JsonViewScope scope, int row, int column,
                                                     const ResolvedCells& resolved = {}) const;
    static JsonViewEvaluation evaluateJsonView(JsonViewSnapshot snapshot);

  signals:
    void pendingEditsChanged(bool pending);

  private:
    struct CopyShape {
        std::vector<int> rows;
        int minColumn = 0;
        int maxColumn = -1;
        // Scope 0 only: selected cells inside the rows x columns rectangle.
        std::vector<bool> selected;
        bool contains(std::size_t line, int column) const {
            return selected.empty() ||
                   selected[line * static_cast<std::size_t>(maxColumn - minColumn + 1) +
                            static_cast<std::size_t>(column - minColumn)];
        }
    };
    std::optional<CopyShape> copyShape(const QItemSelection& selection, int scope) const;
    // A staged value, filled only on edit.
    struct CellEdit {
        std::size_t bytes = 0;
        QString display;
        bool filterable = false;
    };
    static quint64 cellKey(std::size_t row, std::size_t column) {
        return (quint64(row) << 32) | quint64(column);
    }
    const CellEdit* edit(std::size_t row, std::size_t column) const;
    std::size_t editBytes(int row, int column) const;
    // The charge of a staged `value` beyond its parsed bytes: the entry and its display text.
    static std::size_t editCharge(const Cell& value, const QString& display);
    bool validIndex(const QModelIndex& index) const;
    QVariant cellData(int row, int column, int role) const;
    const GridCellPolicy& cellPolicy(const Cell& cell) const;
    bool cellEditable(int row, int column) const;
    bool cellFilterable(int row, int column) const;
    // Stages `value`; `bytes` is the whole charge, from editCharge and the parsed value.
    void stageValue(int row, int column, Cell value, QString display, std::size_t bytes);
    bool stageNull(int row, int column);
    void eraseInsertedRow(int row);
    void preserveOriginal(int row);
    bool linkable(int row, int column) const;
    void applyCellMetadata(std::vector<ResultCellMetadata> metadata);
    void resetPendingCounts();
    std::vector<ResultColumn> columns_;
    std::vector<ResultCellMetadata> cellMetadata_;
    std::vector<Row> rows_;
    // Page originals are retained only for original rows that received a staged edit;
    // every other original row is still identical to rows_.
    std::map<std::size_t, Row> editedOriginals_;
    std::size_t originalRowCount_ = 0;
    std::size_t insertedCount_ = 0, deletedCount_ = 0;
    // Per original page cell, row-major; staged cells use edits_ instead.
    std::vector<QString> display_;
    std::vector<bool> filterable_;
    std::vector<QString> headerTypes_;
    std::unordered_map<quint64, CellEdit> edits_;
    // Columns with complete reference metadata.
    std::vector<bool> linkColumns_;
    // Rust's grid cell policies, copied once so that flags() never asks.
    std::array<GridCellPolicy, GridCellKinds> cellPolicies_;
    std::vector<bool> inserted_, deleted_, editable_;
    std::vector<bool> insertEditable_;
    std::vector<bool> keyColumns_;
    bool canInsert_ = false, canDelete_ = false;
    quint64 firstRow_ = 0;
    std::size_t byteBudget_;
    std::size_t residentBytes_ = 0;
    std::size_t stagedBytes_ = 0;
};
} // namespace choscordb
