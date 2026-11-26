/**
 * hypershader - Shader Builder Dock Implementation
 * Qt dock widget with native shader manager and code editor
 */

#include "shader_builder_dock.hpp"
#include "shader_reload_request.hpp"
#include "shader_catalog.hpp"
#include "shader_catalog_watch.hpp"
#include "shader_catalog_refresh.hpp"
#include "shader_performance_panel.hpp"

extern "C" bool scan_shaders_dir(void);
#include "shader_code_classification.hpp"
#include "shader_repair_prompt.hpp"

#include "../llm/llm_c_api.h"
#include "../llm/llm_context.h"

#include <QMainWindow>
#include <QMenu>
#include <QByteArray>
#include <QComboBox>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QPixmap>
#include <QListView>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QListWidgetItem>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTabBar>
#include <QVBoxLayout>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFileDialog>
#include <QMessageBox>
#include <QFont>
#include <QMetaObject>
#include <QMetaType>
#include <QPointer>
#include <QTimer>
#include <QUuid>
#include <obs-module.h>
#include <obs-frontend-api.h>

// ============================================================================
// Static Callbacks
// ============================================================================

static void save_callback(obs_data_t *save_data, bool saving, void *data)
{
	ShaderBuilderDock *dock = static_cast<ShaderBuilderDock *>(data);
	if (!dock)
		return;

	if (saving) {
		dock->saveSettings(save_data);
	} else {
		dock->loadSettings(save_data);
	}
}

static void frontend_event_callback(obs_frontend_event event, void *data)
{
	ShaderBuilderDock *dock = static_cast<ShaderBuilderDock *>(data);
	if (dock) {
		QMetaObject::invokeMethod(dock, "onFrontendEvent", Qt::QueuedConnection,
					  Q_ARG(obs_frontend_event, event));
	}
}

static void hotkey_reload_callback(void *data, obs_hotkey_id id, obs_hotkey_t *key, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(key);
	ShaderBuilderDock *dock = static_cast<ShaderBuilderDock *>(data);
	if (dock && pressed) {
		QMetaObject::invokeMethod(dock, "onHotkeyTriggered", Qt::QueuedConnection, Q_ARG(bool, true));
	}
}

// ============================================================================
// ShaderBuilderDock Implementation
// ============================================================================

ShaderBuilderDock::ShaderBuilderDock(QWidget *parent)
	: QDockWidget(parent),
	  m_tabs(nullptr),
	  m_managerWidget(nullptr),
	  m_activeShadersList(nullptr),
	  m_shaderFilesList(nullptr),
	  m_shaderSearch(nullptr),
	  m_shaderCategory(nullptr),
	  m_shaderViewMode(nullptr),
	  m_shaderFileCount(nullptr),
	  m_shaderSelectionInfo(nullptr),
	  m_shaderPreviewImage(nullptr),
	  m_applyTargetLabel(nullptr),
	  m_compileStatusLabel(nullptr),
	  m_compileDiagnostics(nullptr),
	  m_compilePollTimer(nullptr),
	  m_aiRepairButton(nullptr),
	  m_openShaderButton(nullptr),
	  m_codeEditor(nullptr),
	  m_aiPrompt(nullptr),
	  m_aiGenerateButton(nullptr),
	  m_aiStatusLabel(nullptr),
	  m_llm(nullptr)
{
	setObjectName("ShaderBuilderDock");
	setWindowTitle("hypershader");
	setMinimumSize(280, 300);

	char *path = obs_module_file("builder/index.html");
	if (path) {
		m_dataPath = QFileInfo(path).absolutePath();
		bfree(path);
	}

	m_llm = llm_create_instance();
	char *tokenizerPath = obs_module_file("models/qwen2_5-0_5b-instruct/tokenizer.json");
	if (tokenizerPath) {
		m_aiModelDir = QFileInfo(QString::fromUtf8(tokenizerPath)).absolutePath();
		bfree(tokenizerPath);
	}

	initializeRecoveryStorage();
	qRegisterMetaType<obs_frontend_event>("obs_frontend_event");
	setupUI();
	setupCallbacks();
	setupHotkeys();
	m_catalogWatch = new ShaderCatalogWatch(getShaderFolderPath(), [this] {
		// This runs from a debounced Qt timer, never from a property callback
		// or the render thread. The catalog lock is released before notification.
		scan_shaders_dir();
		refresh_hypershader_properties();
		refreshManagerLists();
	}, this);

	obs_data_t *settings = obs_data_create();
	loadSettings(settings);
	obs_data_release(settings);
}

void ShaderBuilderDock::stopCatalogWatch()
{
	if (m_catalogWatch)
		m_catalogWatch->stop();
}

ShaderBuilderDock::~ShaderBuilderDock()
{
	stopCatalogWatch();
	m_closing.store(true);
	if (m_compilePollTimer)
		m_compilePollTimer->stop();
	clearValidationSource();
	if (m_llmThread.joinable())
		m_llmThread.join();
	if (m_llm) {
		llm_destroy_instance(m_llm);
		m_llm = nullptr;
	}

	if (m_hotkeyReload != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(m_hotkeyReload);
		m_hotkeyReload = OBS_INVALID_HOTKEY_ID;
	}

	obs_frontend_remove_save_callback(save_callback, this);
	obs_frontend_remove_event_callback(frontend_event_callback, this);

	blog(LOG_INFO, "[hypershader] Shader Builder dock cleaned up");
}

QString ShaderBuilderDock::recoveryDirectoryPath() const
{
	return m_recoveryDir;
}

QStringList ShaderBuilderDock::recoveredDraftIds() const
{
	QStringList draftIds;
	draftIds.reserve(m_recoveredDrafts.size());
	for (const ShaderDraft &draft : m_recoveredDrafts) {
		if (!draft.draftId.isEmpty())
			draftIds.append(draft.draftId);
	}
	return draftIds;
}

ShaderBuilderDock::ShaderDraft ShaderBuilderDock::createDraft(const QString &sourcePath, const QString &displayName,
							      const QString &text, bool dirty, bool recovered) const
{
	ShaderDraft draft;
	draft.draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	draft.sourcePath = sourcePath;
	draft.displayName = displayName.isEmpty() ? QStringLiteral("Untitled Shader") : displayName;
	draft.text = text;
	draft.dirty = dirty;
	draft.recovered = recovered;
	return draft;
}

QString ShaderBuilderDock::recoveryFilePath(const QString &draftId) const
{
	if (m_recoveryDir.isEmpty() || draftId.isEmpty())
		return QString();

	return QDir(m_recoveryDir).filePath(draftId + ".json");
}

QJsonObject ShaderBuilderDock::draftToJson(const ShaderDraft &draft) const
{
	QJsonObject object;
	object.insert("draftId", draft.draftId);
	object.insert("sourcePath", draft.sourcePath);
	object.insert("displayName", draft.displayName);
	object.insert("text", draft.text);
	object.insert("dirty", draft.dirty);
	object.insert("recovered", draft.recovered);
	return object;
}

ShaderBuilderDock::ShaderDraft ShaderBuilderDock::draftFromJson(const QJsonObject &object) const
{
	ShaderDraft draft;
	draft.draftId = object.value("draftId").toString();
	draft.sourcePath = object.value("sourcePath").toString();
	draft.displayName = object.value("displayName").toString();
	draft.text = object.value("text").toString();
	draft.dirty = object.value("dirty").toBool(true);
	draft.recovered = object.value("recovered").toBool(true);
	if (draft.displayName.isEmpty())
		draft.displayName = QStringLiteral("Recovered Shader");
	return draft;
}

void ShaderBuilderDock::initializeRecoveryStorage()
{
	m_activeDraft = createDraft();
	m_recoveredDrafts.clear();

	char *path = obs_module_config_path("shader-drafts");
	if (!path) {
		blog(LOG_WARNING, "[hypershader] Recovery storage path unavailable");
		return;
	}

	m_recoveryDir = QString::fromUtf8(path);
	bfree(path);

	if (m_recoveryDir.isEmpty())
		return;

	QDir dir(m_recoveryDir);
	if (!dir.exists() && !dir.mkpath(".")) {
		blog(LOG_WARNING, "[hypershader] Failed to create recovery directory: %s",
		     m_recoveryDir.toUtf8().constData());
		return;
	}

	loadRecoveredDrafts();
}

void ShaderBuilderDock::loadRecoveredDrafts()
{
	m_recoveredDrafts.clear();
	if (m_recoveryDir.isEmpty())
		return;

	QDir dir(m_recoveryDir);
	const QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Time);
	for (const QFileInfo &info : files) {
		QFile file(info.absoluteFilePath());
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
			blog(LOG_WARNING, "[hypershader] Failed to read recovery draft: %s",
			     info.absoluteFilePath().toUtf8().constData());
			continue;
		}

		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
		file.close();
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			blog(LOG_WARNING, "[hypershader] Invalid recovery draft JSON: %s",
			     info.absoluteFilePath().toUtf8().constData());
			continue;
		}

		ShaderDraft draft = draftFromJson(document.object());
		if (draft.draftId.isEmpty())
			draft.draftId = info.completeBaseName();
		draft.recovered = true;
		if (draft.displayName.isEmpty())
			draft.displayName = info.completeBaseName();
		m_recoveredDrafts.append(draft);
	}

	blog(LOG_INFO, "[hypershader] Loaded %d recovered shader draft(s)",
	     static_cast<int>(m_recoveredDrafts.size()));
}

