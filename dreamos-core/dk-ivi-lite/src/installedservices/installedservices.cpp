#include "installedservices.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
#include <QDebug>
#include <QMutex>
#include "../marketplace/datamanager.hpp"

#include "../installedservices/unsafeparamcheck.hpp"

QMutex dk_installedsersMutex;

extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

QString DK_INSTALLED_SERS_FOLDER = "";

InstalledVsersCheckThread::InstalledVsersCheckThread(VsersAsync *parent)
{
    QString mpDataPath = DK_INSTALLED_SERS_FOLDER + "installedservices.json";
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

void InstalledVsersCheckThread::triggerCheckAppStart(QString id, QString name)
{
    m_appId = id;
    m_appName = name;
    m_istriggeredAppStart = true;
}

void InstalledVsersCheckThread::run()
{
    QString dockerps = DK_INSTALLED_SERS_FOLDER + "listappscmd.log";
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

VsersAsync::VsersAsync()
{
    if (DK_CONTAINER_ROOT.isEmpty())
        DK_CONTAINER_ROOT = qgetenv("DK_CONTAINER_ROOT");

    DK_INSTALLED_SERS_FOLDER = DK_CONTAINER_ROOT + "dk_marketplace/";
    qDebug() << __func__ << "DK_INSTALLED_SERS_FOLDER:" << DK_INSTALLED_SERS_FOLDER;

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
            &VsersAsync::onInstallerFinished);

    // --- existing threads & timers ---
    m_workerThread = new InstalledVsersCheckThread(this);
    // … connect resultReady, start thread …

    m_timer_apprunningcheck = new QTimer(this);
    connect(m_timer_apprunningcheck, &QTimer::timeout,
            this, &VsersAsync::checkRunningAppSts);
    m_timer_apprunningcheck->start(3000);

    m_logStreamer = nullptr;
}

void VsersAsync::streamLogs(int appIdx)
{
    if (m_logStreamer) {
        stopLogStream();
    }

    if (appIdx < 0 || appIdx >= installedVappsList.size()) {
        qWarning() << "[streamLogs] Invalid index:" << appIdx;
        return;
    }

    const auto& app = installedVappsList[appIdx];
    QString appName = app.name.toLower();

    m_logStreamer = new QProcess(this);
    m_logStreamer->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_logStreamer, &QProcess::readyReadStandardOutput, this, &VsersAsync::onNewLogData);
    connect(m_logStreamer, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int, QProcess::ExitStatus){
        qDebug() << "[LogStreamer] finished.";
        stopLogStream();
    });

    QString cmd = QString("kubectl logs -f -l app=%1").arg(appName);
    qDebug() << "[LogStreamer] START:" << cmd;
    m_logStreamer->start("sh", QStringList{"-c", cmd});
}

void VsersAsync::stopLogStream()
{
    if (m_logStreamer && m_logStreamer->state() != QProcess::NotRunning) {
        qDebug() << "[LogStreamer] STOP";
        m_logStreamer->terminate();
        m_logStreamer->waitForFinished(1000);
        m_logStreamer->deleteLater();
        m_logStreamer = nullptr;
    }
}

void VsersAsync::onNewLogData()
{
    if (!m_logStreamer) return;
    QByteArray data = m_logStreamer->readAllStandardOutput();
    if (!data.isEmpty()) {
        emit newLogMessage(QString::fromUtf8(data));
    }
}

