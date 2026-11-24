#!/usr/bin/env node

const fs = require('fs');

const source = fs.readFileSync('src/ui/shader_builder_dock.cpp', 'utf8');
const cmake = fs.readFileSync('CMakeLists.txt', 'utf8');
const header = fs.readFileSync('src/ui/shader_builder_dock.hpp', 'utf8');

const apply = source.split('void ShaderBuilderDock::onApplyClicked()')[1]?.split('QString ShaderBuilderDock::finalizeGeneratedShader')[0] || '';
const open = source.split('void ShaderBuilderDock::openSelectedShaderFile')[1]?.split('void ShaderBuilderDock::onActiveShaderDoubleClicked')[0] || '';
const save = source.split('void ShaderBuilderDock::onSaveAsClicked()')[1]?.split('void ShaderBuilderDock::onApplyClicked')[0] || '';

const validate = source.split('void ShaderBuilderDock::onValidateClicked()')[1]?.split('void ShaderBuilderDock::onApplyClicked()')[0] || '';
const poll = source.split('void ShaderBuilderDock::pollCompileFeedback()')[1]?.split('void ShaderBuilderDock::onPrepareRepairClicked()')[0] || '';
const repair = source.split('void ShaderBuilderDock::onPrepareRepairClicked()')[1]?.split('QString ShaderBuilderDock::finalizeGeneratedShader')[0] || '';
const deferredRemove = source.split('static void remove_deferred_dock_callback(void)')[1]?.split('static void deferred_dock_callback')[0] || '';
const registration = source.split('extern "C" void register_shader_builder_dock(void)')[1]?.split('extern "C" void shader_builder_dock_show')[0] || '';

