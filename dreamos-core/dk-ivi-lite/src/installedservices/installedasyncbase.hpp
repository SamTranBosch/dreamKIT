#pragma once
#include <QObject>
#include <QTimer>
#include <QJsonArray>
#include <QEventLoop>

#include "../utils/async/asyncjob.hpp"
#include "../utils/core/datamanager.hpp"
#include "../utils/notifications/notificationmanager.hpp"
#include "../utils/k3s/jobmanager.hpp"
#include "../utils/monitor/wlanmonitor.hpp"
#include "../utils/monitor/autorestartmanager.hpp"
#include "installedcheckthread.hpp"

extern QString DK_CONTAINER_ROOT;

/********************************************************************/
template<class TI, class TD>
class InstalledAsyncBase : public QObject
{
public:
    explicit InstalledAsyncBase(QObject *parent = nullptr);
    virtual ~InstalledAsyncBase();

    /* must be provided by concrete subclass */
    virtual QString dbKey()      const = 0;
    virtual QString fileName()   const = 0;
    virtual QString folderRoot() const = 0;
    virtual QString deploymentYaml(const QString &id) const = 0;

    /* ---------- API exposed to QML ------------------------------ */
    Q_INVOKABLE void initInstalledFromDB();
    Q_INVOKABLE void executeServices(int idx, const QString&, const QString id, bool subscribe);
    Q_INVOKABLE void removeServices(int idx);
    Q_INVOKABLE virtual void openAppEditor(int) { }          // optional

    // Status accessors
    bool workerNodeOnline() const;
    bool wlanConnected() const;
    
    /* restart options when internet is available */
    Q_INVOKABLE void restartSdvRuntime();
    Q_INVOKABLE void restartApplication(); 
    Q_INVOKABLE void forceRestartBoth();

protected:
    virtual void appendItemToQml(const TI&) = 0;

    /* subclasses return true if they want node monitoring */
    virtual bool wantsNodeMonitor() const { return false; }

    /* subclasses return true if they want WLAN monitoring */
    virtual bool wantsWlanMonitor() const { return false; }
    
    /* subclasses return true if they want auto-restart functionality */
    virtual bool wantsAutoRestart() const { return false; }

    /* helper used by InstalledCheckThread */
    void fileChanged(const QString&);

    /* give read access for openAppEditor() implementation */
    const QList<TI>& items() const { return m_items; }

    /* shared editor launcher (called by the wrappers below) */
    void launchVsCode(int idx);

private slots:
    void onNodeStatusChanged(bool online);
    void onWlanStatusChanged(bool connected);
    void onJobFinished(const QString &operation, bool success, const QString &message);
    void checkRunningAppSts();

private:
    void updateInstalledList(const QJsonArray&);
    void initializeMonitoring();
    void cleanupMonitoring();

    QList<TI>             m_items;
    InstalledCheckThread *m_checkThread {nullptr};
    QTimer               *m_stsTimer    {nullptr};
    
    // Extracted functionality
    WlanMonitor          *m_wlanMonitor      {nullptr};
    AutoRestartManager   *m_autoRestartMgr   {nullptr};
    K3s::JobManager      *m_jobManager       {nullptr};
    
    // Status tracking
    bool                  m_nodeOnline  {true};
    bool                  m_wlanOnline  {false};
    // Node check state
    bool                  m_nodeCheckInProgress {false};
    QDateTime             m_lastNodeCheck;

    struct StRec { QString id; bool ok; int idx; };
    QList<StRec>          m_last;
};

/********************************************************************
 *  I M P L E M E N T A T I O N
 *******************************************************************/
#include <QMetaObject>
#include <QThread>
#include <stdexcept>

