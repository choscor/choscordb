#pragma once
#include <QList>
#include <QString>
#include <QStringList>
#include <utility>

namespace choscordb {
enum class ObjectInspectionPane { Columns = 0, Indexes = 1, Keys = 2, Ddl = 3, Triggers = 6 };
struct CodeLabel {
    QString code;
    QString label;
};
struct StructureField {
    QString key, label, input, names;
    bool required = false;
    QList<CodeLabel> choices;
};
struct StructureChangeKindInfo {
    QString code, label, unavailable;
    ObjectInspectionPane pane = ObjectInspectionPane::Columns;
    QList<StructureField> fields;
};
struct StructureColumn {
    QString name, dataType;
    bool nullable = true;
};
struct StructureKey {
    QString name, kind;
};
struct StructureTable {
    QString driver;
    bool existing = true;
    QString qualifiedName, parentKind, parentQualified, tableName;
    QList<StructureColumn> columns;
    QStringList indexes;
    QList<StructureKey> keys;
};
struct StructureChange {
    QString kind;
    QList<std::pair<QString, QString>> values;
};
struct StructurePlan {
    bool valid = false;
    QString error;
    QStringList summaries, statements;
};
}