const checks = [
    {
        name: 'deferred frontend callback is removed only while registered',
        ok: /if\s*\(!deferredDockCallbackRegistered\)\s*return;/.test(deferredRemove) &&
            deferredRemove.includes('deferredDockCallbackRegistered = false;') &&
            deferredRemove.includes('obs_frontend_remove_event_callback(deferred_dock_callback, nullptr)') &&
            registration.includes('deferredDockCallbackRegistered = true;') &&
            registration.includes('remove_deferred_dock_callback();') &&
            !registration.includes('obs_frontend_remove_event_callback(deferred_dock_callback, nullptr)'),
    },
    {
        name: 'restores View > Docks toggle for the custom QDockWidget with unload cleanup',
        ok: source.includes('findChild<QMenu *>("menuDocks")') &&
            source.includes('menu->addAction(shaderBuilderDock->toggleViewAction())') &&
            source.includes('shaderDockMenu->removeAction(shaderBuilderDock->toggleViewAction())'),
    },
    {
        name: 'registers a QDockWidget through OBS custom-dock API, not as nested dock content',
        ok: header.includes('class ShaderBuilderDock : public QDockWidget') &&
            source.includes('obs_frontend_add_custom_qdock("ShaderBuilderDock", shaderBuilderDock)') &&
            !source.includes('obs_frontend_add_dock_by_id('),
    },
    {
        name: 'AI model status wraps below its button in a narrow Builder dock',
        ok: source.includes('aiLayout->addWidget(m_aiStatusLabel)') &&
            !source.includes('aiControls->addWidget(m_aiStatusLabel, 1)'),
    },
    {
        name: 'allows narrow docks and fits three labelled builder actions',
        ok: source.includes('setMinimumSize(280, 300)') &&
            source.includes('setMinimumContentsLength(12)') &&
            source.includes('"Open", builderWidget') &&
            source.includes('"Save", builderWidget') &&
            source.includes('"Apply", builderWidget'),
    },
    {
        name: 'keeps the tab bar and tool buttons usable at compact dock sizes',
        ok: source.includes('setMinimumHeight(28)') &&
            source.includes('shaderBuilderTabs') &&
            source.includes('shaderManagerRefreshButton') &&
            source.includes('shaderManagerPropertiesButton') &&
            source.includes('shaderBuilderOpenButton') &&
            source.includes('shaderBuilderSaveButton') &&
            source.includes('shaderBuilderApplyButton'),
    },
	{
		name: 'registers obs_frontend_event metatype',
		ok: source.includes('qRegisterMetaType<obs_frontend_event>') ||
			source.includes('qRegisterMetaType<enum obs_frontend_event>'),
	},
	{
		name: 'queues frontend events through Qt',
		ok: source.includes('Q_ARG(obs_frontend_event, event)'),
	},
	{
		name: 'installs OBS frontend API compatibility alias',
		ok: cmake.includes('obs-frontend-api.so') && cmake.includes('OBS_FRONTEND_API_COMPAT_ALIAS'),
	},
	{
		name: 'handles OBS theme changes',
		ok: /case\s+OBS_FRONTEND_EVENT_THEME_CHANGED:[\s\S]*?update\(\);/.test(source),
	},
	{
		name: 'Apply preserves rollback state by rejecting a second live submission',
		ok: apply.includes('m_pendingRevisionPath.isEmpty()') &&
			apply.includes('m_pendingSourceUuid.isEmpty()') &&
			apply.includes('Apply In Progress') &&
			apply.indexOf('Apply In Progress') < apply.indexOf('currentItem()'),
	},
	{
		name: 'Shader Manager and Apply share a target predicate for all three native types',
		ok: source.includes('isShaderBuilderTargetSourceId') &&
			!source.includes('"hypershader_audio_visualizer"') &&
			source.includes('if (!isShaderBuilderTargetSourceId(id))') &&
			apply.includes('if (!isShaderBuilderTargetSourceId(sourceId))'),
	},
	{
		name: 'Apply requires a chosen OBS instance UUID',
		ok: apply.includes('currentItem()') && apply.includes('obs_get_source_by_uuid'),
	},
	{
		name: 'Apply writes an atomic, unique shader revision',
		ok: apply.includes('QSaveFile') && apply.includes('QUuid::createUuid()') &&
			apply.includes('file.commit()') && apply.includes('builder-applied'),
	},
	{
		name: 'Apply updates exactly the selected OBS source using Custom File',
		ok: apply.includes('obs_data_set_int(settings, "preset", customValue)') &&
			apply.includes('obs_data_set_string(settings, "shader_file"') &&
			apply.includes('obs_source_update(target, settings)') &&
			!apply.includes('onShaderExport('),
	},
	{
		name: 'Apply stores a portable per-user managed revision reference',
		ok: apply.includes('managedRevision.toUtf8().constData()') &&
			apply.includes('obs_module_get_config_path(obs_current_module(), "builder-applied")') &&
			apply.includes('managed/builder-applied/') &&
			!apply.includes('obs_data_set_string(settings, "shader_file", path.toUtf8().constData())'),
	},
	{
		name: 'opening a different file protects even an empty unsaved edit',
		ok: open.includes('m_codeEditor->toPlainText() != m_loadedEditorText') &&
			!open.includes('!m_codeEditor->toPlainText().trimmed().isEmpty()'),
	},
	{
		name: 'Save As only declares the editor clean after an atomic confirmed write',
		ok: save.includes('QSaveFile file(filePath)') &&
			save.includes('file.write(bytes) != bytes.size()') &&
			save.includes('!file.commit()') &&
			save.indexOf('m_loadedEditorText = code') > save.indexOf('file.commit()'),
	},
	{
		name: 'Apply refreshes the catalog without losing target selection',
		ok: apply.includes('refreshManagerLists()') &&
			source.includes('previousUuid') && (source.includes('m_activeShadersList->setCurrentRow(i)') || source.includes('m_activeShadersList->setCurrentItem(item)')),
	},
	{
		name: 'Validate compiles a private OBS source without touching the live Apply target',
		ok: validate.includes('obs_source_create_private') &&
			validate.includes('QSaveFile') && validate.includes('m_compilePollTimer->start()') &&
			!validate.includes('m_activeShadersList') && !validate.includes('obs_get_source_by_uuid'),
	},
	{
		name: 'native compiler status is matched to the exact revision and surfaced to the user',
		ok: poll.includes('m_pendingRevisionPath') &&
			poll.includes('hypershader_builder_compile_error') &&
			poll.includes('m_compileDiagnostics->setPlainText') &&
			poll.includes('m_aiRepairButton->setEnabled(true)'),
	},
	{
		name: 'failed Apply restores only its own rejected shader selection',
		ok: poll.includes('m_previousShaderFile') &&
			poll.includes('obs_get_source_by_uuid(m_pendingSourceUuid.toUtf8().constData())') &&
			poll.includes('rejectedPath') &&
			poll.includes('obs_source_update(restoreTarget, restoreSettings)'),
	},
	{
		name: 'editing code invalidates previous validation and stale repair availability',
		ok: source.includes('Draft changed since validation') &&
			source.includes('m_verifiedEditorCode.clear()') &&
			source.includes('m_codeEditor->toPlainText() != m_lastFailedCode'),
	},
	{
		name: 'repair prepares a prompt but never runs generation without another click',
		ok: repair.includes('makeShaderRepairPrompt') &&
			repair.includes('m_aiPrompt->setPlainText(prompt)') &&
			!repair.includes('onGenerateAiClicked(') && !repair.includes('llm_generate('),
	},
	{
		name: 'prevents duplicate dock creation',
		ok: /if\s*\(shaderBuilderDock\)\s*\{[\s\S]*?return;[\s\S]*?\}/.test(source),
	},
];

let failed = 0;
for (const check of checks) {
	if (!check.ok) {
		failed++;
		console.error(`FAIL ${check.name}`);
	}
}

if (failed)
	process.exit(1);

console.log(`dock verification passed (${checks.length} checks)`);
