#pragma once
#include <QObject>
#include <QTimer>
#include <QProcess>
#include <QJsonArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "../utils/async/asyncjob.hpp"
#include "../utils/core/datamanager.hpp"
#include "../utils/k3s/installer.hpp"
#include "../utils/notifications/notificationmanager.hpp"
#include "installedcheckthread.hpp"

extern QString DK_CONTAINER_ROOT;

/* worker-node enum */
enum class NodeStatus { Unknown, Online, Offline };
Q_DECLARE_METATYPE(NodeStatus)

/* WLAN connection enum */
enum class WlanStatus { Unknown, Connected, Disconnected };
Q_DECLARE_METATYPE(WlanStatus)

/********************************************************************/
template<class TI, class TD>
class InstalledAsyncBase : public QObject
{
public:
    explicit InstalledAsyncBase(QObject *parent = nullptr);

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

    bool workerNodeOnline() const { return m_nodeStatus == NodeStatus::Online; }
    
    /* restart options when internet is available */
    bool wlanConnected() const { return m_wlanStatus == WlanStatus::Connected; }
    Q_INVOKABLE void restartSdvRuntime();
    Q_INVOKABLE void restartApplication(); 
    Q_INVOKABLE void forceRestartBoth();

protected:
    virtual void appendItemToQml(const TI&) = 0;

    /* subclasses return true if they want node monitoring */
    virtual bool wantsNodeMonitor() const { return false; }

    /* subclasses return true if they want WLAN monitoring */
    virtual bool wantsWlanMonitor() const { return false; }

    /* helper used by InstalledCheckThread */
    void fileChanged(const QString&);

    /* give read access for openAppEditor() implementation */
    const QList<TI>& items() const { return m_items; }

    /* shared editor launcher (called by the wrappers below) */
    void launchVsCode(int idx);

private:
    void onInstallerFinished(int, QProcess::ExitStatus);
    void checkWorkerNodeStatus();
    void checkInternetConnection();
    void showRestartOptions();
    void handleInternetStatusChange(WlanStatus newStatus);
    void autoRestartServices();
    void performApplicationRestart();
    void saveStateBeforeRestart();
    void checkRunningAppSts();
    void updateInstalledList(const QJsonArray&);

    QList<TI>             m_items;
    InstalledCheckThread *m_checkThread {nullptr};
    QTimer               *m_nodeTimer   {nullptr};
    QTimer               *m_wlanTimer   {nullptr};
    QTimer               *m_stsTimer    {nullptr};
    QProcess             *m_installer   {nullptr};
    NodeStatus            m_nodeStatus  {NodeStatus::Unknown};
    WlanStatus            m_wlanStatus  {WlanStatus::Unknown};

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