bool ShaderBuilderDock::saveDraftSnapshot(const ShaderDraft &draft)
{
	if (draft.draftId.isEmpty())
		return false;

	const QString filePath = recoveryFilePath(draft.draftId);
	if (filePath.isEmpty())
		return false;

	QDir dir(m_recoveryDir);
	if (!dir.exists() && !dir.mkpath(".")) {
		blog(LOG_WARNING, "[hypershader] Failed to prepare recovery directory: %s",
		     m_recoveryDir.toUtf8().constData());
		return false;
	}

	QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		blog(LOG_WARNING, "[hypershader] Failed to open recovery draft for writing: %s",
		     filePath.toUtf8().constData());
		return false;
	}

	const QJsonDocument document(draftToJson(draft));
	file.write(document.toJson(QJsonDocument::Indented));
	file.close();

	return true;
}

bool ShaderBuilderDock::removeDraftSnapshot(const QString &draftId)
{
	const QString filePath = recoveryFilePath(draftId);
	if (filePath.isEmpty())
		return false;

	if (!QFile::exists(filePath))
		return true;

	if (!QFile::remove(filePath)) {
		blog(LOG_WARNING, "[hypershader] Failed to remove recovery draft: %s", filePath.toUtf8().constData());
		return false;
	}

	return true;
}

bool ShaderBuilderDock::saveRecoveryDraft(const QString &draftId, const QString &payload)
{
	if (draftId.isEmpty())
		return false;

	const QString filePath = recoveryFilePath(draftId);
	if (filePath.isEmpty())
		return false;

	QDir dir(m_recoveryDir);
	if (!dir.exists() && !dir.mkpath(".")) {
		blog(LOG_WARNING, "[hypershader] Failed to prepare recovery directory: %s",
		     m_recoveryDir.toUtf8().constData());
		return false;
	}

	QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		blog(LOG_WARNING, "[hypershader] Failed to open recovery draft for writing: %s",
		     filePath.toUtf8().constData());
		return false;
	}

	file.write(payload.toUtf8());
	file.close();

	return true;
}

QString ShaderBuilderDock::loadRecoveryDraft(const QString &draftId) const
{
	const QString filePath = recoveryFilePath(draftId);
	if (filePath.isEmpty() || !QFile::exists(filePath))
		return QString();

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		return QString();

	return QString::fromUtf8(file.readAll());
}

bool ShaderBuilderDock::deleteRecoveryDraft(const QString &draftId)
{
	return removeDraftSnapshot(draftId);
}

void ShaderBuilderDock::setupUI()
{
	QWidget *mainWidget = new QWidget(this);
	QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);
	mainLayout->setContentsMargins(0, 0, 0, 0);
	mainLayout->setSpacing(0);

	m_tabs = new QTabWidget(mainWidget);
	m_tabs->setObjectName("shaderBuilderTabs");
	m_tabs->tabBar()->setExpanding(false);
	m_tabs->tabBar()->setMinimumHeight(28);
	// Keep the active OBS theme's palette, but restore a usable tab hit target.
	m_tabs->tabBar()->setStyleSheet("QTabBar::tab { min-height: 28px; padding: 3px 10px; }");
	setupManagerUI();

	QWidget *builderWidget = new QWidget(m_tabs);
	QVBoxLayout *builderLayout = new QVBoxLayout(builderWidget);
	builderLayout->setContentsMargins(4, 4, 4, 4);
	builderLayout->setSpacing(4);

	// Explicit labelled buttons stay readable when OBS themes shrink QToolBar
	// actions to bare 16px icons. These only affect this builder pane.
	auto *builderActions = new QHBoxLayout();
	builderActions->setSpacing(6);
	auto *openButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton),
	                                   "Open", builderWidget);
	openButton->setObjectName("shaderBuilderOpenButton");
	openButton->setMinimumHeight(28);
	openButton->setToolTip("Open a shader file");
	connect(openButton, &QPushButton::clicked, this, &ShaderBuilderDock::onOpenFileClicked);
	builderActions->addWidget(openButton);

	auto *saveButton = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton),
	                                   "Save", builderWidget);
	saveButton->setObjectName("shaderBuilderSaveButton");
	saveButton->setMinimumHeight(28);
	saveButton->setToolTip("Save shader as a new file");
	connect(saveButton, &QPushButton::clicked, this, &ShaderBuilderDock::onSaveAsClicked);
	builderActions->addWidget(saveButton);

	auto *applyButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),
	                                    "Apply", builderWidget);
	applyButton->setObjectName("shaderBuilderApplyButton");
	applyButton->setMinimumHeight(28);
	connect(applyButton, &QPushButton::clicked, this, &ShaderBuilderDock::onApplyClicked);
	builderActions->addWidget(applyButton);

	// Only re-apply an opened, unchanged file, through the revisioned native
	// compiler pipeline; active source is explicitly chosen in Shader Manager.
	auto *reloadButton = new QPushButton("Reload Saved", builderWidget);
	reloadButton->setObjectName("shaderBuilderReloadSavedButton");
	reloadButton->setMinimumHeight(28);
	reloadButton->setToolTip("Recompile the saved shader on the selected OBS instance without discarding edits");
	connect(reloadButton, &QPushButton::clicked, this, [this]() { onLiveReloadRequested(m_lastShader); });
	builderActions->addWidget(reloadButton);
	builderActions->addStretch();
	builderLayout->addLayout(builderActions);

	m_applyTargetLabel = new QLabel("Apply target: choose an instance above", builderWidget);
	m_applyTargetLabel->setWordWrap(true);
	builderLayout->addWidget(m_applyTargetLabel);
	auto *validateButton = new QPushButton("Validate Native Shader (no live changes)", builderWidget);
	validateButton->setObjectName("shaderBuilderValidateButton");
	validateButton->setMinimumHeight(28);
	validateButton->setToolTip("Compile this draft in a private native OBS source without modifying a scene.");
	connect(validateButton, &QPushButton::clicked, this, &ShaderBuilderDock::onValidateClicked);
	builderLayout->addWidget(validateButton);
	connect(m_activeShadersList, &QListWidget::currentItemChanged, this,
		[this](QListWidgetItem *current, QListWidgetItem *) {
			m_applyTargetLabel->setText(
				current ? "Apply target: " + current->text()
					: "Apply target: choose an instance above");
		});

	m_compileStatusLabel = new QLabel("No Builder revision submitted yet.", builderWidget);
	m_compileStatusLabel->setObjectName("shaderBuilderCompileStatus");
	m_compileStatusLabel->setWordWrap(true);
	builderLayout->addWidget(m_compileStatusLabel);
	m_compileDiagnostics = new QPlainTextEdit(builderWidget);
	m_compileDiagnostics->setObjectName("shaderBuilderCompilerDiagnostics");
	m_compileDiagnostics->setReadOnly(true);
	m_compileDiagnostics->setMaximumHeight(105);
	m_compileDiagnostics->setVisible(false);
	builderLayout->addWidget(m_compileDiagnostics);

	m_compilePollTimer = new QTimer(this);
	m_compilePollTimer->setInterval(250);
	connect(m_compilePollTimer, &QTimer::timeout, this, &ShaderBuilderDock::pollCompileFeedback);

	QGroupBox *aiGroup = new QGroupBox(obs_module_text("AIShaderGenerator"), builderWidget);
	QVBoxLayout *aiLayout = new QVBoxLayout(aiGroup);
	aiLayout->setContentsMargins(8, 8, 8, 8);
	aiLayout->setSpacing(6);

	m_aiPrompt = new QPlainTextEdit(aiGroup);
	m_aiPrompt->setPlaceholderText("Describe the shader you want to create...");
	m_aiPrompt->setMaximumHeight(90);
	aiLayout->addWidget(m_aiPrompt);

	QHBoxLayout *aiControls = new QHBoxLayout();
	m_aiGenerateButton = new QPushButton(obs_module_text("GenerateShader"), aiGroup);
	connect(m_aiGenerateButton, &QPushButton::clicked, this, &ShaderBuilderDock::onGenerateAiClicked);
	aiControls->addWidget(m_aiGenerateButton);
	m_aiRepairButton = new QPushButton("Prepare AI Repair", aiGroup);
	m_aiRepairButton->setObjectName("shaderBuilderPrepareRepairButton");
	m_aiRepairButton->setToolTip("Put the real compiler error and original draft into the AI prompt. You choose when to generate.");
	m_aiRepairButton->setEnabled(false);
	connect(m_aiRepairButton, &QPushButton::clicked, this, &ShaderBuilderDock::onPrepareRepairClicked);
	aiControls->addWidget(m_aiRepairButton);

	m_aiStatusLabel = new QLabel(aiGroup);
	m_aiStatusLabel->setWordWrap(true);
	QDir modelDir(m_aiModelDir);
	const bool modelReady =
		!m_aiModelDir.isEmpty() &&
		QFileInfo::exists(modelDir.filePath("tokenizer.json")) &&
		!modelDir.entryList(QStringList() << "*.tflite", QDir::Files).isEmpty();
	m_aiStatusLabel->setText(
		modelReady
			? "Local Qwen2.5-0.5B • loads on first generation"
			: "Local Qwen model not found • run tools/download_llm.sh");
	aiControls->addStretch();
	aiLayout->addLayout(aiControls);
	// A model-status string beside the Generate button forces compact docks
	// wider than OBS can provide. Give it the full row so it can wrap.
	aiLayout->addWidget(m_aiStatusLabel);

	builderLayout->addWidget(aiGroup);

	m_codeEditor = new QPlainTextEdit(builderWidget);
	QFont editorFont("monospace", 10);
	editorFont.setStyleHint(QFont::Monospace);
	m_codeEditor->setFont(editorFont);
	m_codeEditor->setTabStopDistance(4 * m_codeEditor->fontMetrics().horizontalAdvance(' '));
	m_codeEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
	m_codeEditor->setPlaceholderText("// Write or paste shader code here.\n"
					 "// Entry point: float4 hs_shade_pixel(VertData v_in) : TARGET\n"
					 "// Use Open File to load an existing shader, Save As to export.");
	builderLayout->addWidget(m_codeEditor);
	connect(m_codeEditor, &QPlainTextEdit::textChanged, this, [this]() {
		const QString current = m_codeEditor->toPlainText();
		if (!m_verifiedEditorCode.isEmpty() && current != m_verifiedEditorCode) {
			m_verifiedEditorCode.clear();
			if (!m_compilePollTimer->isActive())
				m_compileStatusLabel->setText("Draft changed since validation; run Validate again.");
		}
		if (!m_lastFailedCode.isEmpty() && current != m_lastFailedCode) {
			m_aiRepairButton->setEnabled(false);
			if (!m_compilePollTimer->isActive())
				m_compileStatusLabel->setText("Draft changed since last compiler error; validate again.");
		}
	});

	// One instance selector across performance, library and authoring.
	m_activeShadersList->setMaximumHeight(82);
	auto *targetRow = new QHBoxLayout();
	targetRow->setContentsMargins(6, 3, 6, 0);
	targetRow->addWidget(new QLabel("Target instance", mainWidget));
	targetRow->addStretch();
	auto *targetRefresh = new QPushButton(style()->standardIcon(QStyle::SP_BrowserReload), QString(), mainWidget);
	targetRefresh->setToolTip("Refresh available hypershader instances and library");
	connect(targetRefresh, &QPushButton::clicked, this, &ShaderBuilderDock::refreshManagerLists);
	targetRow->addWidget(targetRefresh);
	mainLayout->addLayout(targetRow);
	mainLayout->addWidget(m_activeShadersList);
	auto *performance = new ShaderPerformancePanel([this] {
		auto *item = m_activeShadersList->currentItem();
		return item ? item->data(Qt::UserRole).toString() : QString();
	}, m_tabs);
	connect(m_activeShadersList, &QListWidget::currentItemChanged, performance,
		[performance](QListWidgetItem *, QListWidgetItem *) { performance->refresh(true); });
	connect(m_tabs, &QTabWidget::currentChanged, performance,
		[performance](int) { if (performance->isVisible()) performance->refresh(true); });
	m_tabs->addTab(performance, "Play");
	m_tabs->addTab(m_managerWidget, "Library");
	m_tabs->addTab(builderWidget, "Builder");
	mainLayout->addWidget(m_tabs);
	setWidget(mainWidget);

	refreshManagerLists();
}