/* ------------ ctor -------------------------------------------- */
template<class TI,class TD>
InstalledAsyncBase<TI,TD>::InstalledAsyncBase(QObject *parent)
    : QObject(parent)
{
    if (DK_CONTAINER_ROOT.isEmpty())
        DK_CONTAINER_ROOT = qEnvironmentVariable("DK_CONTAINER_ROOT");

    /* Initialize job manager */
    m_jobManager = new K3s::JobManager(this);
    connect(m_jobManager, &K3s::JobManager::jobFinished,
            this, &InstalledAsyncBase::onJobFinished);

    // Initialize monitoring after the base constructor knows the v-table
    QTimer::singleShot(0, this, [this](){
        initializeMonitoring();
    });

    /* deployment-status timer (always on) */
    m_stsTimer = new QTimer(this);
    connect(m_stsTimer, &QTimer::timeout,
            this, &InstalledAsyncBase::checkRunningAppSts);
    m_stsTimer->start(5'000);
}

/* ------------ dtor --------------------------------------------- */
template<class TI,class TD>
InstalledAsyncBase<TI,TD>::~InstalledAsyncBase()
{
    cleanupMonitoring();
}

/* ------------ initialize monitoring --------------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::initializeMonitoring()
{
    // 1) File monitoring (always needed)
    const QString jf = folderRoot() + "installed"
                    + QString(fileName()).remove("vehicle-") + "s.json";
    qDebug() << "[InstalledAsyncBase] Watching file:" << jf;
    m_checkThread = new InstalledCheckThread(static_cast<TD*>(this), jf, this);
    connect(m_checkThread, &InstalledCheckThread::resultReady,
            static_cast<TD*>(this), &TD::handleResults,
            Qt::QueuedConnection);
    m_checkThread->start();

    // 2) WLAN monitoring (if requested)
    if (wantsWlanMonitor()) {
        m_wlanMonitor = new WlanMonitor(this);
        m_wlanMonitor->setCheckInterval(5000); // 5 seconds
        connect(m_wlanMonitor, &WlanMonitor::connectionStatusChanged,
                this, &InstalledAsyncBase::onWlanStatusChanged);
        m_wlanMonitor->startMonitoring();
        
        qDebug() << "[InstalledAsyncBase] WLAN monitoring enabled";
    }

    // 3) Auto-restart functionality (if requested)
    if (wantsAutoRestart()) {
        m_autoRestartMgr = new AutoRestartManager(this);
        m_autoRestartMgr->setWlanMonitor(m_wlanMonitor);
        m_autoRestartMgr->setJobManager(m_jobManager);
        
        qDebug() << "[InstalledAsyncBase] Auto-restart functionality enabled";
    }

    // 4) Node monitoring (if requested) - With caching to prevent excessive calls
    if (wantsNodeMonitor()) {
        auto *nodeTimer = new QTimer(this);
        nodeTimer->setSingleShot(false);
        
        connect(nodeTimer, &QTimer::timeout, this, [this]() {
            // Skip if a check is already in progress
            if (m_nodeCheckInProgress) {
                qDebug() << "[InstalledAsyncBase] Node check already in progress, skipping";
                return;
            }
            
            // Skip if we checked recently (within last 8 seconds)
            if (m_lastNodeCheck.isValid() && 
                m_lastNodeCheck.msecsTo(QDateTime::currentDateTime()) < 8000) {
                return;
            }
            
            m_nodeCheckInProgress = true;
            m_lastNodeCheck = QDateTime::currentDateTime();
            
            auto *job = m_jobManager->checkNodeReady("vip", 3);
            
            connect(job, &Async::JobBase::finished, this, [this, job](bool) {
                bool ready = job->result();
                
                if (ready != m_nodeOnline) {
                    qDebug() << "[InstalledAsyncBase] Node status changed:" << m_nodeOnline << "->" << ready;
                    m_nodeOnline = ready;
                    onNodeStatusChanged(ready);
                }
                
                m_nodeCheckInProgress = false;
                job->deleteLater();
            });
        });
        
        nodeTimer->start(15000); // Increased to 15 seconds to reduce load
        
        qDebug() << "[InstalledAsyncBase] Node monitoring enabled (15s interval, with caching)";
    }
}

/* ------------ cleanup monitoring ------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::cleanupMonitoring()
{
    if (m_wlanMonitor) {
        m_wlanMonitor->stopMonitoring();
    }
}

/* ------------ status accessors -------------------------------- */
template<class TI,class TD>
bool InstalledAsyncBase<TI,TD>::workerNodeOnline() const
{
    return m_nodeOnline;
}

template<class TI,class TD>
bool InstalledAsyncBase<TI,TD>::wlanConnected() const
{
    return m_wlanOnline;
}

/* ------------ DB reload --------------------------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::initInstalledFromDB()
{
    emit static_cast<TD*>(this)->clearServicesListView();
    m_items.clear();
    DataManager dm;
    updateInstalledList(dm.load(dbKey()));
}

/* ------------ rebuild model & notify QML --------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::updateInstalledList(const QJsonArray &arr)
{
    emit static_cast<TD*>(this)->clearServicesListView();
    m_items.clear();

    for (auto v : arr) {
        if(!v.isObject()) continue;
        auto o = v.toObject();

        TI it;
        it.id          = o.value("id").toString();
        it.name        = o.value("name").toString();
        it.author      = o.value("author").toString();
        it.rating      = o.value("rating").toString();
        it.iconPath    = o.value("thumbnail").toString();
        it.isInstalled = true;
        it.isSubscribed= o.value("subscribed").toBool();

        m_items.append(it);
        appendItemToQml(it);
    }

    /* tell QML that the list is complete */
    static_cast<TD*>(this)->appendLastRowToServicesList(m_items.size());
}

