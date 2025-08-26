#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QHostInfo>
#include <QProcessEnvironment>
#include <QTimer>

#include "marketplace.hpp"
#include "../utils/notifications/notificationmanager.hpp"

using namespace Async;
using K3s::ManifestBuilder;
using K3s::JobManager;

extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

//-----------------------------------------------------------------------------
// AppListModel implementation (unchanged)
//-----------------------------------------------------------------------------
AppListModel::AppListModel(QObject* p)
  : QAbstractListModel(p)
{}

int AppListModel::rowCount(const QModelIndex&) const { return m_apps.size(); }

QVariant AppListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_apps.size()) return {};
    const auto &a = m_apps.at(idx.row());
    switch(role) {
      case IdRole:         return a.id;
      case NameRole:       return a.name;
      case AuthorRole:     return a.author;
      case RatingRole:     return a.rating;
      case DownloadsRole:  return a.downloads;
      case IconRole:       return a.iconUrl;
      case InstalledRole:  return a.isInstalled;
      case FolderRole:     return a.folderName;
      case PackageLinkRole:return a.packageLink;
      default:             return {};
    }
}

QHash<int,QByteArray> AppListModel::roleNames() const {
    return {
      {IdRole,         "id"},
      {NameRole,       "name"},
      {AuthorRole,     "author"},
      {RatingRole,     "rating"},
      {DownloadsRole,  "downloads"},
      {IconRole,       "iconUrl"},
      {InstalledRole,  "isInstalled"},
      {FolderRole,     "folderName"},
      {PackageLinkRole,"packageLink"}
    };
}

QVariantMap AppListModel::get(int row) const {
    QVariantMap m;
    if (row<0||row>=m_apps.size()) return m;
    const auto &a = m_apps.at(row);
    m["id"]           = a.id;
    m["name"]         = a.name;
    m["author"]       = a.author;
    m["rating"]       = a.rating;
    m["downloads"]    = a.downloads;
    m["iconUrl"]      = a.iconUrl;
    m["isInstalled"]  = a.isInstalled;
    m["folderName"]   = a.folderName;
    m["packageLink"]  = a.packageLink;
    return m;
}

void AppListModel::updateApps(const QList<AppInfo> &apps) {
    beginResetModel();
      m_apps = apps;
    endResetModel();
}

void AppListModel::setAppInstalled(int idx, bool inst) {
    if (idx<0||idx>=m_apps.size()) return;
    m_apps[idx].isInstalled = inst;
    QModelIndex mi = index(idx,0);
    emit dataChanged(mi, mi, {InstalledRole});
}

//-----------------------------------------------------------------------------
// CategoryListModel implementation (unchanged)
//-----------------------------------------------------------------------------
CategoryListModel::CategoryListModel(QObject* p)
  : QAbstractListModel(p)
{}

int CategoryListModel::rowCount(const QModelIndex&) const { return m_list.size(); }

QVariant CategoryListModel::data(const QModelIndex &idx, int role) const {
    if (!idx.isValid()||idx.row()<0||idx.row()>=m_list.size()) return {};
    const auto &c = m_list.at(idx.row());
    switch(role){
      case NameRole:     return c.name;
      case UrlRole:      return c.url;
      case LoginUrlRole: return c.loginUrl;
      default:           return {};
    }
}

QHash<int,QByteArray> CategoryListModel::roleNames() const {
    return {
      {NameRole,     "displayName"},
      {UrlRole,      "marketUrl"},
      {LoginUrlRole, "loginUrl"}
    };
}

void CategoryListModel::loadFromJsonFile(const QString &filePath) {
    QFile f(filePath);
    if (!f.exists()) {
        QDir().mkpath(QFileInfo(filePath).path());
        QJsonArray arr;
        QJsonObject def;
        def["name"]            = "BGSV Marketplace";
        def["marketplace_url"] = "https://store-be.sdv.digital.auto";
        def["login_url"]       = "";
        arr.append(def);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(QJsonDocument(arr).toJson());
            f.close();
        }
    }
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isArray()) return;

    beginResetModel();
      m_list.clear();
      for (auto v : doc.array()) {
        if (!v.isObject()) continue;
        auto o = v.toObject();
        Info info;
        info.name     = o["name"].toString();
        info.url      = o["marketplace_url"].toString();
        info.loginUrl = o["login_url"].toString();
        m_list.append(info);
      }
    endResetModel();
}