void ShaderBuilderDock::setupManagerUI()
{
    m_managerWidget = new QWidget(m_tabs);
    auto *layout = new QVBoxLayout(m_managerWidget);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *managerActions = new QHBoxLayout();
    managerActions->setSpacing(6);
    auto *refreshButton = new QPushButton(
        style()->standardIcon(QStyle::SP_BrowserReload), "Refresh", m_managerWidget);
    refreshButton->setObjectName("shaderManagerRefreshButton");
    refreshButton->setMinimumHeight(28);
    refreshButton->setToolTip("Refresh the shader library");
    connect(refreshButton, &QPushButton::clicked, this, &ShaderBuilderDock::refreshManagerLists);
    managerActions->addWidget(refreshButton);

    auto *propertiesButton = new QPushButton(
        style()->standardIcon(QStyle::SP_FileDialogDetailedView), "Properties", m_managerWidget);
    propertiesButton->setObjectName("shaderManagerPropertiesButton");
    propertiesButton->setMinimumHeight(28);
    propertiesButton->setToolTip("Open the selected active shader's properties");
    connect(propertiesButton, &QPushButton::clicked, this, [this]() {
        if (auto *item = m_activeShadersList->currentItem())
            onActiveShaderDoubleClicked(item);
    });
    managerActions->addWidget(propertiesButton);
    managerActions->addStretch();
    layout->addLayout(managerActions);

    m_activeShadersList = new QListWidget(m_managerWidget);
    m_activeShadersList->setObjectName("activeShaderInstances");
    m_activeShadersList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_activeShadersList->setMaximumHeight(132);
    m_activeShadersList->setToolTip(
        "Choose which hypershader instance the editor's Apply action will update.");
    connect(m_activeShadersList, &QListWidget::itemDoubleClicked, this,
            &ShaderBuilderDock::onActiveShaderDoubleClicked);
    layout->addWidget(m_activeShadersList);

    auto *libraryLabel = new QLabel("Shader Library", m_managerWidget);
    QFont heading = libraryLabel->font();
    heading.setBold(true);
    libraryLabel->setFont(heading);
    layout->addWidget(libraryLabel);

    m_shaderSearch = new QLineEdit(m_managerWidget);
    m_shaderSearch->setObjectName("shaderLibrarySearch");
    m_shaderSearch->setClearButtonEnabled(true);
    m_shaderSearch->setPlaceholderText("Search shader names, dates, or collections…");
    connect(m_shaderSearch, &QLineEdit::textChanged, this,
            [this](const QString &) { filterShaderFiles(); });

    m_shaderCategory = new QComboBox(m_managerWidget);
    m_shaderCategory->setObjectName("shaderLibraryCategory");
    m_shaderCategory->setMinimumContentsLength(12);
    m_shaderCategory->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_shaderCategory, &QComboBox::currentIndexChanged, this,
            [this](int) { filterShaderFiles(); });

    m_shaderViewMode = new QComboBox(m_managerWidget);
    m_shaderViewMode->setObjectName("shaderLibraryViewMode");
    m_shaderViewMode->addItem("Gallery");
    m_shaderViewMode->addItem("List");
    m_shaderViewMode->setToolTip("Switch between image previews and a compact file list.");
    connect(m_shaderViewMode, &QComboBox::currentIndexChanged, this,
            [this](int) { updateShaderViewMode(); });

    auto *filterRow = new QHBoxLayout();
    layout->addWidget(m_shaderSearch);
    filterRow->addWidget(m_shaderCategory, 2);
    filterRow->addWidget(m_shaderViewMode, 1);
    layout->addLayout(filterRow);

    m_shaderFilesList = new QListWidget(m_managerWidget);
    m_shaderFilesList->setObjectName("shaderLibraryFiles");
    m_shaderFilesList->setAlternatingRowColors(true);
    m_shaderFilesList->setUniformItemSizes(true);
    m_shaderFilesList->setWordWrap(true);
    m_shaderFilesList->setMovement(QListView::Static);
    m_shaderFilesList->setResizeMode(QListView::Adjust);
    m_shaderFilesList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_shaderFilesList, &QListWidget::itemDoubleClicked, this,
            &ShaderBuilderDock::onShaderFileDoubleClicked);
    connect(m_shaderFilesList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { updateShaderSelection(); });
    layout->addWidget(m_shaderFilesList, 1);

    m_shaderFileCount = new QLabel(m_managerWidget);
    m_shaderFileCount->setObjectName("shaderLibraryCount");
    layout->addWidget(m_shaderFileCount);

    m_shaderSelectionInfo = new QLabel(m_managerWidget);
    m_shaderSelectionInfo->setObjectName("shaderLibraryDetails");
    m_shaderSelectionInfo->setWordWrap(true);
    m_shaderSelectionInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_shaderSelectionInfo);

    m_shaderPreviewImage = new QLabel(m_managerWidget);
    m_shaderPreviewImage->setObjectName("shaderLibrarySelectedPreview");
    m_shaderPreviewImage->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    m_shaderPreviewImage->setFixedHeight(100);
    m_shaderPreviewImage->setWordWrap(true);
    layout->addWidget(m_shaderPreviewImage);

    m_openShaderButton = new QPushButton(
        style()->standardIcon(QStyle::SP_DialogOpenButton), "Open Selected in Editor", m_managerWidget);
    m_openShaderButton->setObjectName("shaderLibraryOpenEditor");
    m_openShaderButton->setEnabled(false);
    connect(m_openShaderButton, &QPushButton::clicked, this, [this]() {
        if (auto *item = m_shaderFilesList->currentItem())
            onShaderFileDoubleClicked(item);
    });
    auto *libraryActions = new QHBoxLayout();
    m_libraryApplyButton = new QPushButton("Load on target", m_managerWidget);
    m_libraryApplyButton->setObjectName("shaderLibraryApplyNative");
    m_libraryApplyButton->setEnabled(false);
    m_libraryApplyButton->setToolTip("Load the original native effect without copying or changing its relative includes");
    connect(m_libraryApplyButton, &QPushButton::clicked, this, &ShaderBuilderDock::applyLibraryShader);
    libraryActions->addWidget(m_libraryApplyButton);
    m_openShaderButton->setText("Edit");
    libraryActions->addWidget(m_openShaderButton);
    layout->addLayout(libraryActions);
    m_libraryApplyStatus = new QLabel(m_managerWidget);
    m_libraryApplyStatus->setWordWrap(true);
    m_libraryApplyStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(m_libraryApplyStatus);
    connect(m_activeShadersList, &QListWidget::currentItemChanged, this,
        [this](QListWidgetItem *, QListWidgetItem *) { updateShaderSelection(); });
    updateShaderViewMode();
    updateShaderSelection();
}