void VsersAsync::onInstallerFinished(int exitCode,
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

Q_INVOKABLE void VsersAsync::openAppEditor(int idx)
{
    qDebug() << __func__ << __LINE__ << " index = " << idx;

    if (idx >= installedVappsList.size()) {
        qDebug() << "index out of range";
        return;
    }

    QString thisServiceFolder = DK_INSTALLED_SERS_FOLDER + installedVappsList[idx].id;
    QString vsCodeUserDataFolder = DK_INSTALLED_SERS_FOLDER + "/vscode_user_data";
    QString cmd;
    cmd = "mkdir -p " + vsCodeUserDataFolder + ";";
    cmd += "code " + thisServiceFolder + " --no-sandbox --user-data-dir=" + vsCodeUserDataFolder + ";";
    qDebug() << cmd;
    system(cmd.toUtf8());
}

Q_INVOKABLE void VsersAsync::initInstalledFromDB()
{
    QMutexLocker lock(&dk_installedsersMutex);

    clearServicesListView();
    installedVappsList.clear();

    // use the same track file as DataManager elsewhere
    QString trackFile = DK_INSTALLED_SERS_FOLDER + "installedservices.json";

    // load (or create) a JSON array
    QJsonDocument doc = DataManager::loadJsonFile(
                             trackFile,
                             QJsonValue(QJsonArray()));
    QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

    for (auto v : arr) {
        if (!v.isObject()) continue;
        QJsonObject o = v.toObject();

        VsersListStruct app;
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

Q_INVOKABLE void VsersAsync::executeServices(int appIdx,
                                             const QString name,
                                             const QString appId,
                                             bool isSubscribed)
{
    // guard index
    if (appIdx < 0 || appIdx >= installedVappsList.size())
        return;

    // build path to the deployment YAML we generated earlier
    QString deployYaml = QString("%1/%2/%2_deployment.yaml")
                         .arg(DK_INSTALLED_SERS_FOLDER, appId);

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


Q_INVOKABLE void VsersAsync::removeServices(int index)
{
    if (index < 0 || index >= installedVappsList.size()) return;

    const QString appId = installedVappsList[index].id;
    QString trackFile = DK_INSTALLED_SERS_FOLDER + "installedservices.json";

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
                         .arg(DK_INSTALLED_SERS_FOLDER, appId);
    QString cmd = QString("kubectl delete -f %1").arg(deployYaml);
    qDebug() << "[Installer] DELETE:" << cmd;
    m_installer->start("sh", QStringList{"-c", cmd});
}

void VsersAsync::handleResults(QString appId, bool isStarted, QString msg)
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

void VsersAsync::fileChanged(const QString &path)
{
    qDebug() << __func__ << "@" << __LINE__ <<  " : path: " << path;
    QThread::msleep(1000);
    initInstalledFromDB();
}

void VsersAsync::checkRunningAppSts()
{
    // If we still have outstanding checks, skip this tick:
    if (m_pendingChecks > 0) {
        qDebug() << "[checkRunningAppSts] still waiting on"
                 << m_pendingChecks << "process(es), skipping.";
        return;
    }

    // Kick off one QProcess per installed app:
    int n = installedVappsList.size();
    m_pendingChecks = n;
    for (int i = 0; i < n; ++i) {
        const auto &app = installedVappsList[i];
        // If no ID, immediately report “not running”:
        // if (app.id.isEmpty()) {
        //     updateServicesRunningSts(app.id, false, i);
        //     --m_pendingChecks;
        //     continue;
        // }

        // Make a transient QProcess
        QProcess *proc = new QProcess(this);
        proc->setProgram("kubectl");
        proc->setArguments(QStringList{
            "get", "pods",
            "-l", QString("app=%1").arg(app.name.toLower()),
            "-o", "json"
        });
        proc->setProcessChannelMode(QProcess::MergedChannels);

        // When it finishes, parse the JSON and emit status
        connect(proc, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
                this,
                [this, proc, app, i](int exitCode, QProcess::ExitStatus status) {
            bool isRunning = false;
            if (status == QProcess::NormalExit && exitCode == 0) {
                // parse {"items":[ { "status":{ "containerStatuses":[…] } }, … ]}
                auto doc = QJsonDocument::fromJson(proc->readAllStandardOutput());
                if (doc.isObject()) {
                    QJsonArray items = doc.object().value("items").toArray();
                    for (auto podVal : items) {
                        auto podObj = podVal.toObject();
                        auto st     = podObj.value("status").toObject();
                        auto csArr  = st.value("containerStatuses").toArray();
                        for (auto csVal : csArr) {
                            auto cs = csVal.toObject();
                            if (cs.value("ready").toBool(false)) {
                                isRunning = true;
                                break;
                            }
                        }
                        if (isRunning) break;
                    }
                }
            }
            // report it:
            updateServicesRunningSts(app.id, isRunning, i);
            proc->deleteLater();
            qDebug() << "[checkRunningAppSts] isRunning" << isRunning;

            // count down, and when zero we’re free to start another round
            if (--m_pendingChecks == 0) {
                qDebug() << "[checkRunningAppSts] all"
                         << installedVappsList.size()
                         << "checks done.";
            }
        });

        // fire & forget
        proc->start();
    }
}