#pragma once
#include <QObject>
#include <QTimer>
#include <QProcess>
#include <QJsonArray>
#include <QEventLoop>

#include "../utils/async/asyncjob.hpp"
#include "../utils/core/datamanager.hpp"
#include "../utils/k3s/installer.hpp"
#include "../utils/notifications/notificationmanager.hpp"
#include "installedcheckthread.hpp"

extern QString DK_CONTAINER_ROOT;

/* worker-node enum */
enum class NodeStatus { Unknown, Online, Offline };
Q_DECLARE_METATYPE(NodeStatus)

/********************************************************************/
template<class TI, class TD>
class InstalledAsyncBase : public QObject
{
public:
    explicit InstalledAsyncBase(QObject *parent = nullptr);

    /* must be provided by concrete subclass */
    virtual QString dbKey()      const = 0;
    virtual QString folderRoot() const = 0;
    virtual QString deploymentYaml(const QString &id) const = 0;

    /* ---------- API exposed to QML ------------------------------ */
    Q_INVOKABLE void initInstalledFromDB();
    Q_INVOKABLE void executeServices(int idx, const QString&, const QString id, bool subscribe);
    Q_INVOKABLE void removeServices(int idx);
    Q_INVOKABLE virtual void openAppEditor(int) { }          // optional

    bool workerNodeOnline() const { return m_nodeStatus == NodeStatus::Online; }

protected:
    virtual void appendItemToQml(const TI&) = 0;

    /* subclasses return true if they want node monitoring */
    virtual bool wantsNodeMonitor() const { return false; }

    /* helper used by InstalledCheckThread */
    void fileChanged(const QString&);

    /* give read access for openAppEditor() implementation */
    const QList<TI>& items() const { return m_items; }

    /* shared editor launcher (called by the wrappers below) */
    void launchVsCode(int idx);

private:
    void onInstallerFinished(int, QProcess::ExitStatus);
    void checkWorkerNodeStatus();
    void checkRunningAppSts();
    void updateInstalledList(const QJsonArray&);

    QList<TI>             m_items;
    InstalledCheckThread *m_checkThread {nullptr};
    QTimer               *m_nodeTimer   {nullptr};
    QTimer               *m_stsTimer    {nullptr};
    QProcess             *m_installer   {nullptr};
    NodeStatus            m_nodeStatus  {NodeStatus::Unknown};

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
                        + QString(dbKey()).remove("vehicle-") + "s.json";
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
        DataManager dm; return dm.load(dbKey());
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
        int idx,const QString&,const QString id,bool sub)
{
    if(idx<0 || idx>=m_items.size()) return;

    const QStringList cmds = sub
        ? QStringList{QString("kubectl apply -f %1").arg(deploymentYaml(id))}
        : QStringList{QString("kubectl delete -f %1 --ignore-not-found").arg(deploymentYaml(id))};

    auto *chain = new Async::Chain(this);

    /* node pre-check */
    chain->add([sub](){
        if(!sub) return true;
        bool ok=false; try{ ok=K3s::Installer::nodeReady("vip",5);}catch(...){}
        if(!ok) NOTIFY_WARNING("Node","vip not ready"); return true;
    });

    /* kubectl */
    chain->add([cmds](){
        bool ok=false;
        QMetaObject::invokeMethod(qApp,[&](){
            K3s::Installer inst; QEventLoop l;
            QObject::connect(&inst,&K3s::Installer::finished,&l,
                             [&](bool b){ok=b;l.quit();});
            inst.queueAndRun(cmds); l.exec();
        },Qt::BlockingQueuedConnection);
        if(!ok) throw std::runtime_error("kubectl error");
        return true;
    });

    /* update model + start watcher */
    chain->add([this,idx,id,sub](){
        m_items[idx].isSubscribed=sub;
        m_checkThread->triggerCheckAppStart(id,m_items[idx].name);
        return true;
    });

    connect(chain,&Async::Chain::finished,this,[id,sub](bool ok){
        const QString a=sub?"deployed":"stopped";
        if(ok) NOTIFY_SUCCESS("Service",QString("%1 %2").arg(id,a));
        else   NOTIFY_ERROR  ("Service",QString("Failed to %1 %2").arg(a,id));
    });
    chain->start();
}

/* ------------ remove ------------------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::removeServices(int idx)
{
    if(idx<0||idx>=m_items.size()) return;
    const QString id   = m_items[idx].id;
    const QString yaml = deploymentYaml(id);

    auto *chain = new Async::Chain(this);

    chain->add([=](){
        DataManager dm; QJsonArray in=dm.load(dbKey()),out;
        for(auto v:in) if(v.toObject().value("id").toString()!=id) out.append(v);
        dm.save(dbKey(),out); return true;
    });

    chain->add([=](){
        bool ok=false;
        QMetaObject::invokeMethod(qApp,[&](){
            K3s::Installer inst; QEventLoop l;
            const QStringList c{QString("kubectl delete -f %1 --ignore-not-found").arg(yaml)};
            QObject::connect(&inst,&K3s::Installer::finished,&l,
                             [&](bool b){ok=b;l.quit();});
            inst.queueAndRun(c); l.exec();
        },Qt::BlockingQueuedConnection);
        if(!ok) throw std::runtime_error("kubectl delete failed");
        return true;
    });

    chain->add([this,idx](){ m_items.removeAt(idx); return true;});

    connect(chain,&Async::Chain::finished,this,[=](bool ok){
        if(ok){ NOTIFY_SUCCESS("Service",QString("%1 removed").arg(id));
                initInstalledFromDB(); }
        else  { NOTIFY_ERROR("Service",QString("Failed to remove %1").arg(id)); }
    });
    chain->start();
}

/* ------------ node monitor ------------------------------------ */
template<class TI,class TD>
void InstalledAsyncBase<TI,TD>::checkWorkerNodeStatus()
{
    qDebug() << "[Installer] checkWorkerNodeStatus";
    bool ok=false; try{ ok=K3s::Installer::nodeReady("vip",5);}catch(...){}
    auto st = ok?NodeStatus::Online:NodeStatus::Offline;
    if(st==m_nodeStatus) return;
    m_nodeStatus=st;
    if(ok) NOTIFY_SUCCESS("Node","vip online");
    else   NOTIFY_WARNING("Node","vip offline");
    static_cast<TD*>(this)->workerNodeStatusChanged(ok);
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
