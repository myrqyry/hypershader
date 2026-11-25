#pragma once
/**
 * HyperShaderFX - Shader Builder Dock
 * Qt dock widget with native shader manager and code editor
 */

#ifndef SHADER_BUILDER_DOCK_HPP
#define SHADER_BUILDER_DOCK_HPP

#include <QDockWidget>
#include <QListWidget>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <thread>
#include <obs-data.h>
#include <obs-frontend-api.h>

class QLabel;
class QListWidgetItem;
class QLineEdit;
class QComboBox;
class QPushButton;
class QTimer;
class ShaderCatalogWatch;
struct llm_instance;

class ShaderBuilderDock : public QDockWidget {
	Q_OBJECT

	struct ShaderDraft {
		QString draftId;
		QString sourcePath;
		QString displayName;
		QString text;
		bool dirty = false;
		bool recovered = false;
	};

public:
	explicit ShaderBuilderDock(QWidget *parent = nullptr);
	~ShaderBuilderDock() override;
	void stopCatalogWatch();

	QString getShaderOutputPath() const;

	void saveSettings(obs_data_t *settings);
	void loadSettings(obs_data_t *settings);

	QString recoveryDirectoryPath() const;
	QStringList recoveredDraftIds() const;
	bool saveRecoveryDraft(const QString &draftId, const QString &payload);
	QString loadRecoveryDraft(const QString &draftId) const;
	bool deleteRecoveryDraft(const QString &draftId);

signals:
	void shaderExported(const QString &filePath, const QString &shaderName);

public slots:
	void onShaderExport(const QString &shaderCode, const QString &shaderName);
	void onLiveReloadRequested(const QString &filePath);
	void onFrontendEvent(obs_frontend_event event);
	void onHotkeyTriggered(bool pressed);

private:
	void setupUI();
	void setupManagerUI();
	void setupCallbacks();
	void setupHotkeys();
	void initializeRecoveryStorage();
	void loadRecoveredDrafts();
	bool saveDraftSnapshot(const ShaderDraft &draft);
	bool removeDraftSnapshot(const QString &draftId);
	QString recoveryFilePath(const QString &draftId) const;
	ShaderDraft createDraft(const QString &sourcePath = QString(), const QString &displayName = QString(),
			       const QString &text = QString(), bool dirty = false,
			       bool recovered = false) const;
	ShaderDraft draftFromJson(const QJsonObject &object) const;
	QJsonObject draftToJson(const ShaderDraft &draft) const;
	void refreshManagerLists();
	void applyLibraryShader();
	void filterShaderFiles();
	void updateShaderSelection();
	void updateShaderViewMode();
	QString getShaderFolderPath() const;
	void openActiveShaderProperties(const QString &uuid);
	void openSelectedShaderFile(const QString &path);
	static bool enumActiveShaderSources(void *param, obs_source_t *source);
	void addActiveShaderItem(const QString &name, const QString &uuid, const QString &sourceId);
	void addShaderFileItem(const QString &path, const QString &category);

	void onActiveShaderDoubleClicked(QListWidgetItem *item);
	void onShaderFileDoubleClicked(QListWidgetItem *item);
	void onOpenFileClicked();
	void onSaveAsClicked();
	void onValidateClicked();
	void onApplyClicked();
	void clearValidationSource();
	void onGenerateAiClicked();
	void onPrepareRepairClicked();
	void pollCompileFeedback();
	void finishAiGeneration(const QString &generatedCode, const QString &errorMessage);
	QString finalizeGeneratedShader(const QString &generatedCode) const;

	QTabWidget *m_tabs;
	QWidget *m_managerWidget;
	QListWidget *m_activeShadersList;
	QListWidget *m_shaderFilesList;
	QLineEdit *m_shaderSearch;
	QComboBox *m_shaderCategory;
	QComboBox *m_shaderViewMode;
	QLabel *m_shaderFileCount;
	QLabel *m_shaderSelectionInfo;
	QLabel *m_shaderPreviewImage;
	QLabel *m_applyTargetLabel;
	QLabel *m_compileStatusLabel;
	QPlainTextEdit *m_compileDiagnostics;
	QTimer *m_compilePollTimer;
	ShaderCatalogWatch *m_catalogWatch = nullptr;
	QPushButton *m_aiRepairButton;
	obs_source_t *m_validationSource = nullptr;
	QPushButton *m_openShaderButton;
	QPushButton *m_libraryApplyButton = nullptr;
	QLabel *m_libraryApplyStatus = nullptr;
	bool m_librarySubmission = false;
	QPlainTextEdit *m_codeEditor;
	QPlainTextEdit *m_aiPrompt;
	QPushButton *m_aiGenerateButton;
	QLabel *m_aiStatusLabel;
	struct llm_instance *m_llm;
	std::thread m_llmThread;
	std::atomic_bool m_llmGenerating{false};
	std::atomic_bool m_closing{false};
	bool m_llmModelLoaded = false;
	QString m_aiModelDir;
	QString m_dataPath;
	QString m_recoveryDir;
	ShaderDraft m_activeDraft;
	QList<ShaderDraft> m_recoveredDrafts;
	QString m_lastShader;
	QString m_loadedEditorText;
	QString m_pendingRevisionPath;
	QString m_validationFilePath;
	QString m_pendingSourceUuid;
	QString m_submittedEditorCode;
	QString m_verifiedEditorCode;
	QString m_previousShaderFile;
	long long m_previousPreset = -1;
	long long m_previousPresetSchema = 2;
	QString m_lastCompileError;
	QString m_lastFailedCode;
	int m_compilePollAttempts = 0;

	obs_hotkey_id m_hotkeyReload = OBS_INVALID_HOTKEY_ID;
};

#endif // SHADER_BUILDER_DOCK_HPP
