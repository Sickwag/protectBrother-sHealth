#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>

class PresetManager {
  public:
	PresetManager();

	QStringList listPresets();
	bool		save(const QString& name, const QJsonObject& data);
	QJsonObject load(const QString& name);
	bool		remove(const QString& name);
	bool		exists(const QString& name);

	// Directory where presets are stored
	QString presetsDir() const { return _mDirPath; }

  private:
	QString presetFilePath(const QString& name) const;
	QString _mDirPath;
};
