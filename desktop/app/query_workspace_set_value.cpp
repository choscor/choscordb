#include "app/query_workspace.h"
#include "models/result_table_model.h"
#include "widgets/document_io/document_io.h"

#include <QAction>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMenu>
#include <QPointer>

namespace choscordb {
void QueryWorkspace::appendSetValueActions(QMenu& menu, const QPersistentModelIndex& clicked,
                                           bool current) {
    const bool writable = current && clicked.isValid() && queryConnection_ &&
                          model_->cellWritable(clicked.row(), clicked.column());
    auto* setValue = menu.addMenu(tr("Set value"));
    setValue->setObjectName("setValueMenu");
    setValue->setEnabled(writable);
    for (const auto& kind : EngineAdapter::setValueKinds()) {
        auto* action = setValue->addAction(kind.label);
        action->setObjectName(QStringLiteral("setValue_") + kind.code);
        connect(action, &QAction::triggered, this,
                [this, clicked, code = kind.code] { setCellValue(clicked, code); });
    }
    const auto type = clicked.isValid() && clicked.column() < model_->columnCount()
                          ? model_->columns()[clicked.column()].databaseType
                          : QString();
    setValue->addSeparator();
    auto* load = setValue->addAction(tr("Load from file…"));
    load->setObjectName("loadCellFile");
    load->setEnabled(writable && EngineAdapter::gridBinaryColumn(type));
    connect(load, &QAction::triggered, this, [this, clicked] { loadCellFile(clicked); });
    const auto value = current && clicked.isValid() ? model_->cellValue(clicked) : std::nullopt;
    auto* save = menu.addAction(tr("Save value to file…"));
    save->setObjectName("saveCellFile");
    save->setEnabled(value && std::holds_alternative<QByteArray>(*value));
    connect(save, &QAction::triggered, this, [this, clicked] { saveCellFile(clicked); });
}
void QueryWorkspace::setCellValue(const QPersistentModelIndex& index, const QString& kind) {
    if (!index.isValid() || index.model() != model_ || !queryConnection_)
        return;
    const auto type = model_->columns()[index.column()].databaseType;
    const auto choice = EngineAdapter::gridSetValue(kind, driverForConnection(*queryConnection_),
                                                    type, model_->inserted()[index.row()],
                                                    model_->columnNullable(index.column()));
    if (!choice.error.isEmpty()) {
        message(choice.error);
        return;
    }
    bool staged = false;
    if (choice.outcome == QLatin1String("omit")) {
        staged = model_->omitInsertedCell(index);
    } else if (choice.outcome == QLatin1String("expression")) {
        staged = model_->stageCell(index, Cell{}, choice.expression);
    } else if (choice.outcome == QLatin1String("null")) {
        staged = model_->stageCell(index, Cell{});
    } else {
        QString error;
        auto parsed = EngineAdapter::parseGridEditValue(type, choice.text, &error);
        if (!parsed) {
            message(error);
            return;
        }
        staged = model_->stageCell(index, std::move(*parsed));
    }
    if (!staged)
        message(tr("The cell cannot take this value. Discard other staged edits and try again."));
}
void QueryWorkspace::loadCellFile(const QPersistentModelIndex& index) {
    const auto path =
        QFileDialog::getOpenFileName(widgets_.dialogParent, tr("Load value from file"));
    if (path.isEmpty())
        return;
    auto* watcher = new QFutureWatcher<DocumentIoResult>(this);
    connect(watcher, &QFutureWatcher<DocumentIoResult>::finished, this, [this, watcher, index] {
        watcher->deleteLater();
        auto result = watcher->result();
        if (!result.error.isEmpty()) {
            message(tr("Load value: %1").arg(result.error));
            return;
        }
        if (!index.isValid() || !model_->stageCell(index, std::move(result.bytes)))
            message(
                tr("The cell cannot take this value. Discard other staged edits and try again."));
    });
    watcher->setFuture(DocumentIo().readCell(path));
}
void QueryWorkspace::saveCellFile(const QPersistentModelIndex& index) {
    const auto value = index.isValid() ? model_->cellValue(index) : std::nullopt;
    const auto* bytes = value ? std::get_if<QByteArray>(&*value) : nullptr;
    if (!bytes)
        return;
    const auto path = QFileDialog::getSaveFileName(widgets_.dialogParent, tr("Save value to file"));
    if (path.isEmpty())
        return;
    auto* watcher = new QFutureWatcher<DocumentIoResult>(this);
    connect(watcher, &QFutureWatcher<DocumentIoResult>::finished, this,
            [this, watcher, path, size = bytes->size()] {
                watcher->deleteLater();
                const auto result = watcher->result();
                message(result.error.isEmpty()
                            ? tr("Saved %n bytes to %1.", nullptr, int(size)).arg(path)
                            : tr("Save value: %1").arg(result.error));
            });
    watcher->setFuture(DocumentIo().writeCell(path, *bytes));
}
}
