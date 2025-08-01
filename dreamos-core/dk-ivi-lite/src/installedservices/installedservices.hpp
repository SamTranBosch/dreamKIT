#ifndef INSTALLEDSERVICES_H
#define INSTALLEDSERVICES_H

#include <QObject>
#include <QTextStream>
#include <QFile>
#include <QString>
#include <QThread>
#include <QList>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QProcess>

// ───────────────────────────────────────────────────────────────
// Simple DTO used by the QML ListView
// ───────────────────────────────────────────────────────────────
struct VsersListStruct
{
    QString id;
    QString category;
    QString name;
    QString author;
    QString rating;
    QString noofdownload;
    QString iconPath;
    QString foldername;
    QString packagelink;
    bool    isInstalled = false;
    bool    isSubscribed = false;
};

// ───────────────────────────────────────────────────────────────
// Forward decl
// ───────────────────────────────────────────────────────────────
class VsersAsync;

// ───────────────────────────────────────────────────────────────
// Helper thread that watches docker-ps (unchanged behaviour)
// ───────────────────────────────────────────────────────────────
class InstalledVsersCheckThread : public QThread
{
    Q_OBJECT
public:
    explicit InstalledVsersCheckThread(VsersAsync *parent);
    void notifyState(bool ok);
    void triggerCheckAppStart(QString id, QString name);
    void resetTriggerFlags();

    static QString             m_appId;
    static QString             m_appName;
    static bool                m_istriggeredAppStart;
signals:
    void resultReady(QString appId, bool isStarted, QString msg);

private:
    VsersAsync         *m_serviceAsync {nullptr};
    QFileSystemWatcher *m_filewatcher  {nullptr};
};

// ───────────────────────────────────────────────────────────────
// The object exposed to QML
// ───────────────────────────────────────────────────────────────
class VsersAsync : public QObject
{
    Q_OBJECT
public:
    explicit VsersAsync();

    Q_INVOKABLE void initInstalledFromDB();
    Q_INVOKABLE void updateInstalledList(const QJsonArray &arr);
    Q_INVOKABLE void executeServices(
        int appIdx, const QString name,
        const QString appId, bool isSubscribed);
    Q_INVOKABLE void removeServices(int index);
    Q_INVOKABLE void openAppEditor(int idx);

signals:
    void appendServicesInfoToServicesList(QString name, QString author,
                                          QString rating, QString noofdownload,
                                          QString icon, bool isInstalled,
                                          QString appId, bool isSubscribed);
    void appendLastRowToServicesList(int noOfServices);
    void clearServicesListView();
    void updateStartAppMsg(QString appId, bool isStarted, QString msg);
    void updateServicesRunningSts(QString appId, bool isStarted, int idx);

public slots:
    void handleResults(QString appId, bool isStarted, QString msg);
    void fileChanged(const QString &path);
    void checkRunningAppSts();
    void onInstallerFinished(int exitCode, QProcess::ExitStatus status);

private:
    QList<VsersListStruct>      installedVappsList;
    InstalledVsersCheckThread  *m_workerThread      {nullptr};
    QTimer                     *m_timer_apprunningcheck {nullptr};
    QProcess                   *m_installer         {nullptr};
};

#endif // INSTALLEDSERVICES_H
