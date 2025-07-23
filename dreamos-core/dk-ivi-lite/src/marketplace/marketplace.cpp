#include "marketplace.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QHostInfo>
#include <QProcessEnvironment>

extern QString DK_VCU_USERNAME;
extern QString DK_ARCH;
extern QString DK_DOCKER_HUB_NAMESPACE;
extern QString DK_CONTAINER_ROOT;

//-----------------------------------------------------------------------------
// 1) AppListModel
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
// 2) CategoryListModel
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
        // create parent path + default entry
        QDir().mkpath(QFileInfo(filePath).path());
        QJsonArray arr;
        QJsonObject def;
        def["name"]            = "BGSV Marketplace";
        def["marketplace_url"] = "https://store-be.digitalauto.tech";
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
// 3) MarketplaceViewModel
//-----------------------------------------------------------------------------
MarketplaceViewModel::MarketplaceViewModel(QObject *parent)
  : QObject(parent)
  , m_apps(new AppListModel(this))
  , m_cats(new CategoryListModel(this))
  , m_installer(new QProcess(this))
{
    // … load categories, do initial search, etc …
    // 1) load the file you shipped or created at runtime
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/marketplaceselection.json";
    m_cats->loadFromJsonFile(cfg);

    // Merge stderr/stdout so we can see errors in one channel:
    m_installer->setProcessChannelMode(QProcess::MergedChannels);

    // Log when the process actually starts:
    connect(m_installer, &QProcess::started, this, [this](){
        qDebug() << "[Installer] process started";
    });

    // If the process itself fails to launch:
    connect(m_installer,
            static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::errorOccurred),
            this,
            [this](QProcess::ProcessError err){
                qWarning() << "[Installer] errorOccurred:" << err
                           << m_installer->errorString();
                if (m_isInstalling) {
                    m_isInstalling = false;
                    emit isInstallingChanged(false);
                }
            });

    // When the process finishes (either success or failure):
    connect(m_installer,
            QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus){
                qDebug() << "[Installer] finished, code=" << exitCode
                         << "status=" << exitStatus;
            });

    // Merge stderr/stdout
    m_installer->setProcessChannelMode(QProcess::MergedChannels);

    // When one step finishes, decide what to do next:
    connect(m_installer,
            QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus){
        qDebug() << "[Installer] step" << (m_installCmdIndex)
                 << "finished with code=" << exitCode;
        if (exitStatus==QProcess::NormalExit && exitCode==0) {
            runNextInstallCommand();  // go to the next
        }
        else {
            qWarning() << "[Installer] FAILED at step"
                       << m_installCmdIndex
                       << "code=" << exitCode
                       << "\noutput:\n"
                       << m_installer->readAll();
            // abort and clear busy flag
            m_isInstalling = false;
            emit isInstallingChanged(false);
        }
    });
}

void MarketplaceViewModel::setCurrentCategory(int idx) {
    if (idx<0 || idx>=m_cats->rowCount()) return;
    if (m_currentCategory==idx) return;
    m_currentCategory = idx;
    emit currentCategoryChanged(idx);
    // re-search in new category
    search(m_lastSearchTerm);
}

void MarketplaceViewModel::search(const QString &term)
{
    // pick a default term
    m_lastSearchTerm = term.isEmpty()
                     ? QStringLiteral("vehicle")
                     : term;
    m_apps->updateApps({});  // clear

    // 1) build FetchOptions
    DataManager::FetchOptions opt;
    QModelIndex mi = m_cats->index(m_currentCategory,0);
    opt.marketUrl  = m_cats->data(mi, CategoryListModel::UrlRole).toString();
    opt.loginUrl   = m_cats->data(mi, CategoryListModel::LoginUrlRole).toString();
    opt.username   = "";
    opt.password   = "";
    opt.category   = m_lastSearchTerm;
    opt.page       = 1;
    opt.limit      = 20;
    opt.rootFolder = DK_CONTAINER_ROOT + "dk_marketplace/";

    // 2) fetch + parse + persist via DataManager
    QList<AppInfo> apps = DataManager::fetchAppList(opt);
    if (apps.isEmpty()) {
        // show error / return
        qDebug() << "DataManager::fetchAppList: with apps.isEmpty()" << term;
        return;
    }

    // 3) load the tracking file and collect installed IDs
    QString trackFile;
    if (opt.category == QLatin1String("vehicle")) {
        trackFile = opt.rootFolder + "/installedapps.json";
    }
    else if (opt.category == QLatin1String("vehicle-service")) {
        trackFile = opt.rootFolder + "/installedservices.json";
    }
    // else: no tracking for other categories

    if (!trackFile.isEmpty()) {
        auto doc = DataManager::loadJsonFile(
                     trackFile,
                     QJsonValue(QJsonArray()));
        QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

        QSet<QString> installedIds;
        for (auto v : arr) {
            if (!v.isObject()) continue;
            installedIds.insert(v.toObject()
                                   .value("id").toString());
        }
        // mark them
        for (auto &a : apps) {
            if (installedIds.contains(a.id))
                a.isInstalled = true;
        }
    }

    // 4) update the model and remember apps for later installs
    m_lastApps = apps;
    m_apps->updateApps(apps);
}

