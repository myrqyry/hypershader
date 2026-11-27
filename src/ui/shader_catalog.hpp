#pragma once
// Pure Qt shader-library discovery used by Shader Manager and its tests.
// Only catalog files; discovery never compiles, applies, or modifies a shader.
#include <QCryptographicHash>
#include "../shader/shader_display_name.h"
#include "../shader/shader_categories.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <algorithm>

struct hypershader_catalog_entry {
    QString path;
    QString category;
};

// Same role-prefixed label contract as the native filter chooser.
// Category still represents the collection and never mutates the file path.
inline QString shaderCatalogDisplayLabel(const QString &shaderPath, const QString &category)
{
    QString relative = QFileInfo(shaderPath).fileName();
    if (category == QStringLiteral("Rikiya Daily Practice"))
        relative.prepend(QStringLiteral("root/rikiya/"));
    else if (category.startsWith(QStringLiteral("Collection / ")))
        relative.prepend(QStringLiteral("root/") +
                         category.mid(QStringLiteral("Collection / ").size()) +
                         QStringLiteral("/"));
    else
        relative.prepend(QStringLiteral("root/"));

    const QByteArray encoded = relative.toUtf8();
    char label[4096];
    const QByteArray shaderFile = QFile::encodeName(shaderPath);
    if (hs_shader_display_label_from_file(label, sizeof(label),
                                          encoded.constData(), shaderFile.constData()))
        return QString::fromUtf8(label);
    return QFileInfo(shaderPath).completeBaseName();
}

inline bool shaderCatalogMatches(const hypershader_catalog_entry &entry,
                                 const QString &query, const QString &category)
{
    if (!category.isEmpty() && entry.category != category)
        return false;
    const QString needle = query.trimmed();
    return needle.isEmpty() ||
           shaderCatalogDisplayLabel(entry.path, entry.category).contains(
               needle, Qt::CaseInsensitive) ||
           QFileInfo(entry.path).fileName().contains(needle, Qt::CaseInsensitive) ||
           entry.category.contains(needle, Qt::CaseInsensitive) ||
           entry.path.contains(needle, Qt::CaseInsensitive);
}

inline QList<hypershader_catalog_entry> discover_hypershader_files(const QString &shaderFolder)
{
    QList<hypershader_catalog_entry> entries;
    if (shaderFolder.isEmpty())
        return entries;
    const QDir base(shaderFolder);
    if (!base.exists())
        return entries;

    QSet<QString> seen;
    const QStringList suffixes = {"*.effect", "*.glsl", "*.frag"};
    auto addDirectory = [&](const QDir &dir, const QString &category) {
        if (!dir.exists())
            return;
        for (const QFileInfo &file : dir.entryInfoList(suffixes, QDir::Files | QDir::Readable | QDir::NoSymLinks,
                                                       QDir::Name | QDir::IgnoreCase)) {
            const QString canonical = file.canonicalFilePath();
            if (canonical.isEmpty() || seen.contains(canonical))
                continue;
            seen.insert(canonical);
            entries.append({canonical, category});
        }
    };

    addDirectory(base, QStringLiteral("Loose"));
    const QDir root(base.filePath("root"));
    addDirectory(root, QStringLiteral("Library"));

    // Only immediate children of root/ are collections. Never recurse into
    // arbitrary third-party directories, plugins, or symlink targets.
    for (const QFileInfo &child : root.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::NoSymLinks, QDir::Name)) {
        const QString folderName = child.fileName();
        if (!hs_shader_catalog_collection_allowed(folderName.toUtf8().constData()))
            continue;
        const QString category = folderName.compare(QStringLiteral("rikiya"), Qt::CaseInsensitive) == 0
            ? QStringLiteral("Rikiya Daily Practice")
            : QStringLiteral("Collection / ") + folderName;
        addDirectory(QDir(child.absoluteFilePath()), category);
    }

    addDirectory(QDir(base.filePath("builder-applied")), QStringLiteral("Applied"));
    addDirectory(QDir(base.filePath("../quarantined_shaders")), QStringLiteral("Quarantined"));
    addDirectory(QDir(base.filePath("../quarantined_shaders/root")), QStringLiteral("Quarantined"));

    std::sort(entries.begin(), entries.end(),
              [](const hypershader_catalog_entry &a, const hypershader_catalog_entry &b) {
                  const int categoryOrder = QString::compare(a.category, b.category, Qt::CaseInsensitive);
                  return categoryOrder < 0 ||
                         (categoryOrder == 0 && QString::compare(QFileInfo(a.path).fileName(),
                                                        QFileInfo(b.path).fileName(), Qt::CaseInsensitive) < 0);
              });
    return entries;
}


// Preview filenames are derived from the actual effect's SHA-256, preventing
// edits from inheriting a stale image. This is a read-only cache lookup.
inline QString shaderPreviewFilePath(const QString &shaderPath)
{
    const QFileInfo info(shaderPath);
    if (!info.isFile() || info.suffix().compare("effect", Qt::CaseInsensitive) != 0)
        return QString();
    QFile shader(info.absoluteFilePath());
    if (!shader.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray digest =
        QCryptographicHash::hash(shader.readAll(), QCryptographicHash::Sha256).toHex().left(16);
    const QString candidate =
        QDir(info.absolutePath()).filePath("previews/" + info.completeBaseName() +
                                           "-" + QString::fromLatin1(digest) + ".png");
    return QFileInfo(candidate).isFile() ? candidate : QString();
}
