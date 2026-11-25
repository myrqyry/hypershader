#pragma once
#include "../shader/shader_categories.h"
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <functional>
#include <utility>

// Runs solely on the owning Qt thread. Watch directories for creates/deletes
// and files for in-place metadata edits; re-arm after atomic editor saves.
class ShaderCatalogWatch final : public QObject {
public:
    ShaderCatalogWatch(QString folder, std::function<void()> changed, QObject *parent = nullptr)
        : QObject(parent), base(std::move(folder)), onChanged(std::move(changed))
    {
        debounce.setSingleShot(true);
        debounce.setInterval(180);
        auto schedule = [this](const QString &) {
            // Coalesce bursts without starving updates during continuous writes.
            if (!debounce.isActive()) debounce.start();
        };
        connect(&watcher, &QFileSystemWatcher::directoryChanged, this, schedule);
        connect(&watcher, &QFileSystemWatcher::fileChanged, this, schedule);
        connect(&debounce, &QTimer::timeout, this, [this] {
            rearm();
            onChanged();
        });
        rearm();
    }

    void stop()
    {
        debounce.stop();
        watcher.blockSignals(true);
        const auto paths = watcher.files() + watcher.directories();
        if (!paths.isEmpty()) watcher.removePaths(paths);
    }

private:
    void rearm()
    {
        if (base.isEmpty()) return;
        QSet<QString> wanted;
        auto addDirectory = [&](const QString &path, bool files) {
            const QFileInfo info(path);
            if (!info.isDir() || info.isSymLink()) return;
            wanted.insert(info.absoluteFilePath());
            if (!files) return;
            const QDir dir(path);
            for (const auto &file : dir.entryInfoList({"*.effect","*.glsl","*.frag"},
                    QDir::Files | QDir::NoSymLinks))
                wanted.insert(file.absoluteFilePath());
        };
        // Also watch the parent so replacing the entire shader folder is seen.
        addDirectory(QFileInfo(base).absolutePath(), false);
        addDirectory(base, true);
        const QDir root(QDir(base).filePath("root"));
        addDirectory(root.absolutePath(), true);
        for (const auto &child : root.entryInfoList(
                QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
            if (!hs_shader_catalog_collection_allowed(child.fileName().toUtf8().constData()))
                continue;
            addDirectory(child.absoluteFilePath(), true);
        }
        addDirectory(QDir(base).filePath("builder-applied"), true);
        const auto current = watcher.files() + watcher.directories();
        QStringList remove, add;
        QSet<QString> existing;
        for (const auto &path : current) {
            existing.insert(path);
            if (!wanted.contains(path)) remove.append(path);
        }
        for (const auto &path : wanted)
            if (!existing.contains(path)) add.append(path);
        if (!remove.isEmpty()) watcher.removePaths(remove);
        if (!add.isEmpty()) watcher.addPaths(add);
    }

    QString base;
    std::function<void()> onChanged;
    QFileSystemWatcher watcher;
    QTimer debounce;
};