//-----------------------------------------------------------------------------
// InstallationWorker - FIXED VERSION (No threading complexity)
//-----------------------------------------------------------------------------
InstallationWorker::InstallationWorker(QObject *parent)
    : QObject(parent)
    , m_installationActive(false)
{
    // Create job manager in same thread for simplicity
    m_jobManager = new K3s::JobManager(this);
    qDebug() << "[InstallationWorker] Created with JobManager";
}

InstallationWorker::~InstallationWorker()
{
    // JobManager will be cleaned up by parent
}

void InstallationWorker::startInstallation(const AppInfo &app, const QString &category)
{
    if (m_installationActive) {
        qWarning() << "[InstallationWorker] Installation already active, ignoring request";
        return;
    }
    
    qDebug() << "[InstallationWorker] Starting installation for:" << app.name;
    
    // Store installation info
    m_currentApp = app;
    m_currentCategory = category;
    m_installationActive = true;
    
    // Start installation using async job (non-blocking)
    auto *installJob = new Job<bool>([this]() -> bool {
        return this->performInstallationSync();
    }, this);
    
    connect(installJob, &JobBase::finished, this, [this, installJob](bool success) {
        m_installationActive = false;
        
        if (success) {
            bool result = installJob->result();
            if (result) {
                qDebug() << "[InstallationWorker] Installation completed successfully";
                emit installationCompleted(m_currentApp.id);
            } else {
                qDebug() << "[InstallationWorker] Installation failed";
                emit installationFailed(m_currentApp.id, "Installation process failed");
            }
        } else {
            qDebug() << "[InstallationWorker] Installation job failed";
            emit installationFailed(m_currentApp.id, "Installation job execution failed");
        }
        
        installJob->deleteLater();
    });
    
    emit installationProgress("Installation started...");
}

void InstallationWorker::cancelInstallation()
{
    if (!m_installationActive) return;
    
    qDebug() << "[InstallationWorker] Cancelling installation";
    m_installationActive = false;
    emit installationFailed(m_currentApp.id, "Installation cancelled by user");
}

bool InstallationWorker::performInstallationSync()
{
    try {
        qDebug() << "[InstallationWorker] performInstallationSync started";
        
        // Step 1: Prepare manifest
        emit installationProgress("Preparing installation manifest...");
        K3s::ManifestInfo manifest;
        if (!prepareManifest(m_currentApp, manifest)) {
            emit installationProgress("Failed to create manifest");
            return false;
        }
        
        qDebug() << "[InstallationWorker] Manifest prepared successfully";
        
        // Step 2: Check node readiness if required (simplified)
        if (manifest.isRemoteNode) {
            emit installationProgress("Checking remote node...");
            // Use a simple timeout-based approach
            QTimer timer;
            timer.setSingleShot(true);
            QEventLoop loop;
            
            auto *nodeJob = m_jobManager->checkNodeReady("vip", 3);
            bool nodeReady = false;
            
            connect(nodeJob, &JobBase::finished, &loop, [&](bool success) {
                if (success) {
                    nodeReady = nodeJob->result();
                }
                loop.quit();
            });
            
            connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            timer.start(5000); // 5 second timeout
            
            loop.exec();
            nodeJob->deleteLater();
            
            if (!nodeReady) {
                emit installationProgress("Remote node not ready");
                return false;
            }
            emit installationProgress("Remote node is ready");
        }
        
        // Step 3: Execute installation commands (simplified)
        emit installationProgress("Executing installation commands...");
        QStringList commands = buildInstallationCommands(m_currentApp, manifest);
        
        if (commands.isEmpty()) {
            emit installationProgress("No installation commands to execute");
            return false;
        }
        
        qDebug() << "[InstallationWorker] Executing commands:" << commands;
        
        // Use a simpler approach - execute commands directly
        bool commandsSuccess = executeCommandsDirectly(commands);
        
        if (!commandsSuccess) {
            emit installationProgress("Installation commands failed");
            return false;
        }
        
        emit installationProgress("Installation commands completed");
        
        // Step 4: Update tracking data
        emit installationProgress("Updating installation records...");
        updateInstallationRecord(m_currentApp, m_currentCategory);
        
        // Step 5: Simple cleanup
        emit installationProgress("Cleaning up...");
        cleanupInstallationJobs(m_currentApp.id);
        
        emit installationProgress("Installation completed successfully");
        qDebug() << "[InstallationWorker] performInstallationSync completed successfully";
        return true;
        
    } catch (const std::exception &e) {
        qWarning() << "[InstallationWorker] Exception in performInstallationSync:" << e.what();
        emit installationProgress(QString("Exception: %1").arg(e.what()));
        return false;
    }
}