/* ------------ QFileSystemWatcher slot (debounce) -------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::fileChanged(const QString&)
{
    // Create job without parent to avoid cross-thread issues
    auto *job = new Async::Job<QJsonArray>([=]() -> QJsonArray {
        QThread::msleep(300);
        DataManager dm; 
        qDebug() << "[InstalledAsyncBase] fileChanged, reloading from DB: " << dbKey();
        return dm.load(dbKey());
    }); // No parent
    
    connect(job, &Async::JobBase::finished, this, [this, job](bool) {
        updateInstalledList(job->result());
        job->deleteLater();
    });
}

/* shared editor launcher */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::launchVsCode(int idx)
{
    if (idx < 0 || idx >= m_items.size()) return;
    const QString folder = folderRoot() + m_items[idx].id;
    const QString data   = folderRoot() + "vscode_user_data";
    const QString cmd =
        "mkdir -p " + data + "; "
        "code " + folder + " --no-sandbox --user-data-dir=" + data + ";";
    qDebug() << cmd;
    std::system(cmd.toUtf8().constData());
}

/* ------------ (un)deploy -------------------------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::executeServices(
        int idx, const QString&, const QString id, bool subscribe)
{
    if (idx < 0 || idx >= m_items.size()) return;

    // Prepare deployment info
    K3s::JobManager::DeploymentInfo deployInfo;
    deployInfo.id = id;
    deployInfo.name = m_items[idx].name;
    deployInfo.deploymentYaml = deploymentYaml(id);
    deployInfo.subscribe = subscribe;

    // Use job manager for deployment
    auto *job = m_jobManager->deployService(deployInfo);
    
    connect(job, &Async::JobBase::finished, this, [this, idx, id, subscribe, job](bool) {
        K3s::JobManager::JobResult result = job->result();
        
        if (result.success) {
            // Update local model
            m_items[idx].isSubscribed = subscribe;
            m_checkThread->triggerCheckAppStart(id, m_items[idx].name);
            m_checkThread->notifyState(true);
        } else {
            m_checkThread->notifyState(false);
        }
        
        job->deleteLater();
    });
}

/* ------------ remove ------------------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::removeServices(int idx)
{
    if (idx < 0 || idx >= m_items.size()) return;

    const QString id   = m_items[idx].id;
    const QString yaml = deploymentYaml(id);

    // Create chain without parent to avoid cross-thread issues
    auto *chain = new Async::Chain();

    /* shared state between steps  -------------------------------- */
    auto ok      = std::make_shared<bool>(true);
    auto errText = std::make_shared<QString>();

    /* step-0 : update database file ------------------------------ */
    chain->add([=](){
        try {
            DataManager dm;
            QJsonArray in = dm.load(dbKey()), out;
            for (auto v : in)
                if (v.toObject().value("id").toString() != id)
                    out.append(v);
            dm.save(dbKey(), out);
        } catch (const std::exception &e) {
            *ok      = false;
            *errText = QString("DB error: %1").arg(e.what());
        }
        return true;
    });

    /* step-1 : use job manager for removal ----------------------- */
    chain->add([=](){
        auto *job = m_jobManager->removeService(id, yaml);
        QEventLoop loop;
        
        connect(job, &Async::JobBase::finished, &loop, [&](bool) {
            K3s::JobManager::JobResult result = job->result();
            if (!result.success) {
                *ok = false;
                *errText = result.errorMessage;
            }
            loop.quit();
        });
        
        loop.exec();
        job->deleteLater();
        return true;
    });

    /* step-2 : update local list --------------------------------- */
    chain->add([this, idx](){
        QMetaObject::invokeMethod(qApp, [this, idx]() {
            if (idx < m_items.size())
                m_items.removeAt(idx);
        }, Qt::BlockingQueuedConnection);
        return true;
    });

    /* step-3 : show result and refresh model --------------------- */
    chain->add([this, id, ok, errText](){
        QMetaObject::invokeMethod(qApp, [this, id, ok, errText]() {
            if (*ok) {
                NOTIFY_SUCCESS("Service", QString("%1 removed").arg(id));
                initInstalledFromDB();
            } else {
                NOTIFY_ERROR("Service",
                             QString("Failed to remove %1. %2")
                                 .arg(id, *errText));
            }
        }, Qt::QueuedConnection);
        return true;
    });

    connect(chain,&Async::Chain::finished,
            chain,&QObject::deleteLater);         // cleanup
    chain->start();
}

