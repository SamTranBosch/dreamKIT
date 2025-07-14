#include "installedvapps.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QDebug>
#include <QMutex>
#include "../marketplace/datamanager.hpp"

#include "../installedservices/unsafeparamcheck.hpp"

QMutex dk_installedappsMutex;

extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

QString DK_INSTALLED_APPS_FOLDER = "";

InstalledVappsCheckThread::InstalledVappsCheckThread(VappsAsync *parent)
{
    QString mpDataPath = DK_INSTALLED_APPS_FOLDER + "installedapps.json";
    // qDebug() << __func__ << "@" << __LINE__ <<  " : mpDataPath: " << mpDataPath;

    m_serviceAsync = parent;
    m_filewatcher = new QFileSystemWatcher(this);

    if (m_filewatcher) {
        QString path = mpDataPath;
        qDebug() << __func__ << __LINE__ << " m_filewatcher : " << path;

        if (QFile::exists(path)) {
            m_filewatcher->addPath(path);
            connect(m_filewatcher, SIGNAL(fileChanged(QString)), m_serviceAsync, SLOT(fileChanged(QString)));
        }
    }
}

void InstalledVappsCheckThread::triggerCheckAppStart(QString id, QString name)
{
    m_appId = id;
    m_appName = name;
    m_istriggeredAppStart = true;
}

void InstalledVappsCheckThread::run()
{
    QString dockerps = DK_INSTALLED_APPS_FOLDER + "listappscmd.log";
    QString cmd = "";     

    while(1) {
        if (m_istriggeredAppStart && !m_appId.isEmpty() && !m_appName.isEmpty()) {
            QThread::msleep(3000); // workaround: wait 2s for the app to start. TODO: consider to check if the start time is more than 2s
            cmd = "docker ps > " + dockerps;
            system(cmd.toUtf8()); 
            QThread::msleep(10);
            QFile MyFile(dockerps);
            MyFile.open(QIODevice::ReadWrite);
            QTextStream in (&MyFile);
            QString raw = in.readAll();
            qDebug() << "reprint docker ps: \n" << raw;
            if (raw.contains(m_appId, Qt::CaseSensitivity::CaseSensitive)) {
                Q_EMIT resultReady(m_appId, true, "<b>"+m_appName+"</b>" + " is started successfully.");
            }
            else {
                Q_EMIT resultReady(m_appId, false, "<b>"+m_appName+"</b>" + " is NOT started successfully.<br><br>Please contact the car OEM for more information !!!");
            }
            cmd = "> " + dockerps;
            system(cmd.toUtf8()); 

            m_istriggeredAppStart = false;
            m_appId.clear();
            m_appName.clear();
        }

        QThread::msleep(100);
    }
}

VappsAsync::VappsAsync()
{
    if (DK_CONTAINER_ROOT.isEmpty())
        DK_CONTAINER_ROOT = qgetenv("DK_CONTAINER_ROOT");

    DK_INSTALLED_APPS_FOLDER = DK_CONTAINER_ROOT + "dk_marketplace/";
    qDebug() << __func__ << "DK_INSTALLED_APPS_FOLDER:" << DK_INSTALLED_APPS_FOLDER;

    // --- installer process for kubectl calls ---
    m_installer = new QProcess(this);
    m_installer->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_installer, &QProcess::started, this, [](){
        qDebug() << "[Installer] started";
    });
    connect(m_installer,
            QOverload<QProcess::ProcessError>::of(&QProcess::errorOccurred),
            this,
            [this](QProcess::ProcessError err){
        qWarning() << "[Installer] error:" << err
                   << m_installer->errorString();
    });
    connect(m_installer,
            QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &VappsAsync::onInstallerFinished);

    // --- existing threads & timers ---
    m_workerThread = new InstalledVappsCheckThread(this);
    // … connect resultReady, start thread …

    m_timer_apprunningcheck = new QTimer(this);
    connect(m_timer_apprunningcheck, &QTimer::timeout,
            this, &VappsAsync::checkRunningAppSts);
    m_timer_apprunningcheck->start(3000);
}

void VappsAsync::onInstallerFinished(int exitCode,
                                     QProcess::ExitStatus status)
{
    qDebug() << "[Installer] finished code=" << exitCode
             << "status=" << status
             << "\noutput:\n" << m_installer->readAll();

    // if apply succeeded and we just deployed, trigger the running‐check
    if (status==QProcess::NormalExit && exitCode==0) {
        // here you can trigger your thread to watch for the pod
        // e.g. m_workerThread->triggerCheckAppStart(appId, name);
    }
}

Q_INVOKABLE void VappsAsync::openAppEditor(int idx)
{
    qDebug() << __func__ << __LINE__ << " index = " << idx;

    if (idx >= installedVappsList.size()) {
        qDebug() << "index out of range";
        return;
    }

    QString thisServiceFolder = DK_INSTALLED_APPS_FOLDER + installedVappsList[idx].id;
    QString vsCodeUserDataFolder = DK_INSTALLED_APPS_FOLDER + "/vscode_user_data";
    QString cmd;
    cmd = "mkdir -p " + vsCodeUserDataFolder + ";";
    cmd += "code " + thisServiceFolder + " --no-sandbox --user-data-dir=" + vsCodeUserDataFolder + ";";
    qDebug() << cmd;
    system(cmd.toUtf8());
}