void ShaderBuilderDock::updateShaderViewMode()
{
    if (!m_shaderFilesList || !m_shaderViewMode)
        return;
    const bool gallery = m_shaderViewMode->currentIndex() == 0;
    m_shaderFilesList->setViewMode(gallery ? QListView::IconMode : QListView::ListMode);
    m_shaderFilesList->setFlow(gallery ? QListView::LeftToRight : QListView::TopToBottom);
    m_shaderFilesList->setWrapping(gallery);
    m_shaderFilesList->setIconSize(gallery ? QSize(128, 96) : QSize(40, 30));
    m_shaderFilesList->setGridSize(gallery ? QSize(162, 148) : QSize());
    m_shaderFilesList->setSpacing(gallery ? 7 : 1);
    m_shaderFilesList->setTextElideMode(Qt::ElideRight);
    for (int i = 0; i < m_shaderFilesList->count(); ++i) {
        auto *item = m_shaderFilesList->item(i);
        // The gallery has space for a concise title, while compact mode
        // retains the category for reading through large collections.
        item->setText(item->data(gallery ? Qt::UserRole + 3 : Qt::UserRole + 4).toString());
    }
    m_shaderFilesList->doItemsLayout();
}

void ShaderBuilderDock::filterShaderFiles()
{
    if (!m_shaderFilesList || !m_shaderSearch || !m_shaderCategory)
        return;
    const QString text = m_shaderSearch->text();
    const QString category = m_shaderCategory->currentData().toString();
    int visible = 0;
    int previews = 0;
    for (int i = 0; i < m_shaderFilesList->count(); ++i) {
        auto *item = m_shaderFilesList->item(i);
        hypershader_catalog_entry shader {
            item->data(Qt::UserRole).toString(),
            item->data(Qt::UserRole + 1).toString()
        };
        const bool match = shaderCatalogMatches(shader, text, category);
        item->setHidden(!match);
        if (match) {
            ++visible;
            if (!item->data(Qt::UserRole + 2).toString().isEmpty())
                ++previews;
        }
        else if (item == m_shaderFilesList->currentItem())
            m_shaderFilesList->setCurrentItem(nullptr);
    }
    if (m_shaderFileCount) {
        m_shaderFileCount->setText(QString("%1 of %2 shaders  •  %3 rendered previews")
                                   .arg(visible).arg(m_shaderFilesList->count()).arg(previews));
    }
    updateShaderSelection();
}

void ShaderBuilderDock::updateShaderSelection()
{
    const QListWidgetItem *item = m_shaderFilesList ? m_shaderFilesList->currentItem() : nullptr;
    const bool usable = item && !item->isHidden();
    if (m_openShaderButton)
        m_openShaderButton->setEnabled(usable);
    if (m_libraryApplyButton) {
        auto *target = m_activeShadersList ? m_activeShadersList->currentItem() : nullptr;
        const QString path = usable ? item->data(Qt::UserRole).toString() : QString();
        const QString role = usable ? item->data(Qt::UserRole + 3).toString().section(" / ", 0, 0) : QString();
        const QString id = target ? target->data(Qt::UserRole + 1).toString() : QString();
        const bool matching = (role == "Filter" && id == "hypershader_filter") ||
            (role == "Source" && id == "hypershader_source") ||
            (role == "Transition" && id == "hypershader_transition");
        const QString root = QFileInfo(getShaderFolderPath()).canonicalFilePath();
        m_libraryApplyButton->setEnabled(usable && matching && !root.isEmpty() &&
            path.startsWith(root + "/") && QFileInfo(path).suffix().compare("effect", Qt::CaseInsensitive) == 0 &&
            m_pendingRevisionPath.isEmpty());
    }
    if (!m_shaderSelectionInfo)
        return;
    if (!usable) {
        if (m_shaderPreviewImage) {
            m_shaderPreviewImage->setPixmap(QPixmap());
            m_shaderPreviewImage->setText("Select a shader to inspect its rendered preview");
        }
        m_shaderSelectionInfo->setText(
            "Choose a shader, then load it on the selected matching instance or open the editor.");
        return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    const QString category = item->data(Qt::UserRole + 1).toString();
    m_shaderSelectionInfo->setText(category + "  •  " + path);
    if (m_shaderPreviewImage) {
        const QString previewPath = item->data(Qt::UserRole + 2).toString();
        const QPixmap preview(previewPath);
        if (!preview.isNull()) {
            m_shaderPreviewImage->setPixmap(
                preview.scaled(QSize(160, 96), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        } else {
            m_shaderPreviewImage->setPixmap(QPixmap());
            m_shaderPreviewImage->setText("No rendered preview for this shader yet");
        }
    }
}

QString ShaderBuilderDock::getShaderFolderPath() const
{
	const char *path = obs_get_module_data_path(obs_current_module());
	return path && *path ? QDir(QString::fromUtf8(path)).filePath("shaders") : QString();
}

static bool isShaderBuilderTargetSourceId(const char *id)
{
	return id && (strcmp(id, "hypershader_filter") == 0 || strcmp(id, "hypershader_source") == 0 ||
		      strcmp(id, "hypershader_transition") == 0);
}

bool ShaderBuilderDock::enumActiveShaderSources(void *param, obs_source_t *source)
{
	auto *dock = static_cast<ShaderBuilderDock *>(param);
	if (!dock || !source)
		return true;

	const char *id = obs_source_get_unversioned_id(source);
	if (!id)
		return true;

	if (!isShaderBuilderTargetSourceId(id))
		return true;

	const char *name = obs_source_get_name(source);
	const char *uuid = obs_source_get_uuid(source);
	if (name && uuid)
		dock->addActiveShaderItem(QString::fromUtf8(name), QString::fromUtf8(uuid), QString::fromUtf8(id));

	return true;
}

void ShaderBuilderDock::addActiveShaderItem(const QString &name, const QString &uuid, const QString &sourceId)
{
	if (!m_activeShadersList)
		return;

	QString label = name;
	if (sourceId == "hypershader_filter")
		label += " [Filter]";
	else if (sourceId == "hypershader_source")
		label += " [Source]";
	else if (sourceId == "hypershader_transition")
		label += " [Transition]";

	auto *item = new QListWidgetItem(label, m_activeShadersList);
	item->setData(Qt::UserRole, uuid);
	item->setData(Qt::UserRole + 1, sourceId);
}

void ShaderBuilderDock::addShaderFileItem(const QString &path, const QString &category)
{
    if (!m_shaderFilesList)
        return;
    const QString readable = shaderCatalogDisplayLabel(path, category);
    auto *item = new QListWidgetItem(category + "  /  " + readable, m_shaderFilesList);
    item->setData(Qt::UserRole + 3, readable);
    item->setData(Qt::UserRole + 4, category + "  /  " + readable);
    item->setData(Qt::UserRole, path);
    item->setData(Qt::UserRole + 1, category);
    const QString previewPath = shaderPreviewFilePath(path);
    item->setData(Qt::UserRole + 2, previewPath);
    if (!previewPath.isEmpty())
        item->setIcon(QIcon(previewPath));
    item->setToolTip(path + (previewPath.isEmpty() ? "\nNo verified image preview" :
                            "\nActual OpenGL-rendered frame • source digest matched"));
}

void ShaderBuilderDock::refreshManagerLists()
{
    QString previousUuid, previousFile, previousCategory;
    const QSignalBlocker activeBlocker(m_activeShadersList);
    if (m_activeShadersList) {
        if (auto *item = m_activeShadersList->currentItem())
            previousUuid = item->data(Qt::UserRole).toString();
        m_activeShadersList->clear();
    }
    if (m_shaderFilesList) {
        if (auto *item = m_shaderFilesList->currentItem())
            previousFile = item->data(Qt::UserRole).toString();
        m_shaderFilesList->clear();
    }
    if (m_shaderCategory)
        previousCategory = m_shaderCategory->currentData().toString();

    obs_enum_all_sources(enumActiveShaderSources, this);

    if (!previousUuid.isEmpty()) {
        for (int i = 0; i < m_activeShadersList->count(); ++i) {
            auto *item = m_activeShadersList->item(i);
            if (item->data(Qt::UserRole).toString() == previousUuid) {
                m_activeShadersList->setCurrentItem(item);
                break;
            }
        }
    }

    QSet<QString> categories;
    for (const auto &shader : discover_hypershader_files(getShaderFolderPath())) {
        addShaderFileItem(shader.path, shader.category);
        categories.insert(shader.category);
    }
    char *revisionDir = obs_module_get_config_path(obs_current_module(), "builder-applied");
    if (revisionDir) {
        const QDir revisions(QString::fromUtf8(revisionDir));
        for (const QString &name : revisions.entryList({"*.effect"}, QDir::Files, QDir::Name)) {
            addShaderFileItem(revisions.filePath(name), "Applied revisions");
            categories.insert("Applied revisions");
        }
        bfree(revisionDir);
    }
    if (m_shaderCategory) {
        const QSignalBlocker blocker(m_shaderCategory);
        m_shaderCategory->clear();
        m_shaderCategory->addItem("All Collections", QString());
        QStringList sorted = categories.values();
        sorted.sort(Qt::CaseInsensitive);
        for (const QString &category : sorted)
            m_shaderCategory->addItem(category, category);
        const int index = m_shaderCategory->findData(previousCategory);
        m_shaderCategory->setCurrentIndex(index >= 0 ? index : 0);
    }
    updateShaderViewMode();
    filterShaderFiles();
    if (!previousFile.isEmpty()) {
        for (int i = 0; i < m_shaderFilesList->count(); ++i) {
            auto *item = m_shaderFilesList->item(i);
            if (!item->isHidden() && item->data(Qt::UserRole).toString() == previousFile) {
                m_shaderFilesList->setCurrentItem(item);
                break;
            }
        }
    }
    updateShaderSelection();
    if (m_applyTargetLabel) {
        auto *current = m_activeShadersList->currentItem();
        m_applyTargetLabel->setText(current ? "Apply target: " + current->text() : "Apply target: choose an instance above");
    }
}

void ShaderBuilderDock::openActiveShaderProperties(const QString &uuid)
{
	if (uuid.isEmpty())
		return;

	obs_source_t *source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source)
		return;

	obs_frontend_open_source_properties(source);
	obs_source_release(source);
}

void ShaderBuilderDock::openSelectedShaderFile(const QString &path)
{
	if (path.isEmpty())
		return;

	QFile file(path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		blog(LOG_WARNING, "[hypershader] Failed to open shader file: %s", path.toUtf8().constData());
		return;
	}

	QString code = QString::fromUtf8(file.readAll());
	file.close();

	if (m_codeEditor) {
		if (m_codeEditor->toPlainText() != m_loadedEditorText &&
		    QMessageBox::question(this, "Discard Unsaved Editor Changes",
		        "The current editor contains changes not saved to a file. Open another shader?",
		        QMessageBox::Discard | QMessageBox::Cancel,
		        QMessageBox::Cancel) != QMessageBox::Discard) {
			return;
		}
		m_codeEditor->setPlainText(code);
		m_loadedEditorText = code;
		m_tabs->setCurrentIndex(m_tabs->count() - 1);
	}

	m_lastShader = path;
}

void ShaderBuilderDock::onActiveShaderDoubleClicked(QListWidgetItem *item)
{
	if (!item)
		return;

	openActiveShaderProperties(item->data(Qt::UserRole).toString());
}

void ShaderBuilderDock::onShaderFileDoubleClicked(QListWidgetItem *item)
{
	if (!item)
		return;

	openSelectedShaderFile(item->data(Qt::UserRole).toString());
}

void ShaderBuilderDock::onOpenFileClicked()
{
	QString shaderPath = getShaderFolderPath();
	QString filePath =
		QFileDialog::getOpenFileName(this, "Open Shader File", shaderPath,
					     "Effect Files (*.effect);;GLSL Files (*.glsl *.frag);;All Files (*.*)");
	if (filePath.isEmpty())
		return;

	openSelectedShaderFile(filePath);
}

void ShaderBuilderDock::onSaveAsClicked()
{
	if (!m_codeEditor)
		return;

	QString code = m_codeEditor->toPlainText();
	if (code.trimmed().isEmpty()) {
		QMessageBox::warning(this, "Empty Shader", "The editor is empty. Write or load shader code first.");
		return;
	}

	QString shaderPath = getShaderFolderPath();
	QString filePath = QFileDialog::getSaveFileName(this, "Save Shader As", shaderPath,
							"Effect Files (*.effect);;All Files (*.*)");
	if (filePath.isEmpty())
		return;

	QSaveFile file(filePath);
	const QByteArray bytes = code.toUtf8();
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text) ||
	    file.write(bytes) != bytes.size() || !file.commit()) {
		file.cancelWriting();
		QMessageBox::critical(this, "Save Failed", "Could not save shader to: " + filePath);
		blog(LOG_ERROR, "[hypershader] Failed to save shader: %s", filePath.toUtf8().constData());
		return;
	}

	QFileInfo info(filePath);
	QString shaderName = info.completeBaseName();

	blog(LOG_INFO, "[hypershader] Shader saved: %s", filePath.toUtf8().constData());
	emit shaderExported(filePath, shaderName);
	m_loadedEditorText = code;

	refreshManagerLists();
	m_lastShader = filePath;
}

