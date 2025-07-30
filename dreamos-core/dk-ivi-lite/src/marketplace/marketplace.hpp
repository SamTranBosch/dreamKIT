#pragma once
#include <QObject>
#include <QAbstractListModel>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>

// bring in your existing fetch helpers:
#include "async/asyncjob.hpp"

#include "core/fetching.hpp"
#include "core/datamanager.hpp"

#include "k3s/manifestbuilder.hpp"
#include "k3s/installer.hpp"

class AppListModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Roles {
      IdRole = Qt::UserRole+1,
      NameRole, AuthorRole, RatingRole,
      DownloadsRole, IconRole,
      InstalledRole, FolderRole, PackageLinkRole
    };
    explicit AppListModel(QObject* parent=nullptr);

    // QAbstractListModel overrides
    int rowCount(const QModelIndex& = QModelIndex()) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int,QByteArray> roleNames() const override;

    // helper to read out a full row as a map
    Q_INVOKABLE QVariantMap get(int row) const;

    // update the list wholesale
    void updateApps(const QList<AppInfo>& apps);
    // mark one item installed
    void setAppInstalled(int index, bool installed);

  private:
    QList<AppInfo> m_apps;
};

class CategoryListModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Roles {
      NameRole      = Qt::UserRole+1,
      UrlRole,
      LoginUrlRole
    };
    explicit CategoryListModel(QObject* parent=nullptr);

    int rowCount(const QModelIndex& = QModelIndex()) const override;
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int,QByteArray> roleNames() const override;

    // load (or create + load) your JSON of marketplaces
    Q_INVOKABLE void loadFromJsonFile(const QString& filePath);

  private:
    struct Info { QString name, url, loginUrl; };
    QList<Info> m_list;
};

class MarketplaceViewModel : public QObject {
    Q_OBJECT

    // expose our two models + state
    Q_PROPERTY(AppListModel*       appsModel        READ appsModel       CONSTANT)
    Q_PROPERTY(CategoryListModel*  categoriesModel  READ categoriesModel CONSTANT)
    Q_PROPERTY(int                 currentCategory  READ currentCategory WRITE setCurrentCategory NOTIFY currentCategoryChanged)
    Q_PROPERTY(bool                isInstalling     READ isInstalling    NOTIFY isInstallingChanged)
    Q_PROPERTY(int                 installingIndex  READ installingIndex NOTIFY installingIndexChanged)
    Q_PROPERTY(bool                installPending   READ installPending  NOTIFY installPendingChanged)
    Q_PROPERTY(QString             pendingAppName   READ pendingAppName  NOTIFY pendingAppNameChanged)

  public:
    explicit MarketplaceViewModel(QObject* parent=nullptr);

    AppListModel*      appsModel() const       { return m_apps; }
    CategoryListModel* categoriesModel() const { return m_cats; }
    int                currentCategory() const { return m_currentCategory; }
    bool               isInstalling() const    { return m_isInstalling; }
    int                installingIndex() const { return m_installingIndex; }
    bool               installPending() const  { return m_installPending; }
    QString            pendingAppName() const  { return m_pendingName; }

  public slots:
    // called by QML
    void search(const QString& term);
    void setCurrentCategory(int idx);   // setter for Q_PROPERTY
    void appSelected(int idx);
    void confirmInstall();
    void confirmInstallPre(int idx);
    void confirmInstallPost(int idx);
    void cancelInstall();

  signals:
    void currentCategoryChanged(int);
    void isInstallingChanged(bool);
    void installingIndexChanged(int newIndex);
    void installPendingChanged(bool);
    void pendingAppNameChanged(const QString&);
    // 
    void searchFinished();
    void searchError();
    void installFinished();
    void installError();

  private:
    AppListModel*      m_apps         = nullptr;
    CategoryListModel* m_cats         = nullptr;
    QList<AppInfo>     m_lastApps;

    Async::Job<QList<AppInfo>>     *m_searchJob  = nullptr;
    Async::Chain                   *m_installChain = nullptr;

    K3s::Installer      *m_installer = nullptr;
    K3s::ManifestInfo    m_lastManifest;

    QStringList        m_installCommands;     // queue of kubectl steps
    int                m_installCmdIndex{0};  // current step index
    void               runNextInstallCommand();

    int     m_currentCategory = 0;
    bool    m_isInstalling    = false;
    int     m_installingIndex = -1;
    bool    m_installPending  = false;
    QString m_pendingName;
    int     m_pendingIndex    = -1;
    QString m_lastSearchTerm;
};
