#include "marketplace.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>

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
        def["name"]            = "Default Store";
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
                // Clear the busy overlay:
                if (m_isInstalling) {
                    m_isInstalling = false;
                    emit isInstallingChanged(false);
                }
                // If it ran to completion, mark installed:
                if (exitStatus == QProcess::NormalExit && exitCode == 0
                    && m_pendingIndex >= 0)
                {
                    m_apps->setAppInstalled(m_pendingIndex, true);
                    m_pendingIndex = -1;
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

void MarketplaceViewModel::search(const QString &term) {
    // pick a default term
    m_lastSearchTerm = term.isEmpty() ? QStringLiteral("vehicle") : term;
    // clear old rows
    m_apps->updateApps({});
    
    qDebug() << "MarketplaceViewModel::search:" << term;

    // pick URLs from your cat-model
    QModelIndex mi = m_cats->index(m_currentCategory,0);
    QString marketUrl = m_cats->data(mi, CategoryListModel::UrlRole).toString();
    QString loginUrl  = m_cats->data(mi, CategoryListModel::LoginUrlRole).toString();

    // optional login
    QString token;
    if (!loginUrl.isEmpty()) {
        // you’d gather user/pass from QSettings or a login dialog
        QString user = "";
        QString pass = "";
        token = marketplace_login(loginUrl, user, pass);
    }

    // synchronous fetch + JSON->disk
    bool ok = queryMarketplacePackages(marketUrl, token, /*page*/1,/*limit*/20, m_lastSearchTerm);
    if (!ok) {
        // show popup or just return
        return;
    }

    // read the file that fetching.cpp wrote
    QString dataPath = DK_CONTAINER_ROOT
                     + QStringLiteral("dk_marketplace/marketplace_data_installcfg.json");
    QFile f(dataPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    QList<AppInfo> out;
    if (doc.isArray()) {
        for (auto v : doc.array()) {
            if (!v.isObject()) continue;
            auto o = v.toObject();
            AppInfo a;
            a.id          = o["_id"].toString();
            a.name        = o["name"].toString();
            a.author      = o["storeId"].toObject()["name"].toString();
            a.rating      = o["rating"].toDouble();
            a.downloads   = o["downloads"].toInt();
            a.iconUrl     = o["thumbnail"].toString();
            a.folderName  = a.id;
            a.packageLink = o["dashboardConfig"].toString();
            a.isInstalled = false; 
            out.append(a);
        }
    }
    m_apps->updateApps(out);
}

void MarketplaceViewModel::prepareInstall(int idx) {
    QVariantMap info = m_apps->get(idx);
    if (!info.value("isInstalled").toBool()) {
        m_pendingIndex   = idx;
        m_pendingName    = info.value("name").toString();
        m_installPending = true;
        m_installingIndex = idx;
        emit pendingAppNameChanged(m_pendingName);
        emit installPendingChanged(true);
        emit installingIndexChanged(m_installingIndex);
    }
}

void MarketplaceViewModel::confirmInstall()
{
    if (!m_installPending || m_pendingIndex < 0)
        return;

    m_installPending = false;
    emit installPendingChanged(false);

    // Build the command string
    QString appId = m_apps->get(m_pendingIndex).value("id").toString();
    QString user  = qgetenv("DK_USER");
    QString cmd   = QStringLiteral(
        "docker run --rm --name dk_appinstall "
        "-v /home/%1/.dk:/app/.dk "
        "-v /var/run/docker.sock:/var/run/docker.sock "
        "-v /home/%1/.dk/dk_marketplace/%2_installcfg.json:/app/installCfg.json "
        "dk_appinstallservice:latest"
    ).arg(user, appId);
    
    qDebug() << " install cmd = " << cmd;

    // If an old process is still lingering, kill it:
    if (m_installer->state() != QProcess::NotRunning) {
        m_installer->kill();
        m_installer->waitForFinished(10000);
    }

    // Switch on the busy overlay:
    m_isInstalling = true;
    emit isInstallingChanged(true);
    emit installingIndexChanged(m_pendingIndex);

    // Launch via the shell so we can pass a single command string
    m_installer->start("sh", QStringList() << "-c" << cmd);

    // You can check immediately if it actually started:
    if (!m_installer->waitForStarted(3000)) {
        qWarning() << "[Installer] failed to start!";
        m_isInstalling = false;
        emit isInstallingChanged(false);
        emit installingIndexChanged(-1);
    }
}

void MarketplaceViewModel::cancelInstall() {
    if (!m_installPending) return;
    m_installPending = false;
    emit installPendingChanged(false);
    m_installingIndex = -1;
    emit installingIndexChanged(-1);
}
