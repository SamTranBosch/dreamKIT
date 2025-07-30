#ifndef INSTALLEDVAPPS_H
#define INSTALLEDVAPPS_H

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
struct VappsListStruct
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
class VappsAsync;

// ───────────────────────────────────────────────────────────────
// Helper thread that watches docker-ps (unchanged behaviour)
// ───────────────────────────────────────────────────────────────
class InstalledVappsCheckThread : public QThread
{
    Q_OBJECT
public:
    explicit InstalledVappsCheckThread(VappsAsync *parent);
    void run() override;
    void triggerCheckAppStart(QString id, QString name);

signals:
    void resultReady(QString appId, bool isStarted, QString msg);

private:
    QString             m_appId;
    QString             m_appName;
    bool                m_istriggeredAppStart {false};
    VappsAsync         *m_serviceAsync {nullptr};
    QFileSystemWatcher *m_filewatcher  {nullptr};
};

// ───────────────────────────────────────────────────────────────
// The object exposed to QML
// ───────────────────────────────────────────────────────────────
class VappsAsync : public QObject
{
    Q_OBJECT
public:
    explicit VappsAsync();

    Q_INVOKABLE void initInstalledFromDB();
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
    QList<VappsListStruct>      installedVappsList;
    InstalledVappsCheckThread  *m_workerThread      {nullptr};
    QTimer                     *m_timer_apprunningcheck {nullptr};
    QProcess                   *m_installer         {nullptr};
};

#endif // INSTALLEDVAPPS_H
