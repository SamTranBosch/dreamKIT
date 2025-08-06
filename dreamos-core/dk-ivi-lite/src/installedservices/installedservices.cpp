#include "installedservices.hpp"
#include "../utils/core/datamanager.hpp"
#include "../utils/k3s/installer.hpp"
#include "../utils/notifications/notificationmanager.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QDebug>
#include <QMutex>
#include <QProcessEnvironment>
#include <QRegularExpression>

using namespace Async;


// ───────────────────────────────────────────────────────────────
// Globals that already existed
// ───────────────────────────────────────────────────────────────
extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

static QString  DK_INSTALLED_SERS_FOLDER;

// ───────────────────────────────────────────────────────────────
// InstalledVsersCheckThread
// ───────────────────────────────────────────────────────────────
InstalledVsersCheckThread::InstalledVsersCheckThread(VsersAsync *parent)
{
    const QString mpDataPath = DK_INSTALLED_SERS_FOLDER + "installedservices.json";
    m_serviceAsync = parent;
    m_filewatcher  = new QFileSystemWatcher(this);

    if (m_filewatcher && QFile::exists(mpDataPath)) {
        m_filewatcher->addPath(mpDataPath);
        connect(m_filewatcher, &QFileSystemWatcher::fileChanged,
                m_serviceAsync, &VsersAsync::fileChanged);
    }
}

QString InstalledVsersCheckThread::m_appId;
QString InstalledVsersCheckThread::m_appName;
bool InstalledVsersCheckThread::m_istriggeredAppStart = false;

void InstalledVsersCheckThread::triggerCheckAppStart(QString id, QString name)
{
    m_appId   = std::move(id);
    m_appName = std::move(name);
    m_istriggeredAppStart = true;
}

void InstalledVsersCheckThread::resetTriggerFlags()
{
    m_istriggeredAppStart = false;
    m_appId.clear(); m_appName.clear();
}

void InstalledVsersCheckThread::notifyState(bool ok)
{
    if (m_istriggeredAppStart && !m_appId.isEmpty() && !m_appName.isEmpty())
    {
        const QString msg = ok
              ? tr("<b>%1</b> is started successfully.").arg(m_appName)
              : tr("<b>%1</b> is NOT started successfully.<br><br>"
                   "Please contact the car OEM for more information !!!")
                    .arg(m_appName);
        emit resultReady(m_appId, ok, msg);
        qDebug() << "[InstalledVsersCheckThread] resultReady:"
                 << m_appName << ok << msg;

        resetTriggerFlags();
    }
}

// ───────────────────────────────────────────────────────────────
// VsersAsync ctor
// ───────────────────────────────────────────────────────────────
VsersAsync::VsersAsync()
    : m_lastNodeStatus(NodeStatus::Unknown)
{
    if (DK_CONTAINER_ROOT.isEmpty())
        DK_CONTAINER_ROOT = qEnvironmentVariable("DK_CONTAINER_ROOT");
    DK_INSTALLED_SERS_FOLDER = DK_CONTAINER_ROOT + "dk_marketplace/";
    qDebug() << "[VsersAsync] DK_INSTALLED_SERS_FOLDER =" << DK_INSTALLED_SERS_FOLDER;

    // installer process (simple one-shot apply/delete)
    m_installer = new QProcess(this);
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installer, &QProcess::started,
            [](){ qDebug() << "[Installer] started"; });
    connect(m_installer,
            QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this,
            [this](QProcess::ProcessError e){
        qWarning() << "[Installer] error:" << e << m_installer->errorString();
    });
    connect(m_installer,
            QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, &VsersAsync::onInstallerFinished);

    // background thread + timer
    m_workerThread = new InstalledVsersCheckThread(this);
    connect(m_workerThread, &InstalledVsersCheckThread::resultReady,
            this, &VsersAsync::handleResults);
    m_workerThread->start();

    m_timer_apprunningcheck = new QTimer(this);
    connect(m_timer_apprunningcheck, &QTimer::timeout,
            this, &VsersAsync::checkRunningAppSts);
    m_timer_apprunningcheck->start(5000);

    // Node status monitoring timer
    m_timer_nodecheck = new QTimer(this);
    connect(m_timer_nodecheck, &QTimer::timeout,
            this, &VsersAsync::checkWorkerNodeStatus);
    m_timer_nodecheck->start(10000); // Check every 10 seconds
}

