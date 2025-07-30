#include "installedservices.hpp"
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

static QMutex   dk_installedsersMutex;
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
    m_timer_apprunningcheck->start(3000);
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
    QMutexLocker lock(&dk_installedsersMutex);

    emit clearServicesListView();
    installedVappsList.clear();

    DataManager dm;
    QJsonArray arr = dm.load("vehicle-service");

    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        VsersListStruct app;
        app.id          = o.value("id").toString();
        app.name        = o.value("name").toString();
        app.author      = o.value("author").toString();
        app.rating      = o.value("rating").toString();
        app.iconPath    = o.value("iconPath").toString();
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

// ───────────────────────────────────────────────────────────────
// Apply / Delete deployment
// ───────────────────────────────────────────────────────────────
void VsersAsync::executeServices(int appIdx,
                                 const QString /*name*/,
                                 const QString appId,
                                 bool isSubscribed)
{
    if (appIdx < 0 || appIdx >= installedVappsList.size()) return;

    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_SERS_FOLDER, appId);

    const QString cmd = isSubscribed
        ? QString("kubectl apply -f %1").arg(deployYaml)
        : QString("kubectl delete -f %1 --ignore-not-found").arg(deployYaml);

    qDebug() << "[Installer]" << cmd;
    m_installer->start("sh", {"-c", cmd});

    InstalledVsersCheckThread *worker = new InstalledVsersCheckThread(this);
    connect(worker, &InstalledVsersCheckThread::resultReady,
            this, &VsersAsync::handleResults);
    worker->triggerCheckAppStart(appId, installedVappsList[appIdx].name.toLower());
}

// ───────────────────────────────────────────────────────────────
// Remove services (and update installedservices.json)
// ───────────────────────────────────────────────────────────────
void VsersAsync::removeServices(int index)
{
    if (index < 0 || index >= installedVappsList.size()) return;

    const QString appId = installedVappsList[index].id;

    // load + filter
    DataManager dm;
    QJsonArray arr = dm.load("vehicle-service");
    qDebug() << "[VsersAsync] Loaded" << arr.size() << "installed apps from";

    // filter out the appId
    QJsonArray out;
    for (const auto &v : arr)
        if (v.isObject() && v.toObject().value("id").toString() != appId)
            out.append(v);

    dm.save("vehicle-service", out);

    // kubectl delete
    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_SERS_FOLDER, appId);
    const QString cmd = QString("kubectl delete -f %1 --ignore-not-found").arg(deployYaml);
    qDebug() << "[Installer]" << cmd;
    m_installer->start("sh", {"-c", cmd});
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

// ───────────────────────────────────────────────────────────────
// File changed (installedservices.json)   reload list
// ───────────────────────────────────────────────────────────────
void VsersAsync::fileChanged(const QString &path)
{
    QThread::msleep(1000);   // debounce
    initInstalledFromDB();
}

// ───────────────────────────────────────────────────────────────
// Periodic check: is the deployment up?
// ───────────────────────────────────────────────────────────────
void VsersAsync::checkRunningAppSts()
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

            InstalledVsersCheckThread *worker = new InstalledVsersCheckThread(this);
            if (res.available) {
                connect(worker, &InstalledVsersCheckThread::resultReady,
                    this, &VsersAsync::handleResults);
                    worker->notifyState(res.available);
            }
        });
    }
}