void ShaderBuilderDock::clearValidationSource()
{
	if (m_validationSource) {
		obs_source_release(m_validationSource);
		m_validationSource = nullptr;
	}
	if (!m_validationFilePath.isEmpty()) {
		QFile::remove(m_validationFilePath);
		m_validationFilePath.clear();
	}
}

void ShaderBuilderDock::onValidateClicked()
{
	if (m_librarySubmission) {
		m_compileStatusLabel->setText("Wait for the Library load to finish.");
		return;
	}
	if (!m_codeEditor)
		return;
	const QString code = m_codeEditor->toPlainText();
	if (code.trimmed().isEmpty()) {
		m_compileStatusLabel->setText("Nothing to validate. Enter shader code first.");
		return;
	}

	char *configDir = obs_module_get_config_path(obs_current_module(), "builder-applied");
	const QString folder = configDir ? QString::fromUtf8(configDir) : QString();
	bfree(configDir);
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		m_compileStatusLabel->setText("Validation unavailable: cannot create temporary shader folder.");
		return;
	}
	const QString revisionName = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".effect";
	const QString filePath = QDir(folder).filePath(revisionName);
	const QByteArray bytes = code.toUtf8();
	QSaveFile file(filePath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(bytes) != bytes.size() || !file.commit()) {
		file.cancelWriting();
		m_compileStatusLabel->setText("Validation unavailable: could not stage the draft.");
		return;
	}
	obs_source_t *testSource = obs_source_create_private("hypershader_filter", "Builder native validation", nullptr);
	if (!testSource) {
		QFile::remove(filePath);
		m_compileStatusLabel->setText("Validation unavailable: private OBS compiler source could not be created.");
		return;
	}
	obs_data_t *settings = obs_source_get_settings(testSource);
	obs_data_t *privateData = obs_source_get_private_settings(testSource);
	if (!settings || !privateData) {
		if (settings) obs_data_release(settings);
		if (privateData) obs_data_release(privateData);
		obs_source_release(testSource);
		QFile::remove(filePath);
		m_compileStatusLabel->setText("Validation unavailable: could not initialize private OBS source.");
		return;
	}
	const QString managedRevision = "managed/builder-applied/" + revisionName;
	if (m_compilePollTimer)
		m_compilePollTimer->stop();
	clearValidationSource();
	obs_data_set_string(privateData, "hypershader_builder_compile_path", managedRevision.toUtf8().constData());
	obs_data_set_string(privateData, "hypershader_builder_compile_state", "pending");
	obs_data_set_string(privateData, "hypershader_builder_compile_error", "");
	obs_data_release(privateData);
	obs_data_set_int(settings, "preset", 1);
	obs_data_set_int(settings, "preset_schema", 2);
	obs_data_set_string(settings, "shader_file", managedRevision.toUtf8().constData());
	obs_source_update(testSource, settings); // Isolated private-source native compilation.
	obs_data_release(settings);
	m_validationSource = testSource;
	m_validationFilePath = filePath;
	m_verifiedEditorCode.clear();
	m_pendingSourceUuid.clear();
	m_pendingRevisionPath = managedRevision;
	m_submittedEditorCode = code;
	m_lastCompileError.clear();
	m_lastFailedCode.clear();
	m_aiRepairButton->setEnabled(false);
	m_compileDiagnostics->clear();
	m_compileDiagnostics->setVisible(false);
	m_compilePollAttempts = 0;
	m_compileStatusLabel->setText("Validating with native OBS compiler; no live source changed...");
	m_compilePollTimer->start();
}

void ShaderBuilderDock::applyLibraryShader()
{
    if (!m_libraryApplyButton || !m_libraryApplyButton->isEnabled() || !m_pendingRevisionPath.isEmpty())
        return;
    auto *item = m_shaderFilesList->currentItem();
    auto *selected = m_activeShadersList->currentItem();
    if (!item || !selected || item->isHidden())
        return;
    const QString uuid = selected->data(Qt::UserRole).toString();
    const QString absolute = QFileInfo(item->data(Qt::UserRole).toString()).canonicalFilePath();
    const QString root = QFileInfo(getShaderFolderPath()).canonicalFilePath();
    if (absolute.isEmpty() || root.isEmpty() || !absolute.startsWith(root + "/") ||
        QFileInfo(absolute).suffix().compare("effect", Qt::CaseInsensitive) != 0)
        return;
    obs_source_t *target = obs_get_source_by_uuid(uuid.toUtf8().constData());
    if (!target)
        return;
    const QString id = QString::fromUtf8(obs_source_get_unversioned_id(target));
    const QString role = shaderCatalogDisplayLabel(absolute, item->data(Qt::UserRole+1).toString()).section(" / ", 0, 0);
    if (!((role == "Filter" && id == "hypershader_filter") ||
          (role == "Source" && id == "hypershader_source") ||
          (role == "Transition" && id == "hypershader_transition"))) {
        obs_source_release(target);
        return;
    }
    const QString logical = "shaders/" + QDir(root).relativeFilePath(absolute);
    obs_properties_t *props = obs_source_properties(target);
    auto *picker = props ? obs_properties_get(props, "preset") : nullptr;
    const size_t count = picker ? obs_property_list_item_count(picker) : 0;
    const bool custom = count && QString::fromUtf8(obs_property_list_item_name(picker,count-1)) == QString::fromUtf8(obs_module_text("CustomFile"));
    const long long customValue = custom ? obs_property_list_item_int(picker,count-1) : -1;
    if (props) obs_properties_destroy(props);
    auto *settings = obs_source_get_settings(target);
    auto *priv = obs_source_get_private_settings(target);
    if (!custom || !settings || !priv) {
        if (settings) obs_data_release(settings);
        if (priv) obs_data_release(priv);
        obs_source_release(target);
        m_libraryApplyStatus->setText("Native picker unavailable; no changes made.");
        return;
    }
    if (QString::fromUtf8(obs_data_get_string(settings,"shader_file")) == logical &&
        QString::fromUtf8(obs_data_get_string(priv,"hypershader_live_state")) == "ready") {
        m_libraryApplyStatus->setText("Already loaded on this instance.");
        obs_data_release(settings);obs_data_release(priv);obs_source_release(target);
        m_tabs->setCurrentIndex(0);
        return;
    }
    m_previousShaderFile = QString::fromUtf8(obs_data_get_string(settings,"shader_file"));
    m_previousPreset = obs_data_get_int(settings,"preset");
    m_previousPresetSchema = obs_data_get_int(settings,"preset_schema");
    m_pendingRevisionPath = logical;
    m_pendingSourceUuid = uuid;
    m_librarySubmission = true;
    m_compilePollAttempts = 0;
    m_submittedEditorCode.clear();
    obs_data_set_string(priv,"hypershader_builder_compile_path",logical.toUtf8().constData());
    obs_data_set_string(priv,"hypershader_builder_compile_state","pending");
    obs_data_set_string(priv,"hypershader_builder_compile_error","");
    obs_data_release(priv);
    obs_data_set_int(settings,"preset",customValue);
    obs_data_set_int(settings,"preset_schema",2);
    obs_data_set_string(settings,"shader_file",logical.toUtf8().constData());
    obs_source_update(target,settings);
    obs_data_release(settings);obs_source_release(target);
    m_libraryApplyStatus->setText("Compiling selected native effect…");
    m_compilePollTimer->start();
    updateShaderSelection();
}

