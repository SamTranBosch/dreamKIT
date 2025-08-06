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
#include <QJsonArray>
#include "../utils/async/asyncjob.hpp"

// Forward declarations
class VsersAsync;

// ───────────────────────────────────────────────────────────────
// Node Status Enum (added for node monitoring)
// ───────────────────────────────────────────────────────────────
enum class NodeStatus {
    Unknown,
    Online,
    Offline
};

Q_DECLARE_METATYPE(NodeStatus)

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

Q_DECLARE_METATYPE(VsersListStruct)

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
    Q_PROPERTY(bool workerNodeOnline READ workerNodeOnline NOTIFY workerNodeStatusChanged)

public:
    explicit VsersAsync();

    // Existing Q_INVOKABLE methods
    Q_INVOKABLE void initInstalledFromDB();
    Q_INVOKABLE void updateInstalledList(const QJsonArray &arr);
    Q_INVOKABLE void executeServices(
        int appIdx, const QString name,
        const QString appId, bool isSubscribed);
    Q_INVOKABLE void removeServices(int index);
    Q_INVOKABLE void openAppEditor(int idx);

    // Property getter for worker node status
    bool workerNodeOnline() const { return m_lastNodeStatus == NodeStatus::Online; }

signals:
    // Existing signals
    void appendServicesInfoToServicesList(QString name, QString author,
                                          QString rating, QString noofdownload,
                                          QString icon, bool isInstalled,
                                          QString appId, bool isSubscribed);
    void appendLastRowToServicesList(int noOfServices);
    void clearServicesListView();
    void updateStartAppMsg(QString appId, bool isStarted, QString msg);
    void updateServicesRunningSts(QString appId, bool isStarted, int idx);
    
    // New signal for node status changes
    void workerNodeStatusChanged(bool isOnline);

public slots:
    // Existing slots
    void handleResults(QString appId, bool isStarted, QString msg);
    void fileChanged(const QString &path);
    void checkRunningAppSts();
    void onInstallerFinished(int exitCode, QProcess::ExitStatus status);
    
    // New slot for node monitoring
    void checkWorkerNodeStatus();

private:
    // Helper methods
    void handleNodeStatusChange(NodeStatus newStatus);
    
    // Status check result structure
    struct AppStatusResult {
        QString appId;
        QString appName;
        bool isAvailable;
        int index;
    };
    
    // Existing members
    QList<VsersListStruct>      installedVappsList;
    InstalledVsersCheckThread  *m_workerThread      {nullptr};
    QTimer                     *m_timer_apprunningcheck {nullptr};
    QProcess                   *m_installer         {nullptr};
    
    // New members for node monitoring
    QTimer                     *m_timer_nodecheck   {nullptr};
    NodeStatus                  m_lastNodeStatus    {NodeStatus::Unknown};
    
    // Status checking results (temporary storage)
    QList<AppStatusResult>      m_lastStatusResults;
};

#endif // INSTALLEDSERVICES_H