void MarketplaceViewModel::appSelected(int idx) {
    QVariantMap info = m_apps->get(idx);

    if (idx < 0 || idx >= m_lastApps.size()) return;
    if (!info.value("isInstalled").toBool()) {
        // 1) your existing “ask for confirmation” logic
        m_pendingIndex   = idx;
        m_pendingName    = info.value("name").toString();
        m_installPending = true;
        m_installingIndex = idx;
        emit pendingAppNameChanged(m_pendingName);
        emit installPendingChanged(true);
        emit installingIndexChanged(m_installingIndex);
    }
}

void MarketplaceViewModel::prepareInstall(int idx) {
    // QVariantMap info = m_apps->get(idx);

    // if (idx < 0 || idx >= m_lastApps.size()) return;
    // if (!info.value("isInstalled").toBool()) {
        // 2) pick the right tracking file
        QString folder = DK_CONTAINER_ROOT + "dk_marketplace/";
        QString trackFile;
        if (m_lastSearchTerm == QLatin1String("vehicle")) {
            trackFile = folder + "installedapps.json";
        }
        else if (m_lastSearchTerm == QLatin1String("vehicle-service")) {
            trackFile = folder + "installedservices.json";
        }
        else {
            return;  // no tracking for other categories
        }
    
        // 3) load or create the JSON array
        QJsonDocument doc = DataManager::loadJsonFile(
                            trackFile,
                            QJsonValue(QJsonArray()));
        QJsonArray arr = doc.isArray() ? doc.array() : QJsonArray();

        // 4) build the new record
        const AppInfo &app = m_lastApps[idx];
        QString        id  = app.id;

        // 5) only append if we haven’t already installed it
        bool already = false;
        for (auto v : arr) {
            if (!v.isObject()) continue;
            if (v.toObject().value("id").toString() == id) {
                already = true;
                break;
            }
        }
        if (!already) {
            QJsonObject rec;
            rec["id"]          = app.id;
            rec["name"]        = app.name;
            rec["author"]      = app.author;
            rec["rating"]      = app.rating;
            rec["iconPath"]    = app.iconUrl;
            rec["thumbnail"]   = app.iconUrl;
            rec["installedAt"] = QDateTime::currentDateTime()
                                    .toString(Qt::ISODate);
            arr.append(rec);

            // 6) write the updated array back to disk
            DataManager::saveJsonFile(trackFile,
                                    QJsonDocument(arr));
        }

        // 7) now *also* emit the per‐app JSON+YAML via DataManager
        //    (this was previously in fetchAppList)
        DataManager::saveAppConfig(app, folder);
    // }
}

void MarketplaceViewModel::confirmInstall()
{
    if (!m_installPending || m_pendingIndex < 0
        || m_pendingIndex >= m_lastApps.size())
        return;
    // 
    prepareInstall(m_installingIndex);

    // clear “Pending” dialog in UI
    m_installPending = false;
    emit installPendingChanged(false);

    // figure out node & YAML names exactly as in saveAppConfig()
    const AppInfo &app = m_lastApps[m_pendingIndex];
    QString lcName   = app.name.toLower();
    QString baseDir  = DK_CONTAINER_ROOT + "dk_marketplace/" + app.id;
    QString target   = app.dashboardConfig.Target;
    QString node;

    // resolve node same as before...
    if (target.isEmpty() || target == "xip") {
        node = "xip";
    } else {
        node = "vip";
    }

    bool remote = (node != "xip");
    qDebug() << "[Installer] remote:" << remote;

    QString yaml_pull       = QString("%1/%2_pull.yaml").arg(baseDir, app.id);
    QString yaml_mirror     = QString("%1/%2_mirror.yaml").arg(baseDir, app.id);
    QString jobName_pull    = QString("pull-%1").arg(lcName);
    QString jobName_mirror  = QString("mirror-%1").arg(lcName);
    
    // build our three‐step queue
    m_installCommands.clear();
    m_installCommands << QString("kubectl apply -f %1").arg(yaml_pull)
                      << QString("kubectl wait --for=condition=complete job/%1 --timeout=300s")
                           .arg(jobName_pull)
                      << QString("kubectl delete job %1").arg(jobName_pull);
    if(remote){
        m_installCommands << QString("kubectl apply -f %1").arg(yaml_mirror)
                          << QString("kubectl wait --for=condition=complete job/%1 --timeout=300s")
                               .arg(jobName_mirror)
                          << QString("kubectl delete job %1").arg(jobName_mirror);
    }
    m_installCmdIndex = 0;

    // show busy spinner in QML
    m_isInstalling = true;
    emit isInstallingChanged(true);

    // fire off step #1
    runNextInstallCommand();
}

void MarketplaceViewModel::runNextInstallCommand()
{
    // if we've exhausted the queue, we're done
    if (m_installCmdIndex >= m_installCommands.size()) {
        qDebug() << "[Installer] ALL STEPS DONE";
        m_isInstalling = false;
        emit isInstallingChanged(false);

        // mark installed
        if (m_pendingIndex >= 0) {
            m_apps->setAppInstalled(m_pendingIndex, true);
            m_pendingIndex = -1;
        }
        return;
    }

    // grab & run the next shell line
    const QString cmd = m_installCommands.at(m_installCmdIndex++);
    qDebug() << "[Installer] RUNNING STEP" << m_installCmdIndex << ":" << cmd;
    m_installer->start("sh", QStringList{ "-c", cmd });
}

void MarketplaceViewModel::cancelInstall() {
    if (!m_installPending) return;
    m_installPending = false;
    emit installPendingChanged(false);
    m_installingIndex = -1;
    emit installingIndexChanged(-1);
}