// ───────────────────────────────────────────────────────────────
// Installer finished
// ───────────────────────────────────────────────────────────────
void VsersAsync::onInstallerFinished(int exitCode,
                                     QProcess::ExitStatus status)
{
    qDebug() << "[Installer] finished code=" << exitCode
             << "status=" << status
             << "\noutput:\n" << m_installer->readAll();
}

// ───────────────────────────────────────────────────────────────
// Worker Node Status Monitoring
// ───────────────────────────────────────────────────────────────
void VsersAsync::checkWorkerNodeStatus()
{
    // Use simple QTimer approach to avoid Async framework dependency issues
    QTimer::singleShot(0, this, [this](){
        // Check node status in a lambda that can be called asynchronously
        bool nodeReady = false;
        try {
            nodeReady = K3s::Installer::nodeReady("vip", 5);
        } catch (...) {
            qWarning() << "[VsersAsync] Exception occurred while checking node status";
            nodeReady = false;
        }
        
        const NodeStatus newStatus = nodeReady ? NodeStatus::Online : NodeStatus::Offline;
        handleNodeStatusChange(newStatus);
    });
}

void VsersAsync::handleNodeStatusChange(NodeStatus newStatus)
{
    // Only notify on status change to avoid spam
    if (m_lastNodeStatus == newStatus) {
        return;
    }

    const QString nodeName = "vip"; // Worker node name
    m_lastNodeStatus = newStatus;

    switch (newStatus) {
        case NodeStatus::Online:
            qDebug() << "[VsersAsync] Worker node" << nodeName << "is now ONLINE";
            NOTIFY_SUCCESS("Worker Node Status", 
                          QString("Worker node '%1' is now online and ready").arg(nodeName));
            break;
            
        case NodeStatus::Offline:
            qWarning() << "[VsersAsync] Worker node" << nodeName << "is OFFLINE";
            NOTIFY_WARNING("Worker Node Status",
                          QString("Worker node '%1' is offline. Some services may not function properly").arg(nodeName));
            break;
            
        case NodeStatus::Unknown:
        default:
            qDebug() << "[VsersAsync] Worker node" << nodeName << "status is UNKNOWN";
            break;
    }

    // Emit signal for UI updates if needed
    emit workerNodeStatusChanged(newStatus == NodeStatus::Online);
}

// ───────────────────────────────────────────────────────────────
// Editor helper
// ───────────────────────────────────────────────────────────────
void VsersAsync::openAppEditor(int idx)
{
    if (idx < 0 || idx >= installedVappsList.size())
        return;

    const QString thisServiceFolder = DK_INSTALLED_SERS_FOLDER + installedVappsList[idx].id;
    const QString vsCodeUserData    = DK_INSTALLED_SERS_FOLDER + "vscode_user_data";
    const QString cmd =
        "mkdir -p " + vsCodeUserData + "; "
        "code " + thisServiceFolder + " --no-sandbox --user-data-dir=" + vsCodeUserData + ";";
    qDebug() << cmd;
    system(cmd.toUtf8());
}

// ───────────────────────────────────────────────────────────────
// Read installed vApps list
// ───────────────────────────────────────────────────────────────
void VsersAsync::initInstalledFromDB()
{
    emit clearServicesListView();
    installedVappsList.clear();

    DataManager dm;
    QJsonArray arr = dm.load("vehicle-service");

    updateInstalledList(arr);
}

void VsersAsync::updateInstalledList(const QJsonArray &arr)
{
    emit clearServicesListView();
    installedVappsList.clear();

    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        VsersListStruct app;
        app.id          = o.value("id").toString();
        app.name        = o.value("name").toString();
        app.author      = o.value("author").toString();
        app.rating      = o.value("rating").toString();
        app.iconPath    = o.value("thumbnail").toString();
        app.isInstalled = true;
        app.isSubscribed= o.value("subscribed").toBool();
        installedVappsList.append(app);
    }

    for (const auto &app : installedVappsList)
        appendServicesInfoToServicesList(
            app.name, app.author,
            app.rating, app.noofdownload,
            app.iconPath, app.isInstalled,
            app.id, app.isSubscribed);
}