    /* kubectl helper */
    m_installer = new QProcess(this);
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installer,
            QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this,&InstalledAsyncBase::onInstallerFinished);

    // 1) create the helper AFTER the base ctor knows the v-table
    QTimer::singleShot(0, this, [this](){
        const QString jf = folderRoot() + "installed"
                        + QString(fileName()).remove("vehicle-") + "s.json";
        qDebug() << "[InstalledAsyncBase] Watching file:" << jf;
        m_checkThread = new InstalledCheckThread(static_cast<TD*>(this),
                                                jf, this);
        connect(m_checkThread, &InstalledCheckThread::resultReady,
                static_cast<TD*>(this), &TD::handleResults,
                Qt::QueuedConnection);
        m_checkThread->start();
    });

    /* create node-timer only when requested by subclass  */
    QTimer::singleShot(0, this, [this]() {
        if ( this->wantsNodeMonitor() ) {
            m_nodeTimer = new QTimer(this);
            connect(m_nodeTimer, &QTimer::timeout,
                    this, &InstalledAsyncBase<TI,TD>::checkWorkerNodeStatus);
            m_nodeTimer->start(7'000);
        }
    });

    /* create WLAN-timer only when requested by subclass */
    QTimer::singleShot(0, this, [this]() {
        if ( this->wantsWlanMonitor() ) {
            m_wlanTimer = new QTimer(this);
            connect(m_wlanTimer, &QTimer::timeout,
                    this, &InstalledAsyncBase<TI,TD>::checkInternetConnection);
            m_wlanTimer->start(5'000);  // check every 5 seconds
        }
    });

    /* deployment-status timer (always on) */
    m_stsTimer = new QTimer(this);
    connect(m_stsTimer, &QTimer::timeout,
            this,        &InstalledAsyncBase::checkRunningAppSts);
    m_stsTimer->start(5'000);
}

/* ------------ installer finished (log) ------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::onInstallerFinished(int c,QProcess::ExitStatus s)
{
    qDebug() << "[Installer] finished"<<c<<s<<"\n"<<m_installer->readAll();
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
    auto *job = new Async::Job<QJsonArray>([=](){
        QThread::msleep(300);
        DataManager dm; 
        qDebug() << "[InstalledAsyncBase] fileChanged, reloading from DB: " << dbKey();
        return dm.load(dbKey());
    }, this);

    connect(job,&Async::JobBase::finished,this,[this,job](bool){
        updateInstalledList(job->result());
        job->deleteLater();
    });
}

/* shared editor launcher (called by the wrappers below) */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>:: launchVsCode(int idx)
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

    const QStringList cmds = subscribe
        ? QStringList{ QString("kubectl apply -f %1").arg(deploymentYaml(id)) }
        : QStringList{ QString("kubectl delete -f %1 --ignore-not-found").arg(deploymentYaml(id)) };

    auto *chain = new Async::Chain(this);

    /* ------------------------------------------------------------------
     *  Shared state that every lambda can read/write
     * ----------------------------------------------------------------*/
    auto kubectlOk = std::make_shared<bool>(false);

    /* step-0  : optional node-ready check (unchanged) */
    chain->add([subscribe](){
        if (!subscribe) return true;
        bool ready=false; try{ ready = K3s::Installer::nodeReady("vip",5);}catch(...){}
        if (!ready)
            NOTIFY_WARNING("Deployment warning",
                           "Worker node not ready   deployment may fail.");
        return true;
    });

    /* step-1  : run kubectl and remember the result in our shared bool  */
    chain->add([cmds, kubectlOk](){
        bool ok = false;
        QMetaObject::invokeMethod(qApp,[&](){
            K3s::Installer inst; QEventLoop l;
            QObject::connect(&inst,&K3s::Installer::finished,&l,
                             [&](bool b){ ok = b; l.quit();});
            inst.queueAndRun(cmds); l.exec();
        }, Qt::BlockingQueuedConnection);

        *kubectlOk = ok;                 // <-- store the result here
        if(!ok) throw std::runtime_error("kubectl failed");
        return true;
    });

    /* step-2  : update local model &     start docker-ps watcher       */
    chain->add([this,idx,id,subscribe](){
        m_items[idx].isSubscribed = subscribe;
        m_checkThread->triggerCheckAppStart(id, m_items[idx].name);
        return true;
    });

    /* step-3  : inform the CheckThread about the kubectl result        */
    chain->add([this, kubectlOk](){
        m_checkThread->notifyState(*kubectlOk);
        return true;
    });

    /* finished signal (unchanged) */
    connect(chain,&Async::Chain::finished,this,
            [id,subscribe](bool ok){
        const QString act = subscribe ? "deployed" : "stopped";
        if(ok) NOTIFY_SUCCESS("Service", QString("Service '%1' %2").arg(id,act));
        else   NOTIFY_ERROR  ("Service", QString("Failed to %1 '%2'").arg(act,id));
    });
    chain->start();
}

/* ------------ remove ------------------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::removeServices(int idx)
{
    if (idx < 0 || idx >= m_items.size()) return;

    const QString id   = m_items[idx].id;
    const QString yaml = deploymentYaml(id);

    auto *chain = new Async::Chain(this);

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
        return true;                     // never abort chain
    });

    /* step-1 : run kubectl delete -------------------------------- */
    chain->add([=](){
        try {
            bool cmdOk = false;
            QMetaObject::invokeMethod(qApp,[&](){
                K3s::Installer inst;  QEventLoop loop;
                const QStringList cmd{
                    QString("kubectl delete -f %1 --ignore-not-found").arg(yaml) };
                QObject::connect(&inst,&K3s::Installer::finished,&loop,
                                 [&](bool b){ cmdOk = b; loop.quit();});
                inst.queueAndRun(cmd);  loop.exec();
            }, Qt::BlockingQueuedConnection);

            if (!cmdOk) {
                *ok      = false;
                *errText = "kubectl delete failed";
            }
        } catch (const std::exception &e) {
            *ok      = false;
            *errText = QString("kubectl exception: %1").arg(e.what());
        }
        return true;
    });

    /* step-2 : update local list --------------------------------- */
    chain->add([this,idx](){
        if (idx < m_items.size())
            m_items.removeAt(idx);
        return true;
    });

    /* step-3 : show result and refresh model --------------------- */
    chain->add([this,id,ok,errText](){
        if (*ok) {
            NOTIFY_SUCCESS("Service", QString("%1 removed").arg(id));
            initInstalledFromDB();                // reload UI model
        } else {
            NOTIFY_ERROR("Service",
                         QString("Failed to remove %1. %2")
                             .arg(id,*errText));
        }
        return true;
    });

    connect(chain,&Async::Chain::finished,
            chain,&QObject::deleteLater);         // cleanup
    chain->start();
}

