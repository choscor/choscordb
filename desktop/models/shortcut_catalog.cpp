#include "shortcut_catalog.h"
#include <QHash>
#include <QKeySequence>
#include <QRegularExpression>
#include <QSet>
namespace choscordb {
namespace {
bool parseSequence(const QString& text, QKeySequence& sequence) {
    if (!text.isValidUtf16() || text.size() > 128 || text.contains(QChar(0)) ||
        text.toUtf8().size() > 128)
        return false;
    sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (text.isEmpty())
        return true;
    if (sequence.isEmpty())
        return false;
    for (int i = 0; i < sequence.count(); ++i)
        if (sequence[i].key() == Qt::Key_unknown || sequence[i].key() == Qt::Key(0))
            return false;
    // Portable persisted notation must survive a round trip. This also catches
    // malformed suffixes Qt's permissive parser could otherwise discard.
    auto canonical = sequence.toString(QKeySequence::PortableText);
    auto requested = text.trimmed();
    canonical.remove(' ');
    requested.remove(' ');
    // Modifier ordering is semantically irrelevant; key/chord text must still
    // survive, so a fifth chord or a trailing delimiter cannot disappear.
    static const QRegularExpression modifiers("(?:Ctrl|Alt|Shift|Meta|Num)\\+",
                                              QRegularExpression::CaseInsensitiveOption);
    canonical.remove(modifiers);
    requested.remove(modifiers);
    return canonical.compare(requested, Qt::CaseInsensitive) == 0;
}
} // namespace
QString shortcutValidationError(const EditorPreferences& preferences,
                                const QList<ShortcutDescriptor>& catalog) {
    const auto limits = EngineAdapter::editorPreferenceLimits();
    if (preferences.version != limits.version)
        return QObject::tr("Unsupported preferences version.");
    if (preferences.fontSize < limits.minFontSize || preferences.fontSize > limits.maxFontSize)
        return QObject::tr("Font size is outside the supported range.");
    if (!preferences.fontFamily.isValidUtf16() || preferences.fontFamily.contains(QChar(0)) ||
        preferences.fontFamily.size() > 256 || preferences.fontFamily.toUtf8().size() > 256)
        return QObject::tr("Font family must be valid text of at most 256 UTF-8 bytes.");
    QHash<QString, qsizetype> identifiers;
    QList<QKeySequence> effective;
    for (qsizetype i = 0; i < catalog.size(); ++i) {
        if (catalog[i].id.isEmpty() || identifiers.contains(catalog[i].id))
            return QObject::tr("The command catalog contains duplicate or empty identifiers.");
        identifiers.insert(catalog[i].id, i);
        QKeySequence sequence;
        if (!parseSequence(catalog[i].defaultSequence, sequence))
            return QObject::tr("Invalid default shortcut for %1.").arg(catalog[i].label);
        effective.append(sequence);
    }
    QSet<QString> seen;
    for (const auto& custom : preferences.shortcuts) {
        const auto found = identifiers.constFind(custom.command);
        if (found == identifiers.cend())
            return QObject::tr("Unknown command in shortcut overrides.");
        if (seen.contains(custom.command))
            return QObject::tr("A command has more than one shortcut override.");
        seen.insert(custom.command);
        const auto index = found.value();
        if (!catalog[index].configurable)
            return QObject::tr("The shortcut for %1 cannot be changed.").arg(catalog[index].label);
        QKeySequence sequence;
        if (!parseSequence(custom.sequence, sequence))
            return QObject::tr("Invalid shortcut for %1.").arg(catalog[index].label);
        effective[index] = sequence;
    }
    for (qsizetype i = 0; i < effective.size(); ++i) {
        if (effective[i].isEmpty())
            continue;
        for (qsizetype j = i + 1; j < effective.size(); ++j) {
            if (effective[j].isEmpty())
                continue;
            if (effective[i].matches(effective[j]) != QKeySequence::NoMatch ||
                effective[j].matches(effective[i]) != QKeySequence::NoMatch)
                return QObject::tr("The shortcuts for %1 and %2 conflict.")
                    .arg(catalog[i].label, catalog[j].label);
        }
    }
    return {};
}
} // namespace choscordb
