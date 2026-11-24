#pragma once
// Validate the saved file before routing a WebChannel reload to the existing
// revisioned builder Apply path. Never resolve an unrelated file or overwrite
// an unsaved editor draft.
#include <QFile>
#include <QFileInfo>
#include <QString>

inline bool hsBuilderReloadFileEligible(const QString &requestedPath, const QString &currentEditorFile,
					const QString &editorCode, QString *reason)
{
	auto reject = [&](const QString &message) {
		if (reason)
			*reason = message;
		return false;
	};
	if (requestedPath.isEmpty() || currentEditorFile.isEmpty())
		return reject(QStringLiteral("Open a saved shader in the builder first."));
	const QFileInfo requested(requestedPath);
	const QFileInfo current(currentEditorFile);
	if (!requested.isFile() || !current.isFile() ||
	    requested.suffix().compare(QStringLiteral("effect"), Qt::CaseInsensitive) != 0 ||
	    requested.canonicalFilePath().isEmpty() || requested.canonicalFilePath() != current.canonicalFilePath())
		return reject(QStringLiteral("Reload requires the currently opened .effect file."));
	if (requested.size() > 4 * 1024 * 1024)
		return reject(QStringLiteral("Shader file exceeds the 4 MiB reload limit."));
	QFile file(requested.canonicalFilePath());
	if (!file.open(QIODevice::ReadOnly))
		return reject(QStringLiteral("Could not read the saved shader."));
	const QByteArray bytes = file.readAll();
	if (bytes.size() != requested.size() || QString::fromUtf8(bytes) != editorCode)
		return reject(QStringLiteral("Saved shader differs from the editor; review or Save As first."));
	if (reason)
		reason->clear();
	return true;
}
