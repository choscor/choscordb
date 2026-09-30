#include "app/query_workspace.h"
#include "design_system/button/button.h"
#include "design_system/metrics/metrics.h"
#include "design_system/right_sheet/right_sheet.h"
#include "design_system/text/text.h"
#include <QAccessible>
#include <QAction>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTextCursor>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <algorithm>

namespace choscordb {
namespace {
// Qt normalizes paragraph separators and toPlainText also normalizes NBSP. Keep
// the literal draft and edit deltas beside the presentation, including Qt undo.
class CellDraftTextEdit final : public QPlainTextEdit {
  public:
    explicit CellDraftTextEdit(QWidget* parent) : QPlainTextEdit(parent) {
        // Qt drops combine edits after the MIME insertion returns, hiding the
        // literal source boundaries. Keep this editor to typing and clipboard.
        setAcceptDrops(false);
        viewport()->setAcceptDrops(false);
        connect(document(), &QTextDocument::undoCommandAdded, this, [this] { newCommand_ = true; });
        connect(
            document(), &QTextDocument::contentsChange, this,
            [this](int position, int removed, int added) { changed(position, removed, added); });
    }
    void setDraftText(const QString& text) {
        initializing_ = true;
        setPlainText(text);
        exact_ = text;
        crlf_ = pairedSeparators(text);
        undo_.clear();
        redo_.clear();
        previousSteps_ = document()->availableUndoSteps();
        newCommand_ = false;
        initializing_ = false;
    }
    QString draftText() const { return exact_; }

  protected:
    QMimeData* createMimeDataFromSelection() const override {
        auto* data = new QMimeData;
        const auto cursor = textCursor();
        const auto first = sourceOffset(cursor.selectionStart());
        const auto last = sourceOffset(cursor.selectionEnd());
        data->setText(exact_.mid(first, last - first));
        return data;
    }
    void insertFromMimeData(const QMimeData* source) override {
        pasted_ = source->text();
        QPlainTextEdit::insertFromMimeData(source);
        pasted_.reset();
    }