/* ------------ node monitor ------------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::checkWorkerNodeStatus()
{
    bool ok=false; try{ ok=K3s::Installer::nodeReady("vip",5);}catch(...){}
    auto st = ok?NodeStatus::Online:NodeStatus::Offline;
    if(st==m_nodeStatus) return;
    m_nodeStatus=st;
    if(ok) NOTIFY_SUCCESS("Node","vip online");
    else   NOTIFY_WARNING("Node","vip offline");
    static_cast<TD*>(this)->workerNodeStatusChanged(ok);
}

/* ------------ internet connection monitor --------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::checkInternetConnection()
{
    // Create a network access manager if it doesn't exist
    static QNetworkAccessManager* networkManager = nullptr;
    if (!networkManager) {
        networkManager = new QNetworkAccessManager(this);
    }

    // Try multiple endpoints for better reliability
    static QStringList testUrls = {
        // "http://8.8.8.8",           // Google DNS (IP-based, no DNS lookup needed)
        // "http://1.1.1.1",           // Cloudflare DNS (IP-based)
        "http://www.google.com",    // Domain-based as fallback
        // "http://httpbin.org/get"    // Alternative test endpoint
    };
    
    static int currentUrlIndex = 0;
    QString testUrl = testUrls[currentUrlIndex % testUrls.size()];
    currentUrlIndex++;

    // Fix: Use brace initialization to avoid most vexing parse
    QUrl url{testUrl};
    QNetworkRequest request{url};
    
    request.setRawHeader("User-Agent", "sdv-runtime/1.0");
    
    // Qt6 redirect policy
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    
    // Shorter timeout for faster detection
    request.setTransferTimeout(3000); // 3 seconds timeout
    
    QNetworkReply* reply = networkManager->head(request);
    
    // Handle the reply
    connect(reply, &QNetworkReply::finished, this, [this, reply, testUrl]() {
        WlanStatus newStatus = WlanStatus::Disconnected;
        
        if (reply->error() == QNetworkReply::NoError || 
            reply->error() == QNetworkReply::ContentNotFoundError) {
            // Success or 404 both indicate internet connectivity
            newStatus = WlanStatus::Connected;
            // qDebug() << "[InternetCheck] Connection successful via:" << testUrl;
        } else {
            // qDebug() << "[InternetCheck] Connection failed via" << testUrl 
            //          << "Error:" << reply->error() << reply->errorString();
            newStatus = WlanStatus::Disconnected;
        }
        
        // Handle status change
        this->handleInternetStatusChange(newStatus);
        
        reply->deleteLater();
    });
    
    // Handle timeout and errors
    connect(reply, &QNetworkReply::errorOccurred, this, 
            [this, reply, testUrl](QNetworkReply::NetworkError error) {
        qDebug() << "[InternetCheck] Network error via" << testUrl 
                 << "Error:" << error << reply->errorString();
        
        // Don't immediately mark as disconnected on single failure
        // Let the finished handler deal with it
    });
}

/* ------------ handle internet status change ------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::handleInternetStatusChange(WlanStatus newStatus)
{
    if (newStatus == m_wlanStatus) return; // No change
    
    WlanStatus oldStatus = m_wlanStatus;
    m_wlanStatus = newStatus;
    
    if (newStatus == WlanStatus::Connected) {
        NOTIFY_SUCCESS("Internet", "Internet connection restored");
        qDebug() << "[InternetCheck] Internet connection restored";
        
        // Only trigger restart options if we were previously disconnected
        if (oldStatus == WlanStatus::Disconnected) {
            // Auto-restart both services when internet comes back
            QTimer::singleShot(2000, this, [this]() {
                this->autoRestartServices();
            });
        }
    } else {
        NOTIFY_WARNING("Internet", "Internet connection lost");
        qDebug() << "[InternetCheck] Internet connection lost";
    }
    
    // Notify derived class about the status change
    // static_cast<TD*>(this)->internetConnectionStatusChanged(newStatus == WlanStatus::Connected);
}

/* ------------ auto restart services when internet restored ---- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::autoRestartServices()
{
    static bool restartInProgress = false;
    static uint32_t restartCycleCnt = 5;
    if (restartInProgress) {
        qDebug() << "[AutoRestart] Restart already in progress, skipping";
        return;
    }
    if (--restartCycleCnt == 0) {
        NOTIFY_WARNING("Auto Restart", "Restart already reach the threshold");
        qDebug() << "[AutoRestart] Restart already reach the threshold";
        return;
    }
    
    restartInProgress = true;
    
    NOTIFY_INFO("Auto Restart", "Auto-restarting services due to internet restoration...");
    qDebug() << "[AutoRestart] Starting auto-restart sequence";
    
    auto *chain = new Async::Chain(this);
    auto sdvSuccess = std::make_shared<bool>(false);
    auto errorMsg = std::make_shared<QString>();
    auto deploymentExists = std::make_shared<bool>(false);

    // Step 1: Check if SDV runtime deployment exists
    chain->add([this, deploymentExists, errorMsg]() {
        try {
            qDebug() << "[AutoRestart] Step 1: Checking if SDV deployment exists";
            
            bool exists = false;
            QMetaObject::invokeMethod(qApp, [&]() {
                K3s::Installer inst;
                QEventLoop loop;
                const QStringList checkCmd{
                    "kubectl get deployment sdv-runtime -n default --no-headers"
                };
                QObject::connect(&inst, &K3s::Installer::finished, &loop,
                               [&](bool b) { exists = b; loop.quit(); });
                inst.queueAndRun(checkCmd);
                loop.exec();
            }, Qt::BlockingQueuedConnection);

            *deploymentExists = exists;
            if (!exists) {
                qDebug() << "[AutoRestart] SDV deployment not found, will skip restart";
            } else {
                qDebug() << "[AutoRestart] SDV deployment found, proceeding with restart";
            }
            return true;
        } catch (const std::exception &e) {
            *errorMsg = QString("Deployment check error: %1").arg(e.what());
            qDebug() << "[AutoRestart] Exception during deployment check:" << e.what();
            return true; // Continue anyway
        }
    });

    // Step 2: Scale down deployment to 0 replicas (force stop)
    chain->add([this, deploymentExists, sdvSuccess, errorMsg]() {
        if (!*deploymentExists) {
            qDebug() << "[AutoRestart] Step 2: Skipping scale down - deployment doesn't exist";
            *sdvSuccess = true;
            return true;
        }

        try {
            qDebug() << "[AutoRestart] Step 2: Scaling down deployment to 0 replicas";
            
            bool scaleOk = false;
            QMetaObject::invokeMethod(qApp, [&]() {
                K3s::Installer inst;
                QEventLoop loop;
                const QStringList scaleCmd{
                    "kubectl scale deployment sdv-runtime --replicas=0 -n default"
                };
                QObject::connect(&inst, &K3s::Installer::finished, &loop,
                               [&](bool b) { scaleOk = b; loop.quit(); });
                inst.queueAndRun(scaleCmd);
                loop.exec();
            }, Qt::BlockingQueuedConnection);

            if (!scaleOk) {
                *errorMsg = "Failed to scale down deployment";
                qDebug() << "[AutoRestart] Failed to scale down deployment";
                return true; // Continue anyway, maybe it's already down
            } else {
                qDebug() << "[AutoRestart] Successfully scaled down deployment";
            }
            
            return true;
        } catch (const std::exception &e) {
            *errorMsg = QString("Scale down error: %1").arg(e.what());
            qDebug() << "[AutoRestart] Exception during scale down:" << e.what();
            return true; // Continue anyway
        }
    });

    // Step 3: Wait for pods to terminate completely
    chain->add([this, deploymentExists, errorMsg]() {
        if (!*deploymentExists) {
            qDebug() << "[AutoRestart] Step 3: Skipping pod termination wait - deployment doesn't exist";
            return true;
        }

        try {
            qDebug() << "[AutoRestart] Step 3: Waiting for pods to terminate";
            
            // Wait up to 30 seconds for pods to terminate
            for (int i = 0; i < 30; ++i) {
                bool podsRunning = false;
                QMetaObject::invokeMethod(qApp, [&]() {
                    K3s::Installer inst;
                    QEventLoop loop;
                    const QStringList checkCmd{
                        "kubectl get pods -l app=sdv-runtime -n default --no-headers"
                    };
                    QObject::connect(&inst, &K3s::Installer::finished, &loop,
                                   [&](bool b) { podsRunning = b; loop.quit(); });
                    inst.queueAndRun(checkCmd);
                    loop.exec();
                }, Qt::BlockingQueuedConnection);

                if (!podsRunning) {
                    qDebug() << "[AutoRestart] All pods terminated after" << i << "seconds";
                    break;
                }
                
                qDebug() << "[AutoRestart] Waiting for pods to terminate... (" << (i+1) << "/30)";
                QThread::msleep(1000); // Wait 1 second
            }
            
            return true;
        } catch (const std::exception &e) {
            *errorMsg = QString("Pod termination wait error: %1").arg(e.what());
            qDebug() << "[AutoRestart] Exception during pod termination wait:" << e.what();
            return true; // Continue anyway
        }
    });

    // Step 4: Check and clear any stuck resources
    chain->add([this, deploymentExists, errorMsg]() {
        if (!*deploymentExists) {
            qDebug() << "[AutoRestart] Step 4: Skipping resource cleanup - deployment doesn't exist";
            return true;
        }

        try {
            qDebug() << "[AutoRestart] Step 4: Cleaning up any stuck resources";
            
            // Force delete any remaining pods
            QMetaObject::invokeMethod(qApp, [&]() {
                K3s::Installer inst;
                QEventLoop loop;
                const QStringList cleanupCmd{
                    "kubectl delete pods -l app=sdv-runtime -n default --force --grace-period=0"
                };
                QObject::connect(&inst, &K3s::Installer::finished, &loop,
                               [&](bool) { loop.quit(); }); // Don't care about result
                inst.queueAndRun(cleanupCmd);
                loop.exec();
            }, Qt::BlockingQueuedConnection);

            qDebug() << "[AutoRestart] Resource cleanup completed";
            return true;
        } catch (const std::exception &e) {
            qDebug() << "[AutoRestart] Exception during resource cleanup:" << e.what();
            return true; // Continue anyway
        }
    });

    // Step 5: Wait a bit more to ensure clean state
    chain->add([this, deploymentExists]() {
        if (!*deploymentExists) {
            qDebug() << "[AutoRestart] Step 5: Skipping wait - deployment doesn't exist";
            return true;
        }

        qDebug() << "[AutoRestart] Step 5: Waiting for clean state";
        QThread::msleep(3000); // Wait 3 seconds for clean state
        return true;
    });

    // Step 6: Scale back up to 1 replica (fresh start)
    chain->add([this, deploymentExists, sdvSuccess, errorMsg]() {
        if (!*deploymentExists) {
            qDebug() << "[AutoRestart] Step 6: Skipping scale up - deployment doesn't exist";
            *sdvSuccess = true;
            return true;
        }

        try {
            qDebug() << "[AutoRestart] Step 6: Scaling up deployment to 1 replica";
            
            bool scaleOk = false;
            QMetaObject::invokeMethod(qApp, [&]() {
                K3s::Installer inst;
                QEventLoop loop;
                const QStringList scaleCmd{
                    "kubectl scale deployment sdv-runtime --replicas=1 -n default"
                };
                QObject::connect(&inst, &K3s::Installer::finished, &loop,
                               [&](bool b) { scaleOk = b; loop.quit(); });
                inst.queueAndRun(scaleCmd);
                loop.exec();
            }, Qt::BlockingQueuedConnection);

            *sdvSuccess = scaleOk;
            if (!scaleOk) {
                *errorMsg = "Failed to scale up deployment";
                qDebug() << "[AutoRestart] Failed to scale up deployment";
            } else {
                qDebug() << "[AutoRestart] Successfully scaled up deployment";
            }
            
            return true;
        } catch (const std::exception &e) {
            *errorMsg = QString("Scale up error: %1").arg(e.what());
            qDebug() << "[AutoRestart] Exception during scale up:" << e.what();
            return true;
        }
    });

    // Step 7: Wait for new pods to be ready
    chain->add([this, deploymentExists, sdvSuccess, errorMsg]() {
        if (!*deploymentExists || !*sdvSuccess) {
            qDebug() << "[AutoRestart] Step 7: Skipping readiness check";
            return true;
        }

        try {
            qDebug() << "[AutoRestart] Step 7: Waiting for new pods to be ready";
            
            // Wait up to 60 seconds for pods to be ready
            bool podsReady = false;
            for (int i = 0; i < 60; ++i) {
                QMetaObject::invokeMethod(qApp, [&]() {
                    K3s::Installer inst;
                    QEventLoop loop;
                    const QStringList checkCmd{
                        "kubectl get pods -l app=sdv-runtime -n default -o jsonpath='{.items[*].status.phase}'"
                    };
                    QObject::connect(&inst, &K3s::Installer::finished, &loop,
                                   [&](bool b) { 
                                       // Check if we got "Running" status
                                       podsReady = b;
                                       loop.quit(); 
                                   });
                    inst.queueAndRun(checkCmd);
                    loop.exec();
                }, Qt::BlockingQueuedConnection);

                if (podsReady) {
                    qDebug() << "[AutoRestart] New pods are ready after" << i << "seconds";
                    break;
                }
                
                if (i % 5 == 0) { // Log every 5 seconds
                    qDebug() << "[AutoRestart] Waiting for pods to be ready... (" << (i+1) << "/60)";
                }
                QThread::msleep(1000); // Wait 1 second
            }
            
            if (!podsReady) {
                *errorMsg += " (Warning: Pods may not be fully ready)";
                qDebug() << "[AutoRestart] Warning: Pods may not be fully ready after 60 seconds";
            }
            
            return true;
        } catch (const std::exception &e) {
            *errorMsg += QString(" Pod readiness error: %1").arg(e.what());
            qDebug() << "[AutoRestart] Exception during pod readiness check:" << e.what();
            return true;
        }
    });

    // Step 8: Wait before restarting application
    chain->add([this]() {
        qDebug() << "[AutoRestart] Step 8: Waiting before app restart";
        QThread::msleep(5000); // Wait 5 seconds for SDV to stabilize
        return true;
    });

    // Step 9: Restart this application
    // chain->add([this]() {
    //     qDebug() << "[AutoRestart] Step 9: Restarting sdv-runtime application";
    //     this->performApplicationRestart();
    //     return true;
    // });

    // Cleanup and report results
    connect(chain, &Async::Chain::finished, this, [deploymentExists, sdvSuccess, errorMsg](bool) {
        restartInProgress = false;
        
        if (!*deploymentExists) {
            NOTIFY_INFO("Auto Restart", "SDV deployment not found, only restarting application");
            qDebug() << "[AutoRestart] Restart sequence completed - SDV deployment not found";
        } else if (*sdvSuccess) {
            NOTIFY_SUCCESS("Auto Restart", "Services restart sequence completed successfully");
            qDebug() << "[AutoRestart] Restart sequence completed successfully";
        } else {
            NOTIFY_WARNING("Auto Restart", 
                          QString("Restart completed with issues: %1").arg(*errorMsg));
            qDebug() << "[AutoRestart] Restart completed with issues:" << *errorMsg;
        }
    });

    connect(chain, &Async::Chain::finished, chain, &QObject::deleteLater);
    chain->start();
}

/* ------------ perform application restart --------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::performApplicationRestart()
{
    qDebug() << "[AppRestart] Saving state and preparing restart";
    
    // Save current state
    this->saveStateBeforeRestart();
    
    // Try multiple restart methods
    QString appPath = QCoreApplication::applicationFilePath();
    QStringList args = QCoreApplication::arguments();
    args.removeFirst(); // Remove program name
    
    qDebug() << "[AppRestart] App path:" << appPath;
    qDebug() << "[AppRestart] Args:" << args;
    
    // Method 1: Try systemctl restart (most reliable for service)
    if (QProcess::execute("systemctl", QStringList() << "is-active" << "sdv-runtime") == 0) {
        qDebug() << "[AppRestart] Using systemctl restart";
        QProcess::startDetached("systemctl", QStringList() << "restart" << "sdv-runtime");
        QTimer::singleShot(1000, []() { QCoreApplication::quit(); });
        return;
    }
    
    // Method 2: Direct executable restart
    qDebug() << "[AppRestart] Using direct executable restart";
    if (QProcess::startDetached(appPath, args)) {
        QTimer::singleShot(500, []() { QCoreApplication::quit(); });
    } else {
        // Method 3: Force exit and let external process manager restart
        qDebug() << "[AppRestart] Force exit - relying on external restart";
        QTimer::singleShot(500, []() { 
            QCoreApplication::exit(42); // Special exit code for restart
        });
    }
}

/* ------------ manual restart methods (enhanced) --------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::restartSdvRuntime()
{
    if (m_wlanStatus != WlanStatus::Connected) {
        NOTIFY_WARNING("Restart", "Internet connection required for restart");
        return;
    }

    NOTIFY_INFO("SDV Runtime", "Manually restarting SDV runtime deployment...");
    qDebug() << "[ManualRestart] Starting manual SDV restart";
    
    auto *chain = new Async::Chain(this);
    auto success = std::make_shared<bool>(false);
    auto errorMsg = std::make_shared<QString>();

    chain->add([success, errorMsg]() {
        try {
            bool ok = false;
            QMetaObject::invokeMethod(qApp, [&]() {
                K3s::Installer inst;
                QEventLoop loop;
                const QStringList cmd{
                    "kubectl rollout restart deployment/sdv-runtime -n default"
                };
                QObject::connect(&inst, &K3s::Installer::finished, &loop,
                               [&](bool b) { ok = b; loop.quit(); });
                inst.queueAndRun(cmd);
                loop.exec();
            }, Qt::BlockingQueuedConnection);

            *success = ok;
            if (!ok) {
                *errorMsg = "kubectl rollout restart failed";
            }
            return true;
        } catch (const std::exception &e) {
            *errorMsg = QString("Restart error: %1").arg(e.what());
            return true;
        }
    });

    connect(chain, &Async::Chain::finished, this, [success, errorMsg](bool) {
        if (*success) {
            NOTIFY_SUCCESS("SDV Runtime", "SDV runtime deployment restart initiated");
        } else {
            NOTIFY_ERROR("SDV Runtime", 
                        QString("Failed to restart SDV runtime: %1").arg(*errorMsg));
        }
    });

    connect(chain, &Async::Chain::finished, chain, &QObject::deleteLater);
    chain->start();
}

/* ------------ manual application restart ---------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::restartApplication()
{
    if (m_wlanStatus != WlanStatus::Connected) {
        NOTIFY_WARNING("Restart", "Internet connection required for restart");
        return;
    }

    NOTIFY_INFO("Application", "Manually restarting sdv-runtime application in 3 seconds...");
    qDebug() << "[ManualRestart] Manual application restart requested";
    
    QTimer::singleShot(3000, this, [this]() {
        this->performApplicationRestart();
    });
}

/* ------------ save state before restart ----------------------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::saveStateBeforeRestart()
{
    try {
        DataManager dm;
        QJsonArray currentState;
        
        for (const auto& item : m_items) {
            QJsonObject obj;
            obj["id"] = item.id;
            obj["name"] = item.name;
            obj["author"] = item.author;
            obj["rating"] = item.rating;
            obj["thumbnail"] = item.iconPath;
            obj["subscribed"] = item.isSubscribed;
            currentState.append(obj);
        }
        
        // Add timestamp
        QJsonObject metadata;
        metadata["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        metadata["reason"] = "auto_restart_internet_restored";
        currentState.append(metadata);
        
        dm.save(dbKey() + "_restart_backup", currentState);
        qDebug() << "[StateBackup] State saved before restart";
        
    } catch (const std::exception &e) {
        qDebug() << "[StateBackup] Failed to save state:" << e.what();
    }
}

/* ------------ force restart both (emergency option) ----------- */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::forceRestartBoth()
{
    if (m_wlanStatus != WlanStatus::Connected) {
        NOTIFY_WARNING("Restart", "Internet connection required for restart");
        return;
    }

    NOTIFY_WARNING("Force Restart", "Force restarting both SDV runtime and application...");
    
    auto *chain = new Async::Chain(this);

    // Step 1: Restart SDV runtime
    chain->add([this]() {
        this->restartSdvRuntime();
        QThread::msleep(5000); // Wait for SDV restart to begin
        return true;
    });

    // Step 2: Restart application
    // chain->add([this]() {
    //     this->restartApplication();
    //     return true;
    // });

    connect(chain, &Async::Chain::finished, chain, &QObject::deleteLater);
    chain->start();
}

/* ------------ deployment monitor ------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::checkRunningAppSts()
{
    if(m_items.isEmpty()) return;
    auto *chain=new Async::Chain(this);

    chain->add([this](){
        m_last.clear();
        for(int i=0;i<m_items.size();++i){
            bool ok=false;
            try{ ok=K3s::Installer::deploymentAvailable(m_items[i].id,5);}catch(...){}
            m_last.push_back({m_items[i].id,ok,i});
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

    chain->start();
}