// ─────────────────────────────────────────────────────────────
// Apply / Delete deployment with Chain pattern and node status check
// ─────────────────────────────────────────────────────────────
void VsersAsync::executeServices(int appIdx,
                                 const QString /*name*/,
                                 const QString appId,
                                 bool isSubscribed)
{
    if (appIdx < 0 || appIdx >= installedVappsList.size()) return;

    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_SERS_FOLDER, appId);

    const QStringList cmds = isSubscribed
        ? QStringList{ QString("kubectl apply -f %1").arg(deployYaml) }
        : QStringList{ QString("kubectl delete -f %1 --ignore-not-found")
                          .arg(deployYaml) };

    auto *chain = new Chain(this);

    /* ----------------------------------------------------------- *
     * Step 0: Pre-deployment node status check (worker thread)    *
     * ----------------------------------------------------------- */
    chain->add([this, appId, isSubscribed]() -> bool {
        // Only check node status for deployment (subscribe), not for deletion
        if (isSubscribed) {
            try {
                if (!K3s::Installer::nodeReady("vip", 5)) {
                    qWarning() << "[VsersAsync::executeServices] Worker node not ready for deployment";
                    
                    // Use invokeMethod to show notification in GUI thread
                    QMetaObject::invokeMethod(
                        qApp,
                        [](){
                            NOTIFY_WARNING("Deployment Warning", 
                                          "Worker node is not ready. Relevant deployment may fail");
                        },
                        Qt::QueuedConnection);
                    
                    // Don't fail the chain, just warn user
                } else {
                    qDebug() << "[VsersAsync::executeServices] Worker node is ready for deployment";
                }
            } catch (...) {
                qWarning() << "[VsersAsync::executeServices] Exception while checking node status";
            }
        }
        return true;
    });

    /* ----------------------------------------------------------- *
     * Step 1: Execute kubectl commands (GUI thread)               *
     * ----------------------------------------------------------- */
    chain->add([this, cmds = std::move(cmds)]() -> bool {
        bool okKubectl = false;
        
        QMetaObject::invokeMethod(
            qApp,                                   // run in GUI thread
            [this, cmds, &okKubectl](){
                
                K3s::Installer installer;           // local, GUI thread
                QEventLoop loop;
                
                QObject::connect(&installer, &K3s::Installer::finished,
                                 &loop,
                                 [&](bool ok){ okKubectl = ok; loop.quit(); },
                                 Qt::QueuedConnection);
                
                installer.queueAndRun(cmds);
                loop.exec();                        // wait for pipeline
            },
            Qt::BlockingQueuedConnection);          // block worker thread
        
        if (!okKubectl) {
            qWarning() << "[VsersAsync::executeServices] kubectl command failed";
            throw std::runtime_error("kubectl apply/delete failed");
        }
        
        qDebug() << "[VsersAsync::executeServices] kubectl commands executed successfully";
        return true;
    });

    /* ----------------------------------------------------------- *
     * Step 2: Update status and trigger monitoring (worker thread)*
     * ----------------------------------------------------------- */
    chain->add([this, appId, appIdx, isSubscribed]() -> bool {
        // Update subscription status
        if (appIdx < installedVappsList.size()) {
            installedVappsList[appIdx].isSubscribed = isSubscribed;
            
            // Trigger check for app start
            m_workerThread->triggerCheckAppStart(appId, installedVappsList[appIdx].name);
            
            qDebug() << "[VsersAsync::executeServices] Updated app status for" << appId 
                     << "isSubscribed:" << isSubscribed;
        }
        return true;
    });

    /* ----------------------------------------------------------- *
     * Chain completion handling                                    *
     * ----------------------------------------------------------- */
    connect(chain, &Chain::finished,
            this, [this, appId, isSubscribed](bool success){
        if (success) {
            qDebug() << "[VsersAsync::executeServices] Chain completed successfully for" << appId;
            
            // Show success notification
            const QString action = isSubscribed ? "deployed" : "stopped";
            QMetaObject::invokeMethod(
                qApp,
                [appId, action](){
                    NOTIFY_SUCCESS("Service Operation", 
                                  QString("Service '%1' %2 successfully").arg(appId, action));
                },
                Qt::QueuedConnection);
        } else {
            qWarning() << "[VsersAsync::executeServices] Chain failed for" << appId;
            
            // Show error notification
            QMetaObject::invokeMethod(
                qApp,
                [appId, isSubscribed](){
                    const QString action = isSubscribed ? "deploy" : "stop";
                    NOTIFY_ERROR("Service Operation Failed", 
                                QString("Failed to %1 service '%2'").arg(action, appId));
                },
                Qt::QueuedConnection);
        }
    });

    chain->start();
}

