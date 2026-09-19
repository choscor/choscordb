#pragma once
#include "widgets/document_io/document_io.h"
#include <Qsci/qsciscintilla.h>
#include <optional>

namespace choscordb {
class SqlEditor final : public QsciScintilla {
    Q_OBJECT
  public:
    explicit SqlEditor(QWidget* parent = nullptr);
    void openFile(const QString& path);
    void saveFile(const QString& path);
    QString filePath() const { return path_; }
    bool isIoBusy() const { return ioBusy_; }
    quint64 revision() const { return revision_; }
    std::optional<quint64> connectionTarget() const { return connectionTarget_; }
    bool hasAssignedTarget() const { return targetAssigned_; }
    void setConnectionTarget(std::optional<quint64> connection, const QString& label = {}) {
        connectionTarget_ = connection;
        targetLabel_ = label;
        targetAssigned_ = true;
        emit connectionTargetChanged();
    }
    QString targetLabel() const { return targetLabel_; }
    void setProfileId(const QString& id);
    void setEditorFont(const QFont& font);
    bool restoreDocument(const QByteArray& sql, const QString& path, quint64 cursor, quint64 anchor,
                         bool modified);
  signals:
    void profileAssociationChanged();
    void connectionTargetChanged();
    void fileOpened(const QString& path, const QString& error);
    void fileSaved(const QString& path, const QString& error);

  protected:
    void changeEvent(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;

  private:
    void applyPalette();
    void updateLineNumberMargin();
    std::optional<quint64> connectionTarget_;
    QString targetLabel_;
    bool targetAssigned_ = false;
    QString path_;
    DocumentIo io_;
    quint64 revision_ = 0;
    bool ioBusy_ = false;
};
} // namespace choscordb