bool InstallationWorker::prepareManifest(const AppInfo &app, K3s::ManifestInfo &manifest)
{
    try {
        qDebug() << "[InstallationWorker] Preparing manifest for:" << app.id;
        manifest = K3s::ManifestBuilder::write(app);
        return true;
    } catch (const std::exception &e) {
        qWarning() << "[InstallationWorker] Manifest preparation failed:" << e.what();
        return false;
    }
}

QStringList InstallationWorker::buildInstallationCommands(const AppInfo &app, const K3s::ManifestInfo &manifest)
{
    QStringList commands;
    
    if (manifest.isRemoteNode && !manifest.mirrorJobYaml.isEmpty()) {
        commands << QString("kubectl apply -f %1").arg(manifest.mirrorJobYaml);
        commands << QString("kubectl wait --for=condition=complete job/mirror-%1 --timeout=300s").arg(app.id);
    }
    
    if (!manifest.pullJobYaml.isEmpty()) {
        commands << QString("kubectl apply -f %1").arg(manifest.pullJobYaml);
        commands << QString("kubectl wait --for=condition=complete job/pull-%1 --timeout=600s").arg(app.id);
    }
    
    qDebug() << "[InstallationWorker] Built commands:" << commands;
    return commands;
}

bool InstallationWorker::executeCommandsDirectly(const QStringList &commands)
{
    bool allSuccess = true;
    
    for (int i = 0; i < commands.size(); ++i) {
        const QString &cmd = commands[i];
        qDebug() << "[InstallationWorker] Executing command" << (i+1) << "of" << commands.size() << ":" << cmd;
        
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start("/bin/bash", QStringList() << "-c" << cmd);
        
        if (!process.waitForStarted(3000)) {
            qWarning() << "[InstallationWorker] Failed to start command:" << cmd;
            allSuccess = false;
            break;  // Stop on critical failure
        }
        
        // Set timeout based on command type
        int timeout = 30000;  // default 30 seconds
        if (cmd.contains("kubectl wait")) {
            timeout = 120000;  // 2 minutes for wait commands
        }
        
        if (!process.waitForFinished(timeout)) {
            qWarning() << "[InstallationWorker] Command timed out after" << timeout << "ms:" << cmd;
            process.kill();
            process.waitForFinished(2000);  // Give it time to cleanup
            allSuccess = false;
            break;  // Stop on timeout
        }
        
        QString output = process.readAll().trimmed();
        int exitCode = process.exitCode();
        
        if (exitCode != 0) {
            qWarning() << "[InstallationWorker] Command failed with exit code" << exitCode << ":" << cmd;
            qWarning() << "[InstallationWorker] Command output:" << output;
            allSuccess = false;
            break;  // Stop on command failure
        }
        
        qDebug() << "[InstallationWorker] Command" << (i+1) << "completed successfully";
        
        // Brief pause between commands to avoid overwhelming the system
        if (i < commands.size() - 1) {
            QThread::msleep(500);  // 500ms pause
        }
    }
    
    qDebug() << "[InstallationWorker] Command execution result:" << (allSuccess ? "SUCCESS" : "FAILED");
    return allSuccess;
}