  private:
    struct Edit {
        int steps, position;
        // Adjacent CR and LF created by separate edits are two document chars.
        // Restore their actual lengths and mappings rather than recounting text.
        int removedLength, insertedLength;
        QString removed, inserted;
        std::vector<qsizetype> removedCrlf, insertedCrlf;
    };
    static std::vector<qsizetype> pairedSeparators(const QString& text) {
        std::vector<qsizetype> result;
        for (qsizetype i = 0, position = 0; i < text.size(); ++i, ++position)
            if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                result.push_back(position);
                ++i;
            }
        return result;
    }
    qsizetype sourceOffset(qsizetype position) const {
        return position + (std::lower_bound(crlf_.begin(), crlf_.end(), position) - crlf_.begin());
    }
    void replace(int position, int removed, const QString& inserted, int added,
                 const std::vector<qsizetype>& insertedCrlf) {
        const auto first = sourceOffset(position), last = sourceOffset(position + removed);
        exact_.replace(first, last - first, inserted);
        auto begin = std::lower_bound(crlf_.begin(), crlf_.end(), position);
        auto end = std::lower_bound(begin, crlf_.end(), position + removed);
        const auto offset = begin - crlf_.begin();
        auto following = crlf_.erase(begin, end);
        const auto shift = added - removed;
        for (auto it = following; it != crlf_.end(); ++it)
            *it += shift;
        crlf_.insert(crlf_.begin() + offset, insertedCrlf.begin(), insertedCrlf.end());
        for (std::size_t i = 0; i < insertedCrlf.size(); ++i)
            crlf_[offset + i] += position;
    }
    void changed(int position, int removed, int added) {
        if (initializing_)
            return;
        const int steps = document()->availableUndoSteps();
        // Qt updates steps before this signal. New commands distinguish editing
        // from redo; merged typing can contribute several deltas at one step.
        if (steps < previousSteps_) {
            while (!undo_.empty() && undo_.back().steps > steps) {
                auto edit = std::move(undo_.back());
                undo_.pop_back();
                replace(edit.position, edit.insertedLength, edit.removed, edit.removedLength,
                        edit.removedCrlf);
                redo_.push_back(std::move(edit));
            }
        } else if (steps > previousSteps_ && !newCommand_) {
            while (!redo_.empty() && redo_.back().steps <= steps) {
                auto edit = std::move(redo_.back());
                redo_.pop_back();
                replace(edit.position, edit.removedLength, edit.inserted, edit.insertedLength,
                        edit.insertedCrlf);
                undo_.push_back(std::move(edit));
            }
        } else {
            QTextCursor cursor(document());
            cursor.setPosition(position);
            cursor.setPosition(position + added, QTextCursor::KeepAnchor);
            QString inserted = pasted_ ? *pasted_ : cursor.selectedText();
            if (!pasted_)
                inserted.replace(QChar::ParagraphSeparator, QChar('\n'));
            const auto first = sourceOffset(position), last = sourceOffset(position + removed);
            QString previous = exact_.mid(first, last - first);
            std::vector<qsizetype> previousCrlf;
            for (auto it = std::lower_bound(crlf_.begin(), crlf_.end(), position);
                 it != crlf_.end() && *it < position + removed; ++it)
                previousCrlf.push_back(*it - position);
            auto insertedCrlf = pasted_ ? pairedSeparators(inserted) : std::vector<qsizetype>{};
            replace(position, removed, inserted, added, insertedCrlf);
            undo_.push_back({steps, position, removed, added, std::move(previous),
                             std::move(inserted), std::move(previousCrlf),
                             std::move(insertedCrlf)});
            if (newCommand_)
                redo_.clear();
        }
        previousSteps_ = steps;
        newCommand_ = false;
    }
    QString exact_;
    std::vector<qsizetype> crlf_;
    std::vector<Edit> undo_, redo_;
    std::optional<QString> pasted_;
    int previousSteps_ = 0;
    bool initializing_ = false, newCommand_ = false;
};
} // namespace
void QueryWorkspace::setupCellEditor() {
    connect(model_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
                if (cellEditStaging_ || !cellEditIndex_.isValid())
                    return;
                const bool valueChanged = roles.isEmpty() || roles.contains(Qt::DisplayRole) ||
                                          roles.contains(Qt::EditRole);
                if (valueChanged && first.row() <= cellEditIndex_.row() &&
                    last.row() >= cellEditIndex_.row() &&
                    first.column() <= cellEditIndex_.column() &&
                    last.column() >= cellEditIndex_.column())
                    clearCellEditor();
            });
    connect(model_, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        ++cellResultGeneration_;
        clearCellEditor();
    });
    connect(model_, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            [this](const QModelIndex&, int first, int last) {
                if (cellEditIndex_.isValid() && first <= cellEditIndex_.row() &&
                    last >= cellEditIndex_.row())
                    clearCellEditor();
            });
    connect(this, &QueryWorkspace::documentTargetChanged, this, [this] {
        ++cellResultGeneration_;
        clearCellEditor();
    });
}
bool QueryWorkspace::cellEditEligible(const QPersistentModelIndex& index) const {
    return index.isValid() && index.model() == model_ && widgets_.grid->model() == model_ &&
           widgets_.grid->selectionModel() && widgets_.grid->selectionModel()->model() == model_ &&
           jsonResultCurrent() && !stopping_ && !workInFlight() && !editabilityPlanning_ &&
           (model_->flags(index) & Qt::ItemIsEditable);
}
void QueryWorkspace::appendCellEditAction(QMenu& menu, const QPersistentModelIndex& clicked,
                                          bool current) {
    auto* edit = menu.addAction(tr("Edit cell…"));
    edit->setObjectName("editCell");
    const auto generation = cellResultGeneration_;
    const auto target = editTargetToken_;
    const auto query = query_, connection = queryConnection_;
    QPointer<QueryWorkspace> owner(this);
    const auto refresh = [owner, edit, current, clicked, generation, target, query, connection] {
        edit->setEnabled(owner && current && generation == owner->cellResultGeneration_ &&
                         target == owner->editTargetToken_ && query == owner->query_ &&
                         connection == owner->queryConnection_ && owner->cellEditEligible(clicked));
    };
    refresh();
    connect(model_, &QAbstractItemModel::modelReset, edit, refresh);
    connect(model_, &QAbstractItemModel::rowsRemoved, edit, refresh);
    connect(model_, &QAbstractItemModel::dataChanged, edit, refresh);
    connect(this, &QueryWorkspace::activityChanged, edit, refresh);
    connect(this, &QueryWorkspace::documentTargetChanged, edit, refresh);
    connect(edit, &QAction::triggered, this,
            [this, clicked, generation, target, query, connection] {
                if (generation == cellResultGeneration_ && target == editTargetToken_ &&
                    query == query_ && connection == queryConnection_ && cellEditEligible(clicked))
                    openCellEditor(clicked);
            });
}
void QueryWorkspace::clearCellEditor() {
    ++cellDraftGeneration_;
    cellEditIndex_ = QPersistentModelIndex{};
    cellOpenQuery_.reset();
    cellOpenConnection_.reset();
    cellDraftEdited_ = false;
    if (cellEditSheet_ && cellEditSheet_->isVisible())
        cellEditSheet_->reject();
    if (cellEditText_)
        static_cast<CellDraftTextEdit*>(cellEditText_.data())->setDraftText({});
}
void QueryWorkspace::openCellEditor(const QPersistentModelIndex& index) {
    if (!cellEditEligible(index))
        return;
    clearCellEditor();
    if (!cellEditSheet_) {
        cellEditSheet_ = new design::RightSheet(widgets_.dialogParent);
        cellEditSheet_->setObjectName("cellEditSheet");
        cellEditSheet_->setTitle(tr("Edit cell…"));
        auto* body = new QWidget(cellEditSheet_);
        auto* layout = new QVBoxLayout(body);
        layout->setContentsMargins(
            design::spacing(design::Spacing::Four), design::spacing(design::Spacing::Three),
            design::spacing(design::Spacing::Four), design::spacing(design::Spacing::Three));
        layout->setSpacing(design::spacing(design::Spacing::Two));
        cellEditStatus_ = new design::Text({}, body);
        cellEditStatus_->setObjectName("cellEditStatus");
        cellEditStatus_->setAccessibleName(tr("Cell edit status"));
        cellEditStatus_->setWordWrap(true);
        cellEditText_ = new CellDraftTextEdit(body);
        cellEditText_->setObjectName("cellEditText");
        cellEditText_->setAccessibleName(tr("Cell value"));
        layout->addWidget(cellEditStatus_);
        layout->addWidget(cellEditText_, 1);
        cellEditSheet_->setBody(body);
        auto* cancel = new design::Button(tr("Cancel"), cellEditSheet_);
        cancel->setObjectName("cellEditCancel");
        cancel->setAutoDefault(false);
        cellEditSave_ = new design::Button(tr("Save"), cellEditSheet_);
        cellEditSave_->setObjectName("cellEditSave");
        cellEditSave_->setAutoDefault(false);
        cellEditSheet_->footerLayout()->addWidget(cancel);
        cellEditSheet_->footerLayout()->addWidget(cellEditSave_);
        connect(cancel, &QPushButton::clicked, cellEditSheet_, &QDialog::reject);
        connect(cellEditSave_, &QPushButton::clicked, this, &QueryWorkspace::saveCellEditor);
        connect(cellEditSheet_, &QDialog::finished, this, [this] { clearCellEditor(); });
        connect(cellEditText_, &QPlainTextEdit::textChanged, this, [this] {
            if (cellEditSheet_->isVisible()) {
                cellDraftEdited_ = true;
                ++cellDraftGeneration_;
            }
        });
    }
    cellEditIndex_ = index;
    cellOpenResultGeneration_ = cellResultGeneration_;
    cellOpenTargetToken_ = editTargetToken_;
    cellOpenQuery_ = query_;
    cellOpenConnection_ = queryConnection_;
    const auto value = model_->cellValue(index);
    const bool omitted =
        model_->inserted()[index.row()] && !model_->touched()[index.row()][index.column()];
    const bool null = value && std::holds_alternative<std::monostate>(*value);
    static_cast<CellDraftTextEdit*>(cellEditText_.data())
        ->setDraftText(null || omitted ? QString{} : index.data(Qt::EditRole).toString());
    cellEditText_->setReadOnly(false);
    cellEditText_->setAccessibleDescription({});
    cellEditSave_->setEnabled(!cellEditParsing_);
    cellDraftEdited_ = false;
    cellEditStatus_->setText(
        omitted ? tr("Database default (omitted)")
        : null
            ? tr("SQL NULL")
            : model_->headerData(index.column(), Qt::Horizontal, ResultTableModel::HeaderNameRole)
                  .toString());
    cellEditSheet_->open();
    cellEditText_->setFocus(Qt::OtherFocusReason);
}
void QueryWorkspace::cellEditError(const QString& error) {
    cellEditStatus_->setText(error);
    cellEditText_->setAccessibleDescription(error);
    QAccessibleEvent announcement(cellEditStatus_, QAccessible::Alert);
    QAccessible::updateAccessibility(&announcement);
    cellEditText_->setFocus(Qt::OtherFocusReason);
}
void QueryWorkspace::saveCellEditor() {
    if (!cellEditSheet_ || !cellEditSheet_->isVisible() || cellEditParsing_)
        return;
    if (!cellEditEligible(cellEditIndex_) || cellOpenResultGeneration_ != cellResultGeneration_ ||
        cellOpenTargetToken_ != editTargetToken_ || cellOpenQuery_ != query_ ||
        cellOpenConnection_ != queryConnection_) {
        cellEditError(tr("This cell is no longer editable. Cancel and reload the result."));
        return;
    }
    if (!cellDraftEdited_) {
        cellEditSheet_->accept();
        return;
    }
    auto snapshot = model_->cellEditSnapshot(
        cellEditIndex_, static_cast<CellDraftTextEdit*>(cellEditText_.data())->draftText());
    const auto draftGeneration = cellDraftGeneration_;
    const auto resultGeneration = cellOpenResultGeneration_;
    const auto query = cellOpenQuery_, connection = cellOpenConnection_;
    const auto target = cellOpenTargetToken_;
    const auto index = cellEditIndex_;
    cellEditParsing_ = true;
    cellEditText_->setReadOnly(true);
    cellEditSave_->setEnabled(false);
    auto* watcher = new QFutureWatcher<ResultTableModel::CellEditEvaluation>(this);
    connect(watcher, &QFutureWatcher<ResultTableModel::CellEditEvaluation>::finished, this,
            [this, watcher, draftGeneration, resultGeneration, query, connection, target, index] {
                auto evaluation = watcher->result();
                watcher->deleteLater();
                cellEditParsing_ = false;
                if (cellEditSave_)
                    cellEditSave_->setEnabled(true);
                if (cellEditText_)
                    cellEditText_->setReadOnly(false);
                if (draftGeneration != cellDraftGeneration_ || !cellEditSheet_ ||
                    !cellEditSheet_->isVisible())
                    return;
                if (resultGeneration != cellResultGeneration_ || query != query_ ||
                    connection != queryConnection_ || target != editTargetToken_ ||
                    index != cellEditIndex_ || !cellEditEligible(index)) {
                    clearCellEditor();
                    return;
                }
                cellEditStaging_ = true;
                evaluation = model_->stageCellEdit(index, std::move(evaluation));
                cellEditStaging_ = false;
                if (evaluation.state == ResultTableModel::CellEditState::Ready)
                    cellEditSheet_->accept();
                else
                    cellEditError(evaluation.error);
            });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)]() mutable {
        return ResultTableModel::evaluateCellEdit(std::move(snapshot));
    }));
}
} // namespace choscordb
