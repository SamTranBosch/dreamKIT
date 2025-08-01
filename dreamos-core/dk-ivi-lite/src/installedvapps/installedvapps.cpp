#include "installedvapps.hpp"
#include "../marketplace/core/datamanager.hpp"
#include "../marketplace/k3s/installer.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QDebug>
#include <QMutex>
#include <QProcessEnvironment>


// ───────────────────────────────────────────────────────────────
// Globals that already existed
// ───────────────────────────────────────────────────────────────
extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

static QString  DK_INSTALLED_APPS_FOLDER;

// ───────────────────────────────────────────────────────────────
// InstalledVappsCheckThread
// ───────────────────────────────────────────────────────────────
InstalledVappsCheckThread::InstalledVappsCheckThread(VappsAsync *parent)
{
    const QString mpDataPath = DK_INSTALLED_APPS_FOLDER + "installedapps.json";
    m_serviceAsync = parent;
    m_filewatcher  = new QFileSystemWatcher(this);

    if (m_filewatcher && QFile::exists(mpDataPath)) {
        m_filewatcher->addPath(mpDataPath);
        connect(m_filewatcher, &QFileSystemWatcher::fileChanged,
                m_serviceAsync, &VappsAsync::fileChanged);
    }
}

QString InstalledVappsCheckThread::m_appId;
QString InstalledVappsCheckThread::m_appName;
bool InstalledVappsCheckThread::m_istriggeredAppStart = false;

void InstalledVappsCheckThread::triggerCheckAppStart(QString id, QString name)
{
    m_appId   = std::move(id);
    m_appName = std::move(name);
    m_istriggeredAppStart = true;
}

void InstalledVappsCheckThread::resetTriggerFlags()
{
    m_istriggeredAppStart = false;
    m_appId.clear(); m_appName.clear();
}

void InstalledVappsCheckThread::notifyState(bool ok)
{
    if (m_istriggeredAppStart && !m_appId.isEmpty() && !m_appName.isEmpty())
    {
        const QString msg = ok
              ? tr("<b>%1</b> is started successfully.").arg(m_appName)
              : tr("<b>%1</b> is NOT started successfully.<br><br>"
                   "Please contact the car OEM for more information !!!")
                    .arg(m_appName);
        emit resultReady(m_appId, ok, msg);
        qDebug() << "[InstalledVappsCheckThread] resultReady:"
                 << m_appName << ok << msg;

        resetTriggerFlags();
    }
}

// ───────────────────────────────────────────────────────────────
// VappsAsync ctor
// ───────────────────────────────────────────────────────────────
VappsAsync::VappsAsync()
{
    if (DK_CONTAINER_ROOT.isEmpty())
        DK_CONTAINER_ROOT = qEnvironmentVariable("DK_CONTAINER_ROOT");
    DK_INSTALLED_APPS_FOLDER = DK_CONTAINER_ROOT + "dk_marketplace/";
    qDebug() << "[VappsAsync] DK_INSTALLED_APPS_FOLDER =" << DK_INSTALLED_APPS_FOLDER;

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
            this, &VappsAsync::onInstallerFinished);

    // background thread + timer
    m_workerThread = new InstalledVappsCheckThread(this);
    connect(m_workerThread, &InstalledVappsCheckThread::resultReady,
            this, &VappsAsync::handleResults);
    m_workerThread->start();

    m_timer_apprunningcheck = new QTimer(this);
    connect(m_timer_apprunningcheck, &QTimer::timeout,
            this, &VappsAsync::checkRunningAppSts);
    m_timer_apprunningcheck->start(5000);
}

// ───────────────────────────────────────────────────────────────
// Installer finished
// ───────────────────────────────────────────────────────────────
void VappsAsync::onInstallerFinished(int exitCode,
                                     QProcess::ExitStatus status)
{
    qDebug() << "[Installer] finished code=" << exitCode
             << "status=" << status
             << "\noutput:\n" << m_installer->readAll();
}

// ───────────────────────────────────────────────────────────────
// Editor helper
// ───────────────────────────────────────────────────────────────
void VappsAsync::openAppEditor(int idx)
{
    if (idx < 0 || idx >= installedVappsList.size())
        return;

    const QString thisServiceFolder = DK_INSTALLED_APPS_FOLDER + installedVappsList[idx].id;
    const QString vsCodeUserData    = DK_INSTALLED_APPS_FOLDER + "vscode_user_data";
    const QString cmd =
        "mkdir -p " + vsCodeUserData + "; "
        "code " + thisServiceFolder + " --no-sandbox --user-data-dir=" + vsCodeUserData + ";";
    qDebug() << cmd;
    system(cmd.toUtf8());
}

// ───────────────────────────────────────────────────────────────
// Read installed vApps list
// ───────────────────────────────────────────────────────────────
void VappsAsync::initInstalledFromDB()
{
    emit clearServicesListView();
    installedVappsList.clear();

    DataManager dm;
    QJsonArray arr = dm.load("vehicle");

    updateInstalledList(arr);
}

