#include "ui/shader_reload_request.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <QString>
#include <cassert>
#include <cstdio>

int main()
{
	QTemporaryDir dir;
	assert(dir.isValid());
	const QString active = dir.filePath("working.effect");
	const QString different = dir.filePath("other.effect");
	{
		QFile f(active);
		assert(f.open(QIODevice::WriteOnly));
		assert(f.write("first shader") == 12);
	}
	{
		QFile f(different);
		assert(f.open(QIODevice::WriteOnly));
		assert(f.write("other shader") == 12);
	}
	QString error;
	assert(hsBuilderReloadFileEligible(active, active, "first shader", &error));
	assert(!hsBuilderReloadFileEligible(different, active, "first shader", &error));
	assert(!hsBuilderReloadFileEligible(active, different, "first shader", &error));
	assert(!hsBuilderReloadFileEligible(active, active, "unsaved edits", &error));
	assert(!hsBuilderReloadFileEligible(dir.filePath("missing.effect"), active, "first shader", &error));
	assert(!hsBuilderReloadFileEligible("", active, "first shader", &error));
	assert(!hsBuilderReloadFileEligible(active, "", "first shader", &error));
	{
		QFile f(active);
		assert(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
		assert(f.write("updated shader") == 14);
	}
	assert(!hsBuilderReloadFileEligible(active, active, "first shader", &error));
	assert(hsBuilderReloadFileEligible(active, active, "updated shader", &error));
	std::puts("BUILDER_RELOAD_REQUEST_PASS=1");
}