void ShaderBuilderDock::onApplyClicked()
{
	if (!m_codeEditor)
		return;

	const QString code = m_codeEditor->toPlainText();
	if (code.trimmed().isEmpty()) {
		QMessageBox::warning(this, "Nothing to Apply", "Enter or load shader code first.");
		return;
	}
	if (!m_pendingRevisionPath.isEmpty() && !m_pendingSourceUuid.isEmpty()) {
		QMessageBox::information(this, "Apply In Progress",
					 "Wait for the current shader revision to finish compiling before applying another one.");
		return;
	}

	// Never guess which OBS source to modify. The manager selection is
	// deliberate, persistent across tabs, and resolved by UUID at click time.
	QListWidgetItem *selected = m_activeShadersList ? m_activeShadersList->currentItem() : nullptr;
	if (!selected) {
		QMessageBox::warning(this, "Select an Apply Target",
				     "Select a hypershader instance above first.");
		return;
	}
	const QString uuid = selected->data(Qt::UserRole).toString();
	obs_source_t *target = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!target) {
		QMessageBox::warning(this, "Shader Target Unavailable",
				     "That OBS shader instance no longer exists. Refresh Shader Manager.");
		return;
	}

	const char *sourceId = obs_source_get_unversioned_id(target);
	if (!isShaderBuilderTargetSourceId(sourceId)) {
		obs_source_release(target);
		QMessageBox::warning(this, "Wrong Apply Target", "The selection is not a hypershader instance.");
		return;
	}

	// The OBS runtime lists 'Custom File' last. Set this explicitly to keep
	// the chooser consistent with the actual shader_file setting.
	obs_properties_t *properties = obs_source_properties(target);
	obs_property_t *picker = properties ? obs_properties_get(properties, "preset") : nullptr;
	const size_t count = picker ? obs_property_list_item_count(picker) : 0;
	const bool hasCustom = count > 0 && QString::fromUtf8(obs_property_list_item_name(picker, count - 1)) ==
						    QString::fromUtf8(obs_module_text("CustomFile"));
	const long long customValue = hasCustom ? obs_property_list_item_int(picker, count - 1) : -1;
	if (properties)
		obs_properties_destroy(properties);
	if (!hasCustom) {
		obs_source_release(target);
		QMessageBox::warning(this, "Shader Picker Unavailable",
				     "Could not locate the OBS Custom File option. Nothing was changed.");
		return;
	}

	char *configDir = obs_module_get_config_path(obs_current_module(), "builder-applied");
	const QString folder = configDir ? QString::fromUtf8(configDir) : QString();
	bfree(configDir);
	if (folder.isEmpty() || !QDir().mkpath(folder)) {
		obs_source_release(target);
		QMessageBox::warning(this, "Apply Failed", "Could not create the shader output folder.");
		return;
	}
	// New filename for each revision forces the async loader to compile it.
	// Existing source shaders, other instances and old revisions are intact.
	const QString revisionName = QUuid::createUuid().toString(QUuid::WithoutBraces) + ".effect";
	const QString path = QDir(folder).filePath(revisionName);
	// Stable logical locator across Linux, Windows and macOS installations.
	const QString managedRevision = "managed/builder-applied/" + revisionName;
	QSaveFile file(path);
	const QByteArray bytes = code.toUtf8();
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(bytes) != bytes.size() || !file.commit()) {
		file.cancelWriting();
		obs_source_release(target);
		QMessageBox::warning(this, "Apply Failed", "Could not save the new shader revision.");
		return;
	}

	obs_data_t *settings = obs_source_get_settings(target);
	if (!settings) {
		QFile::remove(path);
		obs_source_release(target);
		QMessageBox::warning(this, "Apply Failed", "Could not read OBS instance settings.");
		return;
	}
	// Mark this exact per-source revision pending before the asynchronous
	// native compiler can finish. Private settings are not saved in scenes.
	obs_data_t *privateData = obs_source_get_private_settings(target);
	if (!privateData) {
		obs_data_release(settings);
		obs_source_release(target);
		QMessageBox::warning(this, "Apply Failed", "Could not monitor the native shader compilation.");
		return;
	}
	if (m_compilePollTimer)
		m_compilePollTimer->stop();
	clearValidationSource();
	obs_data_set_string(privateData, "hypershader_builder_compile_path", managedRevision.toUtf8().constData());
	obs_data_set_string(privateData, "hypershader_builder_compile_state", "pending");
	obs_data_set_string(privateData, "hypershader_builder_compile_error", "");
	obs_data_release(privateData);
	m_previousShaderFile = QString::fromUtf8(obs_data_get_string(settings, "shader_file"));
	m_previousPreset = obs_data_get_int(settings, "preset");
	m_previousPresetSchema = obs_data_get_int(settings, "preset_schema");
	obs_data_set_int(settings, "preset", customValue);
	obs_data_set_int(settings, "preset_schema", 2);
	obs_data_set_string(settings, "shader_file", managedRevision.toUtf8().constData());
	obs_source_update(target, settings); // Runtime queues actual shader compilation.
	obs_data_release(settings);
	m_pendingRevisionPath = managedRevision;
	m_pendingSourceUuid = uuid;
	m_submittedEditorCode = code;
	m_lastCompileError.clear();
	m_lastFailedCode.clear();
	m_compilePollAttempts = 0;
	m_aiRepairButton->setEnabled(false);
	m_compileDiagnostics->clear();
	m_compileDiagnostics->setVisible(false);
	m_compileStatusLabel->setText("Compiling revision with native OBS shader compiler...");
	m_compilePollTimer->start();

	blog(LOG_INFO, "[hypershader] Shader Builder submitted revision %s to instance %s", path.toUtf8().constData(),
	     uuid.toUtf8().constData());
	const QString targetName = selected->text();
	obs_source_release(target);
	refreshManagerLists();
	m_applyTargetLabel->setText("Apply submitted to " + targetName);
}