/* ------------ restart methods (delegate to AutoRestartManager) */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::restartSdvRuntime()
{
    if (m_autoRestartMgr) {
        m_autoRestartMgr->restartSdvRuntime();
    } else {
        NOTIFY_WARNING("Restart", "Auto-restart manager not available");
    }
}

template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::restartApplication()
{
    if (m_autoRestartMgr) {
        m_autoRestartMgr->restartApplication();
    } else {
        NOTIFY_WARNING("Restart", "Auto-restart manager not available");
    }
}

template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::forceRestartBoth()
{
    if (m_autoRestartMgr) {
        m_autoRestartMgr->forceRestartBoth();
    } else {
        NOTIFY_WARNING("Restart", "Auto-restart manager not available");
    }
}

/* ------------ status change handlers -------------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::onNodeStatusChanged(bool online)
{
    if (online) {
        NOTIFY_SUCCESS("Node","VIP (Vehicle Integration Platform) ~ ONLINE");
    } else {
        NOTIFY_WARNING("Node","VIP (Vehicle Integration Platform) ~ OFFLINE");
    }
    static_cast<TD*>(this)->workerNodeStatusChanged(online);
}

template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::onWlanStatusChanged(bool connected)
{
    bool wasConnected = m_wlanOnline;
    m_wlanOnline = connected;
    
    // Only notify if status actually changed
    if (wasConnected != connected) {
        if (connected) {
            NOTIFY_SUCCESS("Internet", "Connection restored - auto-restart may begin");
            qDebug() << "[InstalledAsyncBase] Internet connection restored";
        } else {
            NOTIFY_WARNING("Internet", "Connection lost - services may be affected");
            qDebug() << "[InstalledAsyncBase] Internet connection lost";
        }
    }
}

template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::onJobFinished(const QString &operation, bool success, const QString &message)
{
    qDebug() << "[InstalledAsyncBase] Job finished:" << operation 
             << "Success:" << success << "Message:" << message;
    // Additional handling can be added here if needed
}

/* ------------ deployment monitor ------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::checkRunningAppSts()
{
    if(m_items.isEmpty()) return;
    
    auto *chain = new Async::Chain(this);

    chain->add([this](){
        m_last.clear();
        for(int i = 0; i < m_items.size(); ++i) {
            auto *job = m_jobManager->checkDeploymentAvailable(m_items[i].id, 5);
            QEventLoop loop;
            bool available = false;
            
            connect(job, &Async::JobBase::finished, &loop, [&](bool) {
                available = job->result();
                loop.quit();
            });
            
            loop.exec();
            job->deleteLater();
            
            m_last.push_back({m_items[i].id, available, i});
        }
        return true;
    });

    chain->add([this](){
        QMetaObject::invokeMethod(qApp,[this](){
            for(auto &s:m_last)
                static_cast<TD*>(this)
                    ->updateServicesRunningSts(s.id,s.ok,s.idx);
        },Qt::QueuedConnection);
        return true;
    });

    connect(chain, &Async::Chain::finished, chain, &QObject::deleteLater);
    chain->start();
}