// ─────────────────────────────────────────────────────────────
// Remove services with Chain pattern
// ─────────────────────────────────────────────────────────────
void VsersAsync::removeServices(int index)
{
    if (index < 0 || index >= installedVappsList.size()) return;

    const QString appId = installedVappsList[index].id;
    const QString appName = installedVappsList[index].name;
    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_SERS_FOLDER, appId);

    auto *chain = new Chain(this);

    /* ----------------------------------------------------------- *
     * Step 0: Update installedservices.json (worker thread)       *
     * ----------------------------------------------------------- */
    chain->add([appId]() -> bool {
        try {
            DataManager dm;
            QJsonArray arr = dm.load("vehicle-service");
            QJsonArray out;
            
            bool found = false;
            for (const auto &v : arr) {
                if (v.isObject() && v.toObject().value("id").toString() != appId) {
                    out.append(v);
                } else {
                    found = true;
                }
            }
            
            if (found) {
                dm.save("vehicle-service", out);
                qDebug() << "[VsersAsync::removeServices] Removed" << appId << "from database";
            } else {
                qWarning() << "[VsersAsync::removeServices] App" << appId << "not found in database";
            }
            
            return true;
        } catch (const std::exception &e) {
            qWarning() << "[VsersAsync::removeServices] Database update failed:" << e.what();
            throw std::runtime_error("Failed to update database");
        }
    });

    /* ----------------------------------------------------------- *
     * Step 1: kubectl delete deployment (GUI thread)              *
     * ----------------------------------------------------------- */
    chain->add([this, deployYaml, appId]() -> bool {
        bool okKubectl = false;
        
        QMetaObject::invokeMethod(
            qApp,
            [this, deployYaml, appId, &okKubectl](){
                
                const QStringList cmds{
                    QString("kubectl delete -f %1 --ignore-not-found").arg(deployYaml)
                };
                
                K3s::Installer installer;
                QEventLoop loop;
                
                QObject::connect(&installer, &K3s::Installer::finished,
                                 &loop,
                                 [&](bool ok){ okKubectl = ok; loop.quit(); },
                                 Qt::QueuedConnection);
                
                installer.queueAndRun(cmds);
                loop.exec();
            },
            Qt::BlockingQueuedConnection);
        
        if (!okKubectl) {
            qWarning() << "[VsersAsync::removeServices] kubectl delete failed for" << appId;
            throw std::runtime_error("kubectl delete failed");
        }
        
        qDebug() << "[VsersAsync::removeServices] kubectl delete completed for" << appId;
        return true;
    });

    /* ----------------------------------------------------------- *
     * Step 2: Clean up local data structures (worker thread)      *
     * ----------------------------------------------------------- */
    chain->add([this, index, appId]() -> bool {
        // Remove from local list if still valid index
        if (index >= 0 && index < installedVappsList.size() && 
            installedVappsList[index].id == appId) {
            installedVappsList.removeAt(index);
            qDebug() << "[VsersAsync::removeServices] Removed" << appId << "from local list";
        }
        return true;
    });

    /* ----------------------------------------------------------- *
     * Chain completion handling                                    *
     * ----------------------------------------------------------- */
    connect(chain, &Chain::finished,
            this, [this, appId, appName](bool success){
        if (success) {
            qDebug() << "[VsersAsync::removeServices] Successfully removed" << appId;
            
            // Show success notification and refresh UI
            QMetaObject::invokeMethod(
                qApp,
                [this, appName](){
                    NOTIFY_SUCCESS("Service Removed", 
                                  QString("Service '%1' removed successfully").arg(appName));
                    
                    // Refresh the UI list
                    initInstalledFromDB();
                },
                Qt::QueuedConnection);
        } else {
            qWarning() << "[VsersAsync::removeServices] Failed to remove" << appId;
            
            // Show error notification
            QMetaObject::invokeMethod(
                qApp,
                [appName](){
                    NOTIFY_ERROR("Service Removal Failed", 
                                QString("Failed to remove service '%1'").arg(appName));
                },
                Qt::QueuedConnection);
        }
    });

    chain->start();
}

// ───────────────────────────────────────────────────────────────
// Result from docker-ps watcher
// ───────────────────────────────────────────────────────────────
void VsersAsync::handleResults(QString appId, bool isStarted, QString msg)
{
    emit updateStartAppMsg(appId, isStarted, msg);

    if (isStarted) {
        for (auto &app : installedVappsList)
            if (app.id == appId) {
                app.isSubscribed = true;
                break;
            }
    }
}

// ─────────────────────────────────────────────────────────────
// File changed (installedservices.json) reload list
// ─────────────────────────────────────────────────────────────
void VsersAsync::fileChanged(const QString &path)
{
    // Simple debounce approach without Async framework
    QTimer::singleShot(500, this, [this](){
        DataManager dm;
        QJsonArray arr = dm.load("vehicle-service");
        updateInstalledList(arr);
    });
}

