#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>

class PhraseCatalog : public QObject {
	Q_OBJECT
  public:
	explicit PhraseCatalog(QObject* parent = nullptr);
	~PhraseCatalog() override;

	// Copies the installed read-only template to the user's config directory.
	// Returns true when a usable user copy exists afterwards.
	bool ensureUserCopy(QString* error = nullptr);

	// Loads the user copy, falling back to the installed template and finally to
	// the last successfully parsed catalog. Never replaces good data with a
	// failed parse.
	bool loadActive(QString* error = nullptr);

	// Explicit path load, used by tests and --check-phrases. On failure the
	// previous data is discarded so callers see an invalid catalog.
	bool load(const QString& path, QString* error = nullptr, int* errorLine = nullptr);

	bool reload();

	bool isValid() const { return _mValid; }

	QString configPath() const { return _mConfigPath; }	 // user-editable path

	QString sourcePath() const { return _mSourcePath; }	 // file actually parsed

	QString errorString() const { return _mError; }

	int errorLine() const { return _mErrorLine; }

	bool usedFallback() const { return _mUsedFallback; }

	bool hasLoadError() const { return !_mError.isEmpty(); }

	int			phraseCount(const QString& level) const;
	int			taskPhraseCount(const QString& taskId, const QString& event) const;
	int			totalPhraseCount() const;
	QStringList levels() const;

	QString phraseForScore(int score);
	QString phraseForTask(const QString& taskId, const QString& event, int score);
	QString actionPrompt();

	void setAutoReloadEnabled(bool enabled);

	static QString userConfigPath();
	static QString installedConfigPath();
	static QString missingConfigMessage();
	static QString levelForScore(int score);

	// Parses a file and produces a human-readable lint report. Returns false only
	// for structural errors; warnings are reported in report.
	static bool validateFile(const QString& path, QString* report, QString* error, int* errorLine = nullptr);

  signals:
	void changed();
	void reloadFailed(const QString& error);

  private slots:
	void onWatchTriggered();
	void reloadNow();

  private:
	struct Data {
		QHash<QString, QStringList>					levels;
		QHash<QString, QHash<QString, QStringList>> tasks;
		QStringList									actions;
	};

	bool		parseFile(const QString& path, Data& data, QString& error, int& errorLine);
	void		watchCurrent();
	void		rememberStamps();
	bool		fileStampsChanged() const;
	void		applyData(Data&& data);
	void		resetData();
	QString		pickFrom(const QString& key, const QStringList& pool);
	QString		emergencyPhrase(int score) const;
	QStringList taskPool(const QString& taskId, const QString& event) const;

	QHash<QString, QStringList>					_mLevels;
	QHash<QString, QHash<QString, QStringList>> _mTaskPhrases;
	QStringList									_mActions;
	QHash<QString, QString>						_mLastPicked;

	QString _mConfigPath;
	QString _mSourcePath;
	QString _mError;
	int		_mErrorLine		 = 0;
	bool	_mValid			 = false;
	bool	_mUsedFallback	 = false;
	bool	_mAutoReload	 = true;
	bool	_mInWatchHandler = false;

	QDateTime _mUserStamp;
	qint64	  _mUserSize = -1;
	QDateTime _mSourceStamp;
	qint64	  _mSourceSize = -1;

	QFileSystemWatcher _mWatcher;
	QTimer			   _mReloadTimer;
};
