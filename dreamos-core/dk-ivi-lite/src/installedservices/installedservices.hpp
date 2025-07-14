#ifndef INSTALLEDSERVICES_H
#define INSTALLEDSERVICES_H

#include <QObject>
#include <QTextStream>
#include <QFile>
#include "QString"
#include <QThread>
#include <QList>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QProcess>

typedef struct {
    QString id;
    QString category;
    QString name;
    QString author;
    QString rating;
    QString noofdownload;
    QString iconPath;
    QString foldername;
    QString packagelink;
    bool isInstalled;
    bool isSubscribed;
} VsersListStruct;


class VsersAsync;

class InstalledVsersCheckThread : public QThread
{
    Q_OBJECT

public:
    InstalledVsersCheckThread(VsersAsync *parent);
    void run();
    void triggerCheckAppStart(QString id, QString name);

Q_SIGNALS:
    void resultReady(QString appId, bool isStarted, QString msg);

private:
    QString m_appId;
    QString m_appName;
    bool m_istriggeredAppStart = false;
    VsersAsync *m_serviceAsync = nullptr;
    QFileSystemWatcher *m_filewatcher = nullptr;
};

class VsersAsync: public QObject
{
    Q_OBJECT
public:
    VsersAsync();

    Q_INVOKABLE void initInstalledFromDB();

    Q_INVOKABLE void executeServices(int appIdx, const QString name, const QString appId, bool isSubscribed);

    Q_INVOKABLE void removeServices(const int index);

    Q_INVOKABLE void openAppEditor(int idx);
    Q_INVOKABLE void streamLogs(int appIdx);
    Q_INVOKABLE void stopLogStream();

Q_SIGNALS:
    void appendServicesInfoToServicesList(QString name, QString author, QString rating, QString noofdownload, QString icon, bool isInstalled, QString appId, bool isSubscribed);
    void appendLastRowToServicesList(const int noOfServicess);
    void clearServicesListView();
    void updateStartAppMsg(QString appId, bool isStarted, QString msg);
    void updateServicesRunningSts(QString appId, bool isStarted, int idx);
    void newLogMessage(const QString &logLine);

public Q_SLOTS:
    void handleResults(QString appId, bool isStarted, QString msg);
    void fileChanged(const QString& path);
    void checkRunningAppSts();
    void onInstallerFinished(int exitCode, QProcess::ExitStatus status);
    void onNewLogData();

private:
    QList<VsersListStruct> installedVappsList;
    InstalledVsersCheckThread *m_workerThread;
    QTimer *m_timer_apprunningcheck;
    int   m_pendingChecks = 0;    // >0 means a round is in‐progress
    QProcess* m_installer;
    QProcess* m_logStreamer = nullptr;
};

#endif //INSTALLEDSERVICES_H
