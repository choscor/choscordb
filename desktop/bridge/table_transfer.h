#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace choscordb {
struct TableExportFormatChoice {
    QString code, label;
    bool csvOptions = false, sqlOptions = false;
};
struct TableExportObject {
    QString objectId, qualifiedName, kind, relationSubtype;
};
struct TableExportRequest {
    QString driver;
    QList<TableExportObject> objects;
    QString format, delimiter, quote, lineBreak, nullText;
    bool header = true, structure = true, drop = false, data = true, folder = true;
    QString path, pattern;
};
enum class TableExportField { None, Tables, Options, Destination };
struct TableExportProblem {
    TableExportField field = TableExportField::None;
    QString message;
};
struct TableImportMapping {
    QString target;
    int source = -1;
};
struct TableImportRequest {
    QString driver, path, format, delimiter, quote, nullText, encoding;
    bool header = true, existing = true;
    QString objectId, parentKind, parentQualified, tableName;
    QList<TableImportMapping> mapping;
    QString conflict;
};
struct TableImportPreview {
    bool valid = false;
    QString error;
    QStringList columns;
    int rowCount = 0;
    QStringList cells;
    QList<bool> nulls;
};
struct TableImportReview {
    bool valid = false;
    QString error, sql;
};
struct BackupOption {
    QString code, label;
    bool defaultOn = false;
};
struct BackupForm {
    QList<BackupOption> options;
    QString unavailable;
    bool usesTool = false;
};
struct BackupRequest {
    bool restore = false;
    QString file, toolFolder;
    QStringList options;
};
}
