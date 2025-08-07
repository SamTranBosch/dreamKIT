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
    bool wlanConnected() const { return m_wlanStatus == WlanStatus::Connected; }

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
            m_nodeTimer->start(5'000);
        }
    });

    /* create WLAN-timer only when requested by subclass */
    QTimer::singleShot(0, this, [this]() {
        if ( this->wantsWlanMonitor() ) {
            m_wlanTimer = new QTimer(this);
            connect(m_wlanTimer, &QTimer::timeout,
                    this, &InstalledAsyncBase<TI,TD>::checkInternetConnection);
            m_wlanTimer->start(7'000);  // check every 7 seconds
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

    // Create a simple HTTP request to check internet connectivity
    QNetworkRequest request(QUrl("http://www.google.com"));
    request.setRawHeader("User-Agent", "Mozilla/5.0");
    
    // Qt6 uses RedirectPolicyAttribute instead of FollowRedirectsAttribute
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, 
                        QNetworkRequest::NoLessSafeRedirectPolicy);
    
    // Set a timeout for the request (Qt6 way)
    request.setTransferTimeout(5000); // 5 seconds timeout
    
    QNetworkReply* reply = networkManager->head(request);
    
    // Handle the reply
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        WlanStatus newStatus = WlanStatus::Disconnected;
        
        if (reply->error() == QNetworkReply::NoError) {
            // Successfully connected to the internet
            newStatus = WlanStatus::Connected;
        } else {
            // Failed to connect - could be network issue, DNS issue, etc.
            qDebug() << "[InternetCheck] Connection failed:" << reply->errorString();
            newStatus = WlanStatus::Disconnected;
        }
        
        // Only notify if status changed
        if (newStatus != m_wlanStatus) {
            m_wlanStatus = newStatus;
            
            if (newStatus == WlanStatus::Connected) {
                NOTIFY_SUCCESS("Internet", "Internet connection available");
            } else {
                NOTIFY_WARNING("Internet", "No internet connection");
            }
            // Notify derived class about the status change
            // static_cast<TD*>(this)->internetConnectionStatusChanged(newStatus == WlanStatus::Connected);
        }
        
        reply->deleteLater();
    });
    
    // Handle timeout and errors
    connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](QNetworkReply::NetworkError error) {
        qDebug() << "[InternetCheck] Network error occurred:" << error << reply->errorString();
        
        if (m_wlanStatus != WlanStatus::Disconnected) {
            m_wlanStatus = WlanStatus::Disconnected;
            NOTIFY_WARNING("Internet", "No internet connection");
            // static_cast<TD*>(this)->internetConnectionStatusChanged(false);
        }
    });
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