void InstallationWorker::updateInstallationRecord(const AppInfo &app, const QString &category)
{
    try {
        qDebug() << "[InstallationWorker] Updating installation record for:" << app.id;
        
        DataManager dm;
        QJsonArray arr = dm.load(category);
        
        bool exists = false;
        for (auto v : arr) {
            if (v.isObject() && v.toObject().value("id").toString() == app.id) {
                exists = true;
                break;
            }
        }
        
        if (!exists) {
            QJsonObject rec;
            rec["id"] = app.id;
            rec["name"] = app.name;
            rec["author"] = app.author;
            rec["rating"] = app.rating;
            rec["thumbnail"] = app.iconUrl;
            rec["installedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            arr.append(rec);
            dm.save(category, arr);
            qDebug() << "[InstallationWorker] Installation record updated";
        } else {
            qDebug() << "[InstallationWorker] Installation record already exists";
        }
    } catch (const std::exception &e) {
        qWarning() << "[InstallationWorker] Failed to update installation record:" << e.what();
        // Don't fail the installation for this
    }
}

void InstallationWorker::cleanupInstallationJobs(const QString &appId)
{
    try {
        qDebug() << "[InstallationWorker] Starting cleanup for:" << appId;
        
        QStringList cleanupCmds;
        cleanupCmds << QString("kubectl delete job mirror-%1 --ignore-not-found --timeout=10s").arg(appId);
        cleanupCmds << QString("kubectl delete job pull-%1 --ignore-not-found --timeout=10s").arg(appId);
        
        for (const QString &cmd : cleanupCmds) {
            qDebug() << "[InstallationWorker] Cleanup command:" << cmd;
            
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start("/bin/bash", QStringList() << "-c" << cmd);
            
            if (process.waitForStarted(2000)) {
                // Wait max 10 seconds for cleanup command
                if (process.waitForFinished(10000)) {
                    QString output = process.readAll().trimmed();
                    if (process.exitCode() == 0) {
                        qDebug() << "[InstallationWorker] Cleanup successful:" << cmd;
                    } else {
                        qDebug() << "[InstallationWorker] Cleanup warning (non-critical):" << output;
                    }
                } else {
                    qWarning() << "[InstallationWorker] Cleanup command timed out:" << cmd;
                    process.kill();
                    process.waitForFinished(1000);
                }
            } else {
                qWarning() << "[InstallationWorker] Failed to start cleanup command:" << cmd;
            }
        }
        
        qDebug() << "[InstallationWorker] Cleanup completed for:" << appId;
    } catch (const std::exception &e) {
        qWarning() << "[InstallationWorker] Cleanup exception (non-critical):" << e.what();
        // Never fail the overall process due to cleanup issues
    }
}

// Add lightweight node check method
bool InstallationWorker::checkNodeReadyQuick(const QString &nodeName)
{
    try {
        QString cmd = QString("kubectl get node %1 --no-headers 2>/dev/null | grep -q Ready").arg(nodeName);
        
        QProcess process;
        process.start("/bin/bash", QStringList() << "-c" << cmd);
        
        if (!process.waitForStarted(2000)) {
            qDebug() << "[InstallationWorker] Node check failed to start";
            return false;
        }
        
        if (!process.waitForFinished(5000)) {
            qDebug() << "[InstallationWorker] Node check timed out";
            process.kill();
            return false;
        }
        
        bool isReady = (process.exitCode() == 0);
        qDebug() << "[InstallationWorker] Node" << nodeName << "ready:" << isReady;
        return isReady;
        
    } catch (const std::exception &e) {
        qWarning() << "[InstallationWorker] Node check exception:" << e.what();
        return false;
    }
}

//-----------------------------------------------------------------------------
// MarketplaceViewModel - SIMPLIFIED VERSION
//-----------------------------------------------------------------------------
MarketplaceViewModel::MarketplaceViewModel(QObject *parent)
  : QObject(parent)
  , m_apps(new AppListModel(this))
  , m_cats(new CategoryListModel(this))
  , m_installWorker(new InstallationWorker(this))
{
    // Load categories
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/marketplaceselection.json";
    m_cats->loadFromJsonFile(cfg);

    // Connect installation worker signals
    connect(m_installWorker, &InstallationWorker::installationProgress,
            this, &MarketplaceViewModel::onInstallationProgress);
    connect(m_installWorker, &InstallationWorker::installationCompleted,
            this, &MarketplaceViewModel::onInstallationCompleted);
    connect(m_installWorker, &InstallationWorker::installationFailed,
            this, &MarketplaceViewModel::onInstallationFailed);
            
    qDebug() << "[MarketplaceViewModel] Initialized with InstallationWorker";
}

void MarketplaceViewModel::setCurrentCategory(int idx) {
    if (idx<0 || idx>=m_cats->rowCount()) return;
    if (m_currentCategory==idx) return;
    m_currentCategory = idx;
    emit currentCategoryChanged(idx);
    search(m_lastSearchTerm);
}

void MarketplaceViewModel::search(const QString &term)
{
    m_lastSearchTerm = term.isEmpty() ? QStringLiteral("vehicle") : term;
    m_apps->updateApps({});

    DataManager::FetchOptions opt;
    const QModelIndex mi = m_cats->index(m_currentCategory, 0);
    opt.marketUrl  = m_cats->data(mi, CategoryListModel::UrlRole).toString();
    opt.loginUrl   = m_cats->data(mi, CategoryListModel::LoginUrlRole).toString();
    opt.category   = m_lastSearchTerm;
    opt.page       = 1;
    opt.limit      = 100;
    opt.rootFolder = DK_CONTAINER_ROOT + "dk_marketplace/";

    if (m_searchJob) m_searchJob->deleteLater();
    m_searchJob = new Job<QList<AppInfo>>(
        [=](){ return DataManager::fetchAppList(opt); },
        this);

    connect(m_searchJob, &JobBase::finished,
            this, [this](bool ok){
        if (!ok) { emit searchError(); return; }

        const QList<AppInfo> apps = m_searchJob->result();
        if (apps.isEmpty()) {
            emit searchError();
            return;
        }

        QSet<QString> installed;
        DataManager dm;
        const QJsonArray arr = dm.load(m_lastSearchTerm);
        for (auto v : arr)
            if (v.isObject())
                installed.insert(v.toObject().value("id").toString());

        QList<AppInfo> finalList = apps;
        for (auto &a : finalList)
            a.isInstalled = installed.contains(a.id);

        m_lastApps = finalList;
        m_apps->updateApps(finalList);
        emit searchFinished();
        
        m_searchJob->deleteLater();
        m_searchJob = nullptr;
    });
}

void MarketplaceViewModel::appSelected(int idx) {
    QVariantMap info = m_apps->get(idx);

    if (idx < 0 || idx >= m_lastApps.size()) return;
    if (!info.value("isInstalled").toBool()) {
        m_pendingIndex   = idx;
        m_pendingName    = info.value("name").toString();
        m_installPending = true;
        m_installingIndex = idx;
        m_isInstalling   = false;
        emit pendingAppNameChanged(m_pendingName);
        emit installPendingChanged(true);
        emit installingIndexChanged(m_installingIndex);
        emit isInstallingChanged(false);
    }
}

void MarketplaceViewModel::confirmInstall()
{
    if (!m_installPending || m_pendingIndex < 0 || m_pendingIndex >= m_lastApps.size()) {
        qWarning() << "[MarketplaceViewModel] Invalid install state";
        return;
    }
    
    const AppInfo app = m_lastApps[m_pendingIndex];
    
    // Update UI state immediately 
    m_isInstalling = true;
    emit isInstallingChanged(true);
    
    // Start installation - this should work now
    m_installWorker->startInstallation(app, m_lastSearchTerm);
    
    qDebug() << "[MarketplaceViewModel] Started installation for:" << app.name;
}

void MarketplaceViewModel::cancelInstall() {
    if (!m_installPending) return;
    
    // Cancel ongoing installation
    m_installWorker->cancelInstallation();
    
    // Reset state
    m_installPending = false;
    m_isInstalling = false;
    m_installingIndex = -1;
    m_pendingIndex = -1;
    
    emit installPendingChanged(false);
    emit isInstallingChanged(false);
    emit installingIndexChanged(-1);
    
    qDebug() << "[MarketplaceViewModel] Installation cancelled";
}

void MarketplaceViewModel::onInstallationProgress(const QString &message)
{
    qDebug() << "[MarketplaceViewModel] Installation progress:" << message;
    emit installProgressChanged(message);
}

void MarketplaceViewModel::onInstallationCompleted(const QString &appId)
{
    qDebug() << "[MarketplaceViewModel] Installation completed for:" << appId;
    
    // Update UI state
    m_installPending = false;
    m_isInstalling = false;
    
    emit installPendingChanged(false);
    emit isInstallingChanged(false);
    
    // Update the app as installed
    if (m_pendingIndex >= 0) {
        m_apps->setAppInstalled(m_pendingIndex, true);
        emit installFinished();
    }
    
    // Reset state
    m_pendingIndex = -1;
    m_installingIndex = -1;
    emit installingIndexChanged(-1);
    
    NOTIFY_SUCCESS("Installation", "App installed successfully: " + appId);
}

void MarketplaceViewModel::onInstallationFailed(const QString &appId, const QString &error)
{
    qDebug() << "[MarketplaceViewModel] Installation failed for:" << appId << "Error:" << error;
    
    // Update UI state
    m_installPending = false;
    m_isInstalling = false;
    
    emit installPendingChanged(false);
    emit isInstallingChanged(false);
    emit installError();
    
    // Reset state
    m_pendingIndex = -1;
    m_installingIndex = -1;
    emit installingIndexChanged(-1);
    
    NOTIFY_ERROR("Installation", "Installation failed: " + error);
}