Q_INVOKABLE void VappsAsync::initInstalledFromDB()
{
    QMutexLocker lock(&dk_installedappsMutex);

    clearServicesListView();
    installedVappsList.clear();

    // use the same track file as DataManager elsewhere
    QString trackFile = DK_INSTALLED_APPS_FOLDER + "installedapps.json";

    // load (or create) a JSON array
    QJsonDocument doc = DataManager::loadJsonFile(
                             trackFile,
                             QJsonValue(QJsonArray()));
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    for (auto v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();

        VappsListStruct app;
        app.id          = o.value("id").toString();
        app.name        = o.value("name").toString();
        app.author      = o.value("author").toString();
        app.rating      = o.value("rating").toString();
        app.iconPath    = o.value("iconPath").toString();
        app.isInstalled = true;  // by definition in this file
        app.isSubscribed= o.value("subscribed").toBool();

        installedVappsList.append(app);
    }

    qDebug() << "Loaded" << installedVappsList.size()
             << "installed apps from" << trackFile;

    // populate your UI list
    for (const auto &app : installedVappsList) {
        appendServicesInfoToServicesList(
            app.name, app.author,
            app.rating, app.noofdownload,
            app.iconPath, app.isInstalled,
            app.id, app.isSubscribed);
    }
}

Q_INVOKABLE void VappsAsync::executeServices(int appIdx,
                                             const QString name,
                                             const QString appId,
                                             bool isSubscribed)
{
    // guard index
    if (appIdx < 0 || appIdx >= installedVappsList.size())
        return;

    // build path to the deployment YAML we generated earlier
    QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                         .arg(DK_INSTALLED_APPS_FOLDER, appId);

    if (isSubscribed) {
        // APPLY the Deployment
        QString cmd = QString("kubectl apply -f %1").arg(deployYaml);
        qDebug() << "[Installer] APPLY:" << cmd;
        m_installer->start("sh", QStringList{"-c", cmd});
    }
    else {
        // if unsubscribing, DELETE the Deployment
        QString cmd = QString("kubectl delete -f %1").arg(deployYaml);
        qDebug() << "[Installer] DELETE:" << cmd;
        m_installer->start("sh", QStringList{"-c", cmd});
    }
}


Q_INVOKABLE void VappsAsync::removeServices(int index)
{
    if (index < 0 || index >= installedVappsList.size()) return;

    const QString appId = installedVappsList[index].id;
    QString trackFile = DK_INSTALLED_APPS_FOLDER + "installedapps.json";

    // load & filter out this appId
    QJsonDocument doc = DataManager::loadJsonFile(
                             trackFile,
                             QJsonValue(QJsonArray()));
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();
    QJsonArray out;
    for (auto v : arr) {
        if (!v.isObject() || v.toObject().value("id").toString() == appId)
            continue;
        out.append(v);
    }

    // save the trimmed array
    DataManager::saveJsonFile(trackFile, QJsonDocument(out));

    // now delete via kubectl
    QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                         .arg(DK_INSTALLED_APPS_FOLDER, appId);
    QString cmd = QString("kubectl delete -f %1").arg(deployYaml);
    qDebug() << "[Installer] DELETE:" << cmd;
    m_installer->start("sh", QStringList{"-c", cmd});
}

void VappsAsync::handleResults(QString appId, bool isStarted, QString msg)
{
    updateStartAppMsg(appId, isStarted, msg);
    if (isStarted) {
        int len = installedVappsList.size();
        for (int i = 0; i < len; i++) {
            if (installedVappsList[i].id == appId) {
                installedVappsList[i].isSubscribed = true;
                return;
            }
        }
    }
}

void VappsAsync::fileChanged(const QString &path)
{
    qDebug() << __func__ << "@" << __LINE__ <<  " : path: " << path;
    QThread::msleep(1000);
    initInstalledFromDB();
}

void VappsAsync::checkRunningAppSts()
{
    // Iterate over every installed vApp and check its k8s Deployment
    for (int i = 0; i < installedVappsList.size(); ++i) {
        const auto &app = installedVappsList[i];
        if (app.id.isEmpty()) {
            updateServicesRunningSts(app.id, false, i);
            continue;
        }

        // assume we named the Deployment exactly by the lowercased app.id
        QString deployId = app.id.toLower();
        QString deployName = app.name.toLower();

        // build the kubectl wait command:
        // waits until the Deployment has at least one complete replica
        QStringList args = {
            "wait",
            "--for=condition=complete",
            "deployment/" + deployId,
            "--timeout=2s"
        };

        QProcess proc;
        proc.setProgram("kubectl");
        proc.setArguments(args);
        proc.setProcessChannelMode(QProcess::MergedChannels);

        proc.start();
        bool started  = proc.waitForStarted(1000);
        bool finished = proc.waitForFinished(3000);
        int  code     = proc.exitCode();
        QString out   = proc.readAll().trimmed();

        bool isRunning = (started && finished && code == 0);

        qDebug() << "[checkRunningAppSts]"
                 << deployName
                 << (isRunning ? "AVAILABLE" : "NOT AVAILABLE")
                 << "exitCode=" << code
                 << "output=" << out;

        // notify the QML list-view of this app’s running status
        updateServicesRunningSts(app.id, isRunning, i);
    }
}