void ShaderBuilderDock::pollCompileFeedback()
{
	if (m_pendingRevisionPath.isEmpty() || (!m_validationSource && m_pendingSourceUuid.isEmpty())) {
		m_compilePollTimer->stop();
		return;
	}
	const bool preflight = m_validationSource != nullptr;
	const bool library = m_librarySubmission;
	auto finishLibrary = [this, library](const QString &message) {
		if (!library) return;
		m_librarySubmission = false;
		m_pendingSourceUuid.clear();
		m_libraryApplyStatus->setText(message);
		updateShaderSelection();
	};
	obs_source_t *target = preflight ? m_validationSource
		: obs_get_source_by_uuid(m_pendingSourceUuid.toUtf8().constData());
	if (!target) {
		m_compilePollTimer->stop();
		m_compileStatusLabel->setText("Compilation status unavailable: the target source was removed.");
		m_pendingRevisionPath.clear();
		clearValidationSource();
		finishLibrary("Target removed; no further changes made.");
		return;
	}
	obs_data_t *settings = obs_source_get_settings(target);
	obs_data_t *privateData = obs_source_get_private_settings(target);
	const QString current = settings ? QString::fromUtf8(obs_data_get_string(settings, "shader_file")) : QString();
	const QString completedPath = privateData ? QString::fromUtf8(obs_data_get_string(privateData, "hypershader_builder_compile_path")) : QString();
	const QString state = privateData ? QString::fromUtf8(obs_data_get_string(privateData, "hypershader_builder_compile_state")) : QString();
	const QString error = privateData ? QString::fromUtf8(obs_data_get_string(privateData, "hypershader_builder_compile_error")) : QString();
	if (privateData)
		obs_data_release(privateData);
	if (settings)
		obs_data_release(settings);
	if (!preflight)
		obs_source_release(target);

	if (current != m_pendingRevisionPath) {
		m_compilePollTimer->stop();
		m_compileStatusLabel->setText("Compilation superseded: the source now uses a different shader.");
		m_pendingRevisionPath.clear();
		clearValidationSource();
		finishLibrary("Selection changed elsewhere; no further changes made.");
		return;
	}
	if (completedPath == m_pendingRevisionPath && (state == "failed" || state == "succeeded")) {
		m_compilePollTimer->stop();
		const QString rejectedPath = m_pendingRevisionPath;
		m_pendingRevisionPath.clear();
		clearValidationSource();
		if (state == "succeeded") {
			if (library) {
				const auto *selected = m_activeShadersList->currentItem();
				const bool stillSelected = selected && selected->data(Qt::UserRole).toString() == m_pendingSourceUuid;
				finishLibrary("Native shader loaded successfully.");
				if (stillSelected) m_tabs->setCurrentIndex(0);
				return;
			}
			const bool unchanged = m_codeEditor && m_codeEditor->toPlainText() == m_submittedEditorCode;
			if (preflight && unchanged)
				m_verifiedEditorCode = m_submittedEditorCode;
			m_compileStatusLabel->setText(preflight
				? (unchanged ? "Native validation passed. Visual output not verified; choose Apply to update an instance."
				             : "Submitted revision passed native validation, but the editor changed. Validate again.")
				: "Submitted revision compiled successfully. Visual output is not verified.");
		} else {
			bool restored = false;
			bool validationDraftSaved = false;
			if (!preflight) {
				obs_source_t *restoreTarget = obs_get_source_by_uuid(m_pendingSourceUuid.toUtf8().constData());
				if (restoreTarget) {
					obs_data_t *restoreSettings = obs_source_get_settings(restoreTarget);
					if (restoreSettings) {
						const QString activePath = QString::fromUtf8(obs_data_get_string(restoreSettings, "shader_file"));
						if (activePath == rejectedPath) {
							obs_data_set_string(restoreSettings, "shader_file", m_previousShaderFile.toUtf8().constData());
							obs_data_set_int(restoreSettings, "preset", m_previousPreset);
							obs_data_set_int(restoreSettings, "preset_schema", m_previousPresetSchema);
							obs_source_update(restoreTarget, restoreSettings);
							obs_data_t *confirmed = obs_source_get_settings(restoreTarget);
							restored = confirmed &&
								QString::fromUtf8(obs_data_get_string(confirmed, "shader_file")) == m_previousShaderFile &&
								obs_data_get_int(confirmed, "preset") == m_previousPreset;
							if (confirmed)
								obs_data_release(confirmed);
						}
						obs_data_release(restoreSettings);
					}
					obs_source_release(restoreTarget);
				}
			}
			if (library) {
				finishLibrary((restored ? QString("Compile failed; previous shader selection restored.\n") :
					QString("Compile failed; previous selection could not be restored.\n")) + error);
				return;
			}
			m_lastCompileError = error.isEmpty() ? QStringLiteral("Unknown shader load error") : error;
			if (preflight) {
				ShaderDraft snapshot = createDraft(QString(), QStringLiteral("Failed native validation"),
							    m_submittedEditorCode, true, false);
				validationDraftSaved = saveDraftSnapshot(snapshot);
			}
			m_compileDiagnostics->setPlainText(m_lastCompileError);
			m_compileDiagnostics->setVisible(true);
			const bool unmodified = m_codeEditor && m_codeEditor->toPlainText() == m_submittedEditorCode;
			if (unmodified) {
				m_lastFailedCode = m_submittedEditorCode;
				m_aiRepairButton->setEnabled(true);
			}
			m_compileStatusLabel->setText(unmodified
				? (preflight ? (validationDraftSaved
					? "Native validation failed. Draft saved; review diagnostics or prepare an AI repair."
					: "Native validation failed. Draft remains in editor; recovery snapshot could not be saved.")
				            : restored ? "Native compilation failed; previous shader selection restored. Prepare an AI repair if desired."
				                       : "Native compilation failed. Prior selection could not be restored; inspect OBS settings.")
				: (restored ? "Native compilation failed; previous shader selection restored. The editor has changed."
				            : "Native compilation failed. The editor changed since submission; original draft is preserved."));
		}
		return;
	}
	if (++m_compilePollAttempts >= 240) { // 60 seconds; never invent a success or failure.
		m_compilePollTimer->stop();
		m_compileStatusLabel->setText("Native compilation not confirmed yet. Check the OBS log; no repair was requested.");
		m_pendingRevisionPath.clear();
		clearValidationSource();
		finishLibrary("Compilation not confirmed; inspect the OBS log.");
	}
}

void ShaderBuilderDock::onPrepareRepairClicked()
{
	if (!m_codeEditor || !m_aiPrompt || m_lastCompileError.isEmpty() || m_lastFailedCode.isEmpty())
		return;
	if (m_codeEditor->toPlainText() != m_lastFailedCode) {
		m_compileStatusLabel->setText("The draft has changed. Apply it again to obtain current compiler diagnostics.");
		m_aiRepairButton->setEnabled(false);
		return;
	}
	const QString prompt = makeShaderRepairPrompt(m_lastFailedCode, m_lastCompileError);
	if (prompt.isEmpty()) {
		m_aiStatusLabel->setText("Repair prompt unavailable: draft is empty or exceeds the supported size.");
		return;
	}
	m_aiPrompt->setPlainText(prompt);
	m_aiPrompt->setFocus();
	m_aiStatusLabel->setText("Repair prompt prepared from OBS diagnostics. Review it and click Generate when ready.");
}

QString ShaderBuilderDock::finalizeGeneratedShader(const QString &generatedCode) const
{
	QString code = generatedCode.trimmed();
	const QString fence(3, QChar(0x60));

	if (code.startsWith(fence)) {
		const int firstNewline = code.indexOf('\n');
		if (firstNewline >= 0)
			code = code.mid(firstNewline + 1);

		const int closingFence = code.lastIndexOf(fence);
		if (closingFence >= 0 && code.mid(closingFence).trimmed() == fence)
			code = code.left(closingFence).trimmed();
	}

	const QString declarations = stripShaderComments(code);
	const bool hasTechnique = QRegularExpression("\\btechnique\\s+").match(declarations).hasMatch();
	const bool hasNativePixel = QRegularExpression("\\bhs_shade_pixel\\s*\\(").match(declarations).hasMatch();
	const bool hasNativeWrapper = declarations.contains("HYPERSHADER_PIXEL_WRAPPER");
	const bool hasNativeStandard = declarations.contains("hypershader_standard.glsl");

	QString effect;
	effect += "// @label \"AI Generated Shader\"\n";
	effect += "// @description \"Generated in the hypershader Shader Builder\"\n\n";

	// Complete native effects own their techniques/passes and are not wrapped.
	if (hasTechnique) {
		effect += code;
		effect += "\n";
		return effect;
	}

	// Simple hs_shade_pixel effects use hypershader's native standard layer.
	if ((hasNativePixel || hasNativeWrapper) && !hasNativeStandard)
		effect += "#include \"hypershader_standard.glsl\"\n\n";

	effect += code;
	effect += "\n";

	if (hasNativePixel && !hasNativeWrapper)
		effect += "\nHYPERSHADER_PIXEL_WRAPPER\n";

	return effect;
}

void ShaderBuilderDock::onGenerateAiClicked()
{
	if (!m_aiPrompt || !m_aiGenerateButton || !m_aiStatusLabel || !m_llm)
		return;

	const QString prompt = m_aiPrompt->toPlainText().trimmed();
	if (prompt.isEmpty()) {
		m_aiStatusLabel->setText("Enter a shader description first.");
		return;
	}

	if (m_llmGenerating.load())
		return;

	if (m_llmThread.joinable())
		m_llmThread.join();

	m_llmGenerating.store(true);
	m_aiGenerateButton->setEnabled(false);
	m_aiStatusLabel->setText(
		m_llmModelLoaded ? "Generating shader..." : "Loading local Qwen model...");

	const QString modelDir = m_aiModelDir;

	m_llmThread = std::thread([this, prompt, modelDir]() {
		QString generated;
		QString error;

		if (!m_llm) {
			error = "Local AI generator is unavailable.";
		} else if (!m_llmModelLoaded) {
			if (modelDir.isEmpty()) {
				error = "Local Qwen model not found. Run tools/download_llm.sh.";
			} else {
				const QByteArray modelPath = modelDir.toUtf8();
				blog(LOG_INFO, "[hypershader] Loading dock LLM from: %s", modelPath.constData());
				if (llm_load_model(m_llm, modelPath.constData())) {
					m_llmModelLoaded = true;
				} else {
					const char *llmError = llm_get_error(m_llm);
					error = (llmError && llmError[0])
						? QString::fromUtf8(llmError)
						: QStringLiteral("Failed to load the local Qwen model.");
				}
			}
		}

		if (error.isEmpty()) {
			QByteArray fullPrompt(LLM_SYSTEM_PROMPT);
			fullPrompt += prompt.toUtf8();

			char *result = llm_generate(m_llm, fullPrompt.constData());
			if (result) {
				generated = QString::fromUtf8(result).trimmed();
				llm_free(result);
			}

			if (generated.isEmpty()) {
				const char *llmError = llm_get_error(m_llm);
				error = (llmError && llmError[0])
					? QString::fromUtf8(llmError)
					: QStringLiteral("The local model returned no shader code.");
			}
		}

		if (m_closing.load()) {
			m_llmGenerating.store(false);
			return;
		}

		const bool queued = QMetaObject::invokeMethod(
			this,
			[this, generated, error]() { finishAiGeneration(generated, error); },
			Qt::QueuedConnection);
		if (!queued)
			m_llmGenerating.store(false);
	});
}

