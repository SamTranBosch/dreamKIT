#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDir>
#include <QProcess>
#include <QHostInfo>
#include <QProcessEnvironment>

#include "marketplace.hpp"
#include "core/datamanager.hpp"
#include "k3s/manifestbuilder.hpp"
#include "k3s/installer.hpp"

using namespace Async;
using K3s::ManifestBuilder;
using K3s::Installer;

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
  , m_installer(new Installer(this))
{
    // … load categories, do initial search, etc …
    // 1) load the file you shipped or created at runtime
    QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/marketplaceselection.json";
    m_cats->loadFromJsonFile(cfg);

    connect(m_installer, &Installer::busyChanged,
        this, [this](bool b){ m_isInstalling = b;
                               emit isInstallingChanged(b); });
    connect(m_installer, &Installer::finished,
            this, [this](bool ok){
        if (ok && m_pendingIndex >= 0)
            m_apps->setAppInstalled(m_pendingIndex, true);
        m_pendingIndex = -1;
        m_installingIndex = -1;
        emit installingIndexChanged(-1);
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

/* ─────────────────────────────────────────────────────────── */
/*  Asynchronous search                                       */
/* ─────────────────────────────────────────────────────────── */
void MarketplaceViewModel::search(const QString &term)
{
    // keep the filter text
    m_lastSearchTerm = term.isEmpty()
                     ? QStringLiteral("vehicle")
                     : term;

    // clear list immediately so UI reacts
    m_apps->updateApps({});

    // --- build FetchOptions --------------------------------
    DataManager::FetchOptions opt;
    const QModelIndex mi = m_cats->index(m_currentCategory, 0);
    opt.marketUrl  = m_cats->data(mi, CategoryListModel::UrlRole)      .toString();
    opt.loginUrl   = m_cats->data(mi, CategoryListModel::LoginUrlRole) .toString();
    opt.category   = m_lastSearchTerm;
    opt.page       = 1;
    opt.limit      = 20;
    opt.rootFolder = DK_CONTAINER_ROOT + "dk_marketplace/";

    // --- run fetch in background ---------------------------
    if (m_searchJob) m_searchJob->deleteLater();            // cancel old one
    m_searchJob = new Job<QList<AppInfo>>(
        [=](){ return DataManager::fetchAppList(opt); },    // runs in thread
        this);

    connect(m_searchJob, &JobBase::finished,
            this, [this](bool ok){
        if (!ok) { emit searchError(); return; }

        const QList<AppInfo> apps = m_searchJob->result();
        if (apps.isEmpty()) {
            qWarning() << "[search] no result";
            emit searchError();
            return;
        }

        // ---------------- mark already installed ----------
        QSet<QString> installed;
        DataManager dm;
        const QJsonArray arr = dm.load(m_lastSearchTerm);
        for (auto v : arr)
            if (v.isObject())
                installed.insert(v.toObject().value("id").toString());

        QList<AppInfo> finalList = apps;
        for (auto &a : finalList)
            a.isInstalled = installed.contains(a.id);

        // ---------------- update model on GUI thread -------
        m_lastApps = finalList;
        m_apps->updateApps(finalList);
        emit searchFinished();
    });
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

/* ─────────────────────────────────────────────────────────── */
/*  Asynchronous (sequential) confirmInstall                  */
/* ─────────────────────────────────────────────────────────── */
void MarketplaceViewModel::confirmInstall()
{
    if (!m_installPending) return;
    const int idx = m_installingIndex;
    const AppInfo app = m_lastApps[idx];         // copy for threads

    /* -------- build kubectl command list *later* (see below) ---- */

    if (m_installChain) m_installChain->deleteLater();
    m_installChain = new Chain(this);

    /* ----------------------------------------------------------- *
     * step-0  : heavy disk-IO (worker thread)                     *
     * ----------------------------------------------------------- */
    m_installChain->add([this, idx](){
        confirmInstallPre(idx);
    });

    /* ----------------------------------------------------------- *
     * step-1  : build cmds + run kubectl in GUI thread            *
     *           (BlockingQueuedConnection guarantees sequential)  *
     * ----------------------------------------------------------- */
    m_installChain->add([this, app](){

        QStringList cmds;
        cmds  << QString("kubectl delete job pull-%1 --ignore-not-found")
                   .arg(app.name.toLower())
              << QString("kubectl apply -f %1")
                   .arg(m_lastManifest.pullJobYaml)
              << QString("kubectl wait --for=condition=complete "
                         "job/pull-%1 --timeout=3000s")
                   .arg(app.name.toLower())
              << QString("kubectl delete job pull-%1 --ignore-not-found")
                   .arg(app.name.toLower());

        if (m_lastManifest.isRemoteNode) {
            cmds << QString("kubectl delete job mirror-%1 --ignore-not-found")
                        .arg(app.name.toLower())
                 << QString("kubectl apply -f %1")
                        .arg(m_lastManifest.mirrorJobYaml)
                 << QString("kubectl wait --for=condition=complete "
                            "job/mirror-%1 --timeout=300s")
                        .arg(app.name.toLower())
                 << QString("kubectl delete job mirror-%1 --ignore-not-found")
                        .arg(app.name.toLower());
        }

        /* --- jump to GUI thread but block until it finishes --- */
        QMetaObject::invokeMethod(
            qApp,                                     // target (GUI thread)
            [this, cmds]() {
                QEventLoop loop;
                connect(m_installer, &K3s::Installer::finished,
                        &loop, &QEventLoop::quit, Qt::QueuedConnection);
                m_installer->queueAndRun(cmds);       // GUI thread safe
                loop.exec();                          // waits for finished()
            },
            Qt::BlockingQueuedConnection);            // <── THIS is the trick
    });

    /* ----------------------------------------------------------- *
     * step-2  : post update (worker thread)                       *
     * ----------------------------------------------------------- */
    m_installChain->add([this, idx](){
        confirmInstallPost(idx);
    });

    /* -------------- final result ------------------------------- */
    connect(m_installChain, &Chain::finished,
            this, [this](bool ok){
        m_installPending = false;
        emit installPendingChanged(false);
        if (ok)  emit installFinished();
        else     emit installError();

        m_installChain->deleteLater();
        m_installChain = nullptr;
    });

    m_installChain->start();
}

void MarketplaceViewModel::confirmInstallPre(int idx)
{
    // update tracking json
    DataManager dm;
    QJsonArray arr = dm.load(m_lastSearchTerm);

    const AppInfo &app = m_lastApps[idx];
    bool exists = false;
    for (auto v : arr)
        if (v.isObject() && v.toObject().value("id").toString() == app.id)
            { exists = true; break; }

    if (!exists) {
        m_lastManifest = ManifestBuilder::write(app);
    }
}

void MarketplaceViewModel::confirmInstallPost(int idx)
{
    // update tracking json
    DataManager dm;
    QJsonArray arr = dm.load(m_lastSearchTerm);
    qDebug() << "[MarketplaceViewModel::confirmInstallPost] Loaded " << m_lastSearchTerm;

    const AppInfo &app = m_lastApps[idx];
    bool exists = false;
    for (auto v : arr)
        if (v.isObject() && v.toObject().value("id").toString() == app.id)
            { exists = true; break; }

    if (!exists) {
        QJsonObject rec;
        rec["id"]   = app.id;
        rec["name"] = app.name;
        rec["author"] = app.author;
        rec["rating"] = app.rating;
        rec["thumbnail"] = app.iconUrl;
        rec["installedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        arr.append(rec);
        // save the updated array
        dm.save(m_lastSearchTerm, arr);
        qDebug() << "[MarketplaceViewModel::confirmInstallPost] save the updated array";
    }
}

void MarketplaceViewModel::cancelInstall() {
    if (!m_installPending) return;
    m_installPending = false;
    emit installPendingChanged(false);
    m_installingIndex = -1;
    emit installingIndexChanged(-1);
}
