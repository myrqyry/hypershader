#pragma once
/**
 * HyperShaderFX - Shader Builder Bridge
 * JavaScript bridge for communication between web builder and C++ plugin
 */

#ifndef SHADER_BUILDER_BRIDGE_HPP
#define SHADER_BUILDER_BRIDGE_HPP

#include <QObject>
#include <QString>
#include <QStringList>

class ShaderBuilderDock;

/**
 * Bridge object exposed to JavaScript via QWebChannel
 * Allows the web-based shader builder to communicate with the C++ plugin
 */
class ShaderBuilderBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString dataPath READ getDataPath CONSTANT)
    Q_PROPERTY(QString recoveryPath READ getRecoveryPath CONSTANT)
    Q_PROPERTY(QString version READ getVersion CONSTANT)

public:
    explicit ShaderBuilderBridge(ShaderBuilderDock *dock, QObject *parent = nullptr);

    // Properties accessible from JavaScript
    QString getDataPath() const;
    QString getRecoveryPath() const;
    QString getVersion() const;

public slots:
    /**
     * Export shader code to file and notify plugin
     * Called from JavaScript: bridge.exportShader(code, name)
     */
    Q_INVOKABLE void exportShader(const QString &shaderCode, const QString &shaderName);

    /**
     * Request the plugin to reload a specific shader file
     * Called from JavaScript: bridge.reloadShader(path)
     */
    Q_INVOKABLE void reloadShader(const QString &filePath);

    /**
     * Get list of available shader files in the shaders directory
     * Called from JavaScript: bridge.getShaderList()
     */
    Q_INVOKABLE QStringList getShaderList();

    /**
     * Read a shader file's contents
     * Called from JavaScript: bridge.readShaderFile(filename)
     */
    Q_INVOKABLE QString readShaderFile(const QString &filename);

    /**
     * Get a list of recovery draft ids currently stored on disk.
     */
	Q_INVOKABLE QStringList getRecoveredDraftIds() const;

	/**
	 * Save a recovery draft snapshot.
	 */
	Q_INVOKABLE bool saveRecoveredDraft(const QString &draftId, const QString &payload);

	/**
	 * Load a recovery draft snapshot.
	 */
	Q_INVOKABLE QString loadRecoveredDraft(const QString &draftId) const;

	/**
	 * Delete a recovery draft snapshot.
	 */
	Q_INVOKABLE bool deleteRecoveredDraft(const QString &draftId);

    /**
     * Log message to OBS log
     * Called from JavaScript: bridge.log(message, level)
     */
    Q_INVOKABLE void log(const QString &message, int level = 0);

signals:
    // Emitted to notify JavaScript of events
    void shaderSaved(const QString &path, bool success);
    void shaderListUpdated(const QStringList &shaders);
    void themeChanged(const QString &themeCss);

private:
    ShaderBuilderDock *m_dock;
    QString m_dataPath;
};

#endif // SHADER_BUILDER_BRIDGE_HPP