void ShaderBuilderDock::finishAiGeneration(const QString &generatedCode, const QString &errorMessage)
{
	m_llmGenerating.store(false);
	if (m_aiGenerateButton)
		m_aiGenerateButton->setEnabled(true);

	if (!errorMessage.isEmpty()) {
		if (m_aiStatusLabel)
			m_aiStatusLabel->setText("Generation failed: " + errorMessage);
		blog(LOG_ERROR, "[hypershader] Dock AI generation failed: %s",
		     errorMessage.toUtf8().constData());
		return;
	}

	const QString effect = finalizeGeneratedShader(generatedCode);
	if (effect.trimmed().isEmpty()) {
		if (m_aiStatusLabel)
			m_aiStatusLabel->setText("Generation failed: empty shader.");
		return;
	}

	if (m_codeEditor) {
		const QString previousText = m_codeEditor->toPlainText();
		if (!previousText.trimmed().isEmpty()) {
			m_activeDraft.text = previousText;
			m_activeDraft.dirty = true;
			saveDraftSnapshot(m_activeDraft);
		}

		m_activeDraft = createDraft(
			QString(), QStringLiteral("AI Generated Shader"), effect, true, false);
		m_codeEditor->setPlainText(effect);
		saveDraftSnapshot(m_activeDraft);
	}

	if (m_tabs)
		m_tabs->setCurrentIndex(m_tabs->count() - 1);
	if (m_aiStatusLabel)
		m_aiStatusLabel->setText("Generated draft - review it, then Save As");

	blog(LOG_INFO, "[hypershader] AI shader draft generated in Shader Builder (%lld chars)",
	     static_cast<long long>(effect.size()));
}

QString ShaderBuilderDock::getShaderOutputPath() const
{
	char *path = obs_module_file("shaders");
	QString shaderPath;
	if (path) {
		shaderPath = QString(path);
		bfree(path);
	}
	return shaderPath;
}

void ShaderBuilderDock::onLiveReloadRequested(const QString &filePath)
{
	// Explicit request only. A saved active editor file must match its
	// current contents before we reuse Apply's revision + async compiler +
	// per-instance status/rollback workflow. Never reload arbitrary paths.
	QString reason;
	if (!m_codeEditor ||
	    !hsBuilderReloadFileEligible(filePath, m_lastShader, m_codeEditor->toPlainText(), &reason)) {
		blog(LOG_WARNING, "[hypershader] Live reload rejected: %s", reason.toUtf8().constData());
		if (m_compileStatusLabel)
			m_compileStatusLabel->setText("Reload not applied: " + reason);
		return;
	}
	onApplyClicked(); // Selects an explicit active source; no implicit broadcast.
}

void ShaderBuilderDock::onShaderExport(const QString &shaderCode, const QString &shaderName)
{
	QString safeName = shaderName;
	safeName.replace(QRegularExpression("[^a-zA-Z0-9_-]"), "_");
	if (safeName.isEmpty()) {
		safeName = "custom_shader";
	}

	QString outputPath = getShaderOutputPath() + "/" + safeName + ".effect";

	QFile file(outputPath);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		file.write(shaderCode.toUtf8());
		file.close();
		refreshManagerLists();

		blog(LOG_INFO, "[hypershader] Shader exported: %s", outputPath.toUtf8().constData());
		if (m_codeEditor && m_codeEditor->toPlainText() == shaderCode) {
			m_lastShader = outputPath;
			m_loadedEditorText = shaderCode;
		}
		emit shaderExported(outputPath, shaderName);
	} else {
		blog(LOG_ERROR, "[hypershader] Failed to export shader: %s", outputPath.toUtf8().constData());
	}
}

void ShaderBuilderDock::setupCallbacks()
{
	obs_frontend_add_save_callback(save_callback, this);
	obs_frontend_add_event_callback(frontend_event_callback, this);
}

void ShaderBuilderDock::setupHotkeys()
{
	m_hotkeyReload = obs_hotkey_register_frontend("shader_builder.reload", "Shader Builder: Reload",
						      hotkey_reload_callback, this);
}

void ShaderBuilderDock::saveSettings(obs_data_t *settings)
{
	obs_data_set_string(settings, "shader_builder.last_shader", m_lastShader.toUtf8().constData());
	blog(LOG_DEBUG, "[hypershader] Settings saved");
}

void ShaderBuilderDock::loadSettings(obs_data_t *settings)
{
	const char *lastShader = obs_data_get_string(settings, "shader_builder.last_shader");
	if (lastShader) {
		m_lastShader = QString(lastShader);
		blog(LOG_DEBUG, "[hypershader] Settings loaded: last_shader=%s", lastShader);
	}
}

void ShaderBuilderDock::onFrontendEvent(obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_EXIT:
		stopCatalogWatch();
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		style()->unpolish(this);
		style()->polish(this);
		update();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		refreshManagerLists();
		break;
	case OBS_FRONTEND_EVENT_RECORDING_STARTED:
	case OBS_FRONTEND_EVENT_RECORDING_STOPPED:
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		break;
	default:
		break;
	}
}

void ShaderBuilderDock::onHotkeyTriggered(bool pressed)
{
	if (pressed && m_tabs) {
		m_tabs->setCurrentIndex(m_tabs->count() - 1);
		show();
		raise();
		activateWindow();
	}
}

// ============================================================================
// Dock Registration (called from plugin-main.c)
// ============================================================================

static QPointer<ShaderBuilderDock> shaderBuilderDock;
static QPointer<QMenu> shaderDockMenu;
static bool deferredDockCallbackRegistered = false;

static void deferred_dock_callback(obs_frontend_event event, void *data);

static void remove_deferred_dock_callback(void)
{
	if (!deferredDockCallbackRegistered)
		return;
	deferredDockCallbackRegistered = false;
	obs_frontend_remove_event_callback(deferred_dock_callback, nullptr);
}

static void remove_shader_dock_toggle(void)
{
	if (shaderDockMenu && shaderBuilderDock)
		shaderDockMenu->removeAction(shaderBuilderDock->toggleViewAction());
	shaderDockMenu.clear();
}

static void deferred_dock_callback(obs_frontend_event event, void *data)
{
	UNUSED_PARAMETER(data);

	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING) {
		if (shaderBuilderDock) {
			remove_deferred_dock_callback();
			return;
		}

		QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (!mainWindow) {
			blog(LOG_WARNING, "[hypershader] Could not get main window for dock");
			return;
		}

		shaderBuilderDock = new ShaderBuilderDock(mainWindow);

#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
		// OBS add_dock_by_id expects QWidget content and creates a QDockWidget.
		// Passing our QDockWidget there produced two stacked title bars.
		// Register this existing dock through the dedicated custom-dock API.
		if (!obs_frontend_add_custom_qdock("ShaderBuilderDock", shaderBuilderDock)) {
			blog(LOG_ERROR, "[hypershader] OBS rejected the Shader Builder custom dock");
			shaderBuilderDock->deleteLater();
			shaderBuilderDock = nullptr;
			return;
		}
		// Unlike add_dock_by_id, AddCustomDockWidget does not install a
		// View > Docks action. Without one, closing the dock can strand it.
		QMenu *menu = mainWindow->findChild<QMenu *>("menuDocks");
		if (menu) {
			if (!menu->actions().contains(shaderBuilderDock->toggleViewAction()))
				menu->addAction(shaderBuilderDock->toggleViewAction());
			shaderDockMenu = menu;
			blog(LOG_INFO, "[hypershader] Shader Builder View > Docks toggle registered");
		} else {
			blog(LOG_WARNING, "[hypershader] View > Docks menu not available for toggle registration");
		}
#else
		obs_frontend_add_dock(shaderBuilderDock);
#endif

		blog(LOG_INFO, "[hypershader] Shader Builder dock registered");
		remove_deferred_dock_callback();
	} else if (event == OBS_FRONTEND_EVENT_THEME_CHANGED) {
		if (shaderBuilderDock)
			shaderBuilderDock->update();
	} else if (event == OBS_FRONTEND_EVENT_EXIT) {
		if (shaderBuilderDock) {
			shaderBuilderDock->stopCatalogWatch();
#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
			remove_shader_dock_toggle();
			obs_frontend_remove_dock("ShaderBuilderDock");
#endif
			shaderBuilderDock->hide();
			shaderBuilderDock->deleteLater();
			shaderBuilderDock = nullptr;
		}
	}
}

extern "C" void register_shader_builder_dock(void)
{
	const auto theme_event_handled_by_deferred_callback = OBS_FRONTEND_EVENT_THEME_CHANGED;
	UNUSED_PARAMETER(theme_event_handled_by_deferred_callback);

	if (deferredDockCallbackRegistered)
		return;
	obs_frontend_add_event_callback(deferred_dock_callback, nullptr);
	deferredDockCallbackRegistered = true;
	blog(LOG_INFO, "[hypershader] Shader Builder dock deferred to FINISHED_LOADING");
}

extern "C" void unregister_shader_builder_dock(void)
{
	remove_deferred_dock_callback();

	if (shaderBuilderDock) {
		shaderBuilderDock->stopCatalogWatch();
#if LIBOBS_API_VER >= MAKE_SEMANTIC_VERSION(30, 0, 0)
		remove_shader_dock_toggle();
		obs_frontend_remove_dock("ShaderBuilderDock");
#endif
		shaderBuilderDock->hide();
		shaderBuilderDock->deleteLater();
		shaderBuilderDock = nullptr;
		blog(LOG_INFO, "[hypershader] Shader Builder dock unregistered");
	}
}

extern "C" void shader_builder_dock_show(void)
{
	if (!shaderBuilderDock)
		return;

	if (QTabWidget *tabs = shaderBuilderDock->findChild<QTabWidget *>())
		tabs->setCurrentIndex(0);
	shaderBuilderDock->show();
	shaderBuilderDock->raise();
	shaderBuilderDock->activateWindow();
}
