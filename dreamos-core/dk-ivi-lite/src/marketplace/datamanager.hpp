#pragma once
#include <QString>
#include <QJsonDocument>
#include <QJsonArray>
#include <QList>
#include <QMutex>
#include "dashboardconfig.hpp"

struct AppInfo {
    QString id,name,author,iconUrl,folderName,packageLink;
    double  rating=0;
    int     downloads=0;
    bool    isInstalled=false;
    DashboardConfig dashboardConfig;
};

class DataManager {
  public:
    struct FetchOptions {
      QString marketUrl;
      QString loginUrl;     
      QString username;     
      QString password;     
      QString category;     
      int     page    = 1;
      int     limit   = 20;
      QString rootFolder;   
    };

    // façade: LOGIN→HTTP→dump raw JSON→parse→persist→return typed list
    static QList<AppInfo> fetchAppList(const FetchOptions &opt);

    // low‐level file I/O (+ default‐file creation)
    static QJsonDocument loadJsonFile( const QString &filePath,
                                       QJsonValue defaultValue = QJsonValue(QJsonArray()));
    static bool          saveJsonFile( const QString &filePath,
                                       const QJsonDocument &doc);
    static bool          saveAppConfig(const AppInfo &app,
                                      const QString &folderPath);

  private:
    // JSON↔AppInfo
    static AppInfo        fromJson(const QJsonObject &o);
    static QJsonObject    toJson(  const AppInfo   &app);
    static QList<AppInfo> listFromJson(const QJsonArray &arr);

    // persist full list and per‐app configs
    static bool saveAppList( const QList<AppInfo> &apps,
                             const QString         &filePath);

    inline static QMutex s_mutex;  // guards file + saveAppConfig
};
