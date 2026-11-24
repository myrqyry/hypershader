/**
 * HyperShaderFX - Shader Builder Bridge Implementation
 * JavaScript bridge for communication between web builder and C++ plugin
 */

#include "shader_builder_bridge.hpp"
#include "shader_builder_dock.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <obs-module.h>

ShaderBuilderBridge::ShaderBuilderBridge(ShaderBuilderDock *dock, QObject *parent)
    : QObject(parent)
    , m_dock(dock)
{
    // Get data path
    char *path = obs_module_file("shaders");
    if (path) {
        m_dataPath = QString(path);
        bfree(path);
    }
}

QString ShaderBuilderBridge::getDataPath() const
{
    return m_dataPath;
}

QString ShaderBuilderBridge::getRecoveryPath() const
{
    return m_dock ? m_dock->recoveryDirectoryPath() : QString();
}

QString ShaderBuilderBridge::getVersion() const
{
    return "1.0.0";
}

void ShaderBuilderBridge::exportShader(const QString &shaderCode, const QString &shaderName)
{
    if (m_dock) {
        m_dock->onShaderExport(shaderCode, shaderName);
    }
}

void ShaderBuilderBridge::reloadShader(const QString &filePath)
{
    blog(LOG_INFO, "[HyperShaderFX] Reload requested for: %s", filePath.toUtf8().constData());
    if (m_dock)
        m_dock->onLiveReloadRequested(filePath);
    else
        blog(LOG_WARNING, "[HyperShaderFX] Reload declined: builder dock unavailable.");
}

QStringList ShaderBuilderBridge::getShaderList()
{
    QStringList shaders;

    QStringList folders;
    if (char *path = obs_module_file("shaders")) {
	    folders.append(QString::fromUtf8(path));
	    bfree(path);
    }
    if (char *path = obs_module_file("quarantined_shaders")) {
	    folders.append(QString::fromUtf8(path));
	    bfree(path);
    }
    for (const QString &folder : folders) {
        if (folder.isEmpty())
            continue;

        QDir dir(folder);
        QStringList filters;
        filters << "*.effect" << "*.shader";

        QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo &file : files) {
            shaders.append(file.fileName());
        }
    }

    return shaders;
}

QString ShaderBuilderBridge::readShaderFile(const QString &filename)
{
    // Security: only allow reading from the shaders directory
    QString safeName = QFileInfo(filename).fileName(); // Strip path

    char *path = obs_module_file(("shaders/" + safeName).toUtf8().constData());
    if (!path)
	    path = obs_module_file(("quarantined_shaders/" + safeName).toUtf8().constData());
    if (!path) {
        return QString();
    }

    QString filePath(path);
    bfree(path);

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    return QString::fromUtf8(file.readAll());
}

QStringList ShaderBuilderBridge::getRecoveredDraftIds() const
{
    return m_dock ? m_dock->recoveredDraftIds() : QStringList();
}

bool ShaderBuilderBridge::saveRecoveredDraft(const QString &draftId, const QString &payload)
{
	return m_dock ? m_dock->saveRecoveryDraft(draftId, payload) : false;
}

QString ShaderBuilderBridge::loadRecoveredDraft(const QString &draftId) const
{
	return m_dock ? m_dock->loadRecoveryDraft(draftId) : QString();
}

bool ShaderBuilderBridge::deleteRecoveredDraft(const QString &draftId)
{
	return m_dock ? m_dock->deleteRecoveryDraft(draftId) : false;
}

void ShaderBuilderBridge::log(const QString &message, int level)
{
    const char *prefix = "[ShaderBuilder] ";
    QByteArray msg = (prefix + message).toUtf8();

    switch (level) {
        case 0: // Info
            blog(LOG_INFO, "%s", msg.constData());
            break;
        case 1: // Warning
            blog(LOG_WARNING, "%s", msg.constData());
            break;
        case 2: // Error
            blog(LOG_ERROR, "%s", msg.constData());
            break;
        default:
            blog(LOG_DEBUG, "%s", msg.constData());
            break;
    }
}