// ───────────────────────────────────────────────────────────────
// Periodic check: is the deployment up? (Enhanced with Chain pattern)
// ───────────────────────────────────────────────────────────────
void VsersAsync::checkRunningAppSts()
{
    if (installedVappsList.isEmpty()) {
        return; // No apps to check
    }

    // Create a single chain to check all apps sequentially
    auto *chain = new Chain(this);

    /* ----------------------------------------------------------- *
     * Step 0: Check deployment status for all apps (worker thread)*
     * ----------------------------------------------------------- */
    chain->add([this]() -> bool {
        QList<AppStatusResult> results;
        
        // Check each app's deployment status
        for (int i = 0; i < installedVappsList.size(); ++i) {
            const auto &app = installedVappsList[i];
            AppStatusResult result;
            result.appId = app.id;
            result.appName = app.name;
            result.index = i;
            result.isAvailable = false;
            
            if (app.id.isEmpty()) {
                results.append(result);
                continue;
            }
            
            try {
                // Use kubectl to check deployment status
                QProcess checkProcess;
                checkProcess.start("kubectl", QStringList() 
                                  << "get" << "deployment" << app.id
                                  << "--no-headers" << "-o" 
                                  << "custom-columns=READY:.status.readyReplicas,DESIRED:.spec.replicas");
                checkProcess.waitForFinished(5000); // 5 second timeout
                
                if (checkProcess.exitCode() == 0) {
                    QString output = checkProcess.readAllStandardOutput().trimmed();
                    if (!output.isEmpty() && output != "<none>") {
                        // Use QRegularExpression instead of QRegExp for Qt6
                        QRegularExpression regex("\\s+");
                        QStringList parts = output.split(regex);
                        if (parts.size() >= 2) {
                            int ready = parts[0].toInt();
                            int desired = parts[1].toInt();
                            result.isAvailable = (ready > 0 && ready == desired);
                        }
                    }
                } else {
                    // Deployment doesn't exist or error occurred
                    result.isAvailable = false;
                }
            } catch (...) {
                qWarning() << "[VsersAsync::checkRunningAppSts] Exception while checking" << app.id;
                result.isAvailable = false;
            }
            
            results.append(result);
        }
        
        // Store results for next step
        m_lastStatusResults = results;
        return true;
    });

    /* ----------------------------------------------------------- *
     * Step 1: Update UI and notify about status changes (GUI thread)*
     * ----------------------------------------------------------- */
    chain->add([this]() -> bool {
        QMetaObject::invokeMethod(
            qApp,
            [this](){
                for (const auto &result : m_lastStatusResults) {
                    // Emit status update signal
                    emit updateServicesRunningSts(result.appId, result.isAvailable, result.index);
                    
                    // If app is available, trigger success notification
                    if (result.isAvailable) {
                        // m_workerThread->triggerCheckAppStart(result.appId, result.appName);
                        m_workerThread->notifyState(true);
                    }
                }
                
                // Clear results after processing
                m_lastStatusResults.clear();
            },
            Qt::QueuedConnection);
        
        return true;
    });

    /* ----------------------------------------------------------- *
     * Step 2: Log summary and schedule next check (worker thread) *
     * ----------------------------------------------------------- */
    chain->add([this]() -> bool {
        int runningCount = 0;
        int totalCount = installedVappsList.size();
        
        // Count running services from last results
        for (const auto &result : m_lastStatusResults) {
            if (result.isAvailable) {
                runningCount++;
            }
        }
        
        // qDebug() << "[VsersAsync::checkRunningAppSts] Status check completed:"
        //          << runningCount << "of" << totalCount << "services running";
        
        return true;
    });

    /* ----------------------------------------------------------- *
     * Chain completion handling                                    *
     * ----------------------------------------------------------- */
    connect(chain, &Chain::finished,
            this, [this](bool success){
        if (success) {
            // qDebug() << "[VsersAsync::checkRunningAppSts] Status check chain completed successfully";
        } else {
            qWarning() << "[VsersAsync::checkRunningAppSts] Status check chain failed";
            
            // Show error notification for failed status check
            QMetaObject::invokeMethod(
                qApp,
                [](){
                    NOTIFY_WARNING("Status Check", 
                                  "Failed to check service status. Will retry in next cycle.");
                },
                Qt::QueuedConnection);
        }
    });

    chain->start();
}