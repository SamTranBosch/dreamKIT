#include "installedvapps.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QDebug>
#include <QMutex>
#include <QProcessEnvironment>

#include "../installedservices/unsafeparamcheck.hpp"
#include "../marketplace/core/datamanager.hpp"

// ───────────────────────────────────────────────────────────────
// Globals that already existed
// ───────────────────────────────────────────────────────────────
extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

static QMutex   dk_installedappsMutex;
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

void InstalledVappsCheckThread::triggerCheckAppStart(QString id, QString name)
{
    m_appId   = std::move(id);
    m_appName = std::move(name);
    m_istriggeredAppStart = true;
}

void InstalledVappsCheckThread::run()
{
    const QString dockerps = DK_INSTALLED_APPS_FOLDER + "listappscmd.log";
    while (true) {
        if (m_istriggeredAppStart && !m_appId.isEmpty() && !m_appName.isEmpty())
        {
            QThread::msleep(3000);                     // allow container to start
            const QString cmd = "docker ps > " + dockerps;
            system(cmd.toUtf8());
            QFile f(dockerps);
            f.open(QIODevice::ReadOnly);
            const QString raw = QTextStream(&f).readAll();
            f.close();

            const bool ok = raw.contains(m_appId, Qt::CaseSensitive);
            const QString msg = ok
                ? "<b>"+m_appName+"</b> is started successfully."
                : "<b>"+m_appName+"</b> is NOT started successfully."
                  "<br><br>Please contact the car OEM for more information !!!";
            emit resultReady(m_appId, ok, msg);

            system(QString("> %1").arg(dockerps).toUtf8()); // truncate
            m_istriggeredAppStart = false;
            m_appId.clear(); m_appName.clear();
        }
        QThread::msleep(100);
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
    m_timer_apprunningcheck->start(3000);
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
    QMutexLocker lock(&dk_installedappsMutex);

    emit clearServicesListView();
    installedVappsList.clear();

    DataManager dm;
    QJsonArray arr = dm.load("vehicle");
    qDebug() << "[VappsAsync] Loaded" << arr.size() << "installed apps from";

    for (const auto &v : arr) {
        if (!v.isObject()) continue;
        const QJsonObject o = v.toObject();

        VappsListStruct app;
        app.id          = o.value("id").toString();
        app.name        = o.value("name").toString();
        app.author      = o.value("author").toString();
        app.rating      = o.value("rating").toString();
        app.iconPath    = o.value("iconPath").toString();
        app.isInstalled = true;
        app.isSubscribed= o.value("subscribed").toBool();
        installedVappsList.append(app);
    }

    qDebug() << "[VappsAsync] loaded" << installedVappsList.size();

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
void VappsAsync::executeServices(int appIdx,
                                 const QString /*name*/,
                                 const QString appId,
                                 bool isSubscribed)
{
    if (appIdx < 0 || appIdx >= installedVappsList.size()) return;

    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_APPS_FOLDER, appId);

    const QString cmd = isSubscribed
        ? QString("kubectl apply -f %1").arg(deployYaml)
        : QString("kubectl delete -f %1 --ignore-not-found").arg(deployYaml);

    qDebug() << "[Installer]" << cmd;
    m_installer->start("sh", {"-c", cmd});
}

// ───────────────────────────────────────────────────────────────
// Remove services (and update installedapps.json)
// ───────────────────────────────────────────────────────────────
void VappsAsync::removeServices(int index)
{
    if (index < 0 || index >= installedVappsList.size()) return;

    const QString appId = installedVappsList[index].id;

    // load + filter
    DataManager dm;
    QJsonArray arr = dm.load("vehicle");
    qDebug() << "[VappsAsync] Loaded" << arr.size() << "installed apps from";

    // filter out the appId
    QJsonArray out;
    for (const auto &v : arr)
        if (v.isObject() && v.toObject().value("id").toString() != appId)
            out.append(v);

    dm.save("vehicle", out);

    // kubectl delete
    const QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                               .arg(DK_INSTALLED_APPS_FOLDER, appId);
    const QString cmd = QString("kubectl delete -f %1 --ignore-not-found").arg(deployYaml);
    qDebug() << "[Installer]" << cmd;
    m_installer->start("sh", {"-c", cmd});
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

// ───────────────────────────────────────────────────────────────
// File changed (installedapps.json)   reload list
// ───────────────────────────────────────────────────────────────
void VappsAsync::fileChanged(const QString &path)
{
    qDebug() << "[VappsAsync] fileChanged:" << path;
    QThread::msleep(1000);   // debounce
    initInstalledFromDB();
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

        const QString deployId = app.id.toLower();
        QProcess proc;
        proc.setProgram("kubectl");
        proc.setArguments({"wait",
                           "--for=condition=available",
                           "deployment/" + deployId,
                           "--timeout=2s"});
        proc.setProcessChannelMode(QProcess::MergedChannels);

        proc.start();
        const bool started  = proc.waitForStarted(1000);
        const bool finished = proc.waitForFinished(3000);
        const bool ok       = (started && finished && proc.exitCode() == 0);

        emit updateServicesRunningSts(app.id, ok, i);
    }
}