void VappsAsync::updateInstalledList(const QJsonArray &arr)
{
    emit clearServicesListView();
    installedVappsList.clear();

    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        VappsListStruct app;
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
// Apply / Delete deployment
// ─────────────────────────────────────────────────────────────
void VappsAsync::executeServices(int appIdx,
                                 const QString /*name*/,
                                 const QString appId,
                                 bool isSubscribed)
{
    if (appIdx < 0 || appIdx >= installedVappsList.size()) return;

    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_APPS_FOLDER, appId);

    const QStringList cmds = isSubscribed
        ? QStringList{ QString("kubectl apply -f %1").arg(deployYaml) }
        : QStringList{ QString("kubectl delete -f %1 --ignore-not-found")
                          .arg(deployYaml) };

    auto *chain = new Async::Chain(this);

    /* step-0 : kubectl (runs in GUI thread, waits synchronously) */
    chain->add([cmds = std::move(cmds)](){

        bool okKubectl = false;
    
        QMetaObject::invokeMethod(
            qApp,                                        // run in GUI thread
            [&cmds, &okKubectl](){
    
                K3s::Installer installer;                // local, GUI thread
                QEventLoop loop;
    
                QObject::connect(&installer, &K3s::Installer::finished,
                                 &loop,
                                 [&](bool ok){ okKubectl = ok; loop.quit(); },
                                 Qt::QueuedConnection);
    
                installer.queueAndRun(cmds);
                loop.exec();                             // wait for pipeline
            },
            Qt::BlockingQueuedConnection);
    
        if (!okKubectl)
            throw std::runtime_error("kubectl apply/delete failed");
    });    

    /* step-1 : docker-ps watcher (worker thread) */
    chain->add([this, appId, appIdx, isSubscribed](){
        InstalledVappsCheckThread *worker = new InstalledVappsCheckThread(this);
        connect(worker, &InstalledVappsCheckThread::resultReady,
                this,   &VappsAsync::handleResults,
                Qt::QueuedConnection);

        worker->triggerCheckAppStart(
                appId,
                installedVappsList[appIdx].name.toLower());

        installedVappsList[appIdx].isSubscribed = isSubscribed;
    });

    chain->start();
}

// ─────────────────────────────────────────────────────────────
// Remove services
// ─────────────────────────────────────────────────────────────
void VappsAsync::removeServices(int index)
{
    if (index < 0 || index >= installedVappsList.size()) return;

    const QString appId = installedVappsList[index].id;
    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_APPS_FOLDER, appId);

    auto *chain = new Async::Chain(this);

    /* step-0 : update installedapps.json (worker thread) */
    chain->add([appId](){
        DataManager dm;
        QJsonArray arr = dm.load("vehicle");
        QJsonArray out;
        for (const auto &v : arr)
            if (v.isObject() && v.toObject().value("id").toString() != appId)
                out.append(v);
        dm.save("vehicle", out);
    });

    /* step-1 : kubectl delete in GUI thread */
    chain->add([deployYaml](){

        bool okKubectl = false;
    
        QMetaObject::invokeMethod(
            qApp,
            [&deployYaml, &okKubectl](){
    
                const QStringList cmds{
                    QString("kubectl delete -f %1 --ignore-not-found")
                            .arg(deployYaml)
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
    
        if (!okKubectl)
            throw std::runtime_error("kubectl delete failed");
    });    

    chain->start();
}

// ───────────────────────────────────────────────────────────────
// Result from docker-ps watcher
// ───────────────────────────────────────────────────────────────
void VappsAsync::handleResults(QString appId, bool isStarted, QString msg)
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
// File changed (installedapps.json)   reload list
// ─────────────────────────────────────────────────────────────
void VappsAsync::fileChanged(const QString &path)
{
    /* debounce in worker thread, no sleep on GUI */
    auto *job = new Async::Job<QJsonArray>([=](){

        QThread::msleep(500);          // debounce 0.5 s
        DataManager dm;
        return dm.load("vehicle");   // worker thread!

    }, this);

    connect(job, &Async::JobBase::finished,
            this, [this, job](bool ok){
        if (!ok) { job->deleteLater(); return; }

        const QJsonArray arr = job->result();
        /* a helper you already have   refresh model from JSON */
        updateInstalledList(arr);
        job->deleteLater();
    });
}

// ───────────────────────────────────────────────────────────────
// Periodic check: is the deployment up?
// ───────────────────────────────────────────────────────────────
void VappsAsync::checkRunningAppSts()
{
    for (int i = 0; i < installedVappsList.size(); ++i) {
        const auto &app = installedVappsList[i];
        if (app.id.isEmpty()) {
            emit updateServicesRunningSts(app.id, false, i);
            continue;
        }

        const QString appId   = app.id;
        const QString appName = app.name.toLower();

        auto *job = K3s::Installer::deploymentAvailableAsync(
                        appName, 10, this);

        connect(job,
                &Async::Job<K3s::DeploymentCheck>::finished,
                this,
                /* capture id + index + job */
                [this, i, job, appId](bool){
            const auto res = job->result();
            emit updateServicesRunningSts(appId, res.available, i);
            job->deleteLater();

            InstalledVappsCheckThread *worker = new InstalledVappsCheckThread(this);
            if (res.available) {
                connect(worker, &InstalledVappsCheckThread::resultReady,
                    this, &VappsAsync::handleResults);
                    worker->notifyState(res.available);
            }
        });
    }
}
