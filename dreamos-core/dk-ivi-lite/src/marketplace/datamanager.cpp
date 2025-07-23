#include "datamanager.hpp"
#include "fetching.hpp"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDebug>
#include <QStandardPaths>
#include <QProcess>
#include <QProcessEnvironment>
#include <QHostInfo>

QJsonDocument DataManager::loadJsonFile(const QString &filePath,
                                        QJsonValue defaultValue)
{
    QFileInfo fi(filePath);
    QDir().mkpath(fi.path());

    // 1) If missing, write default and return it
    if (!fi.exists()) {
        QJsonDocument doc;
        if (defaultValue.isArray())
            doc = QJsonDocument(defaultValue.toArray());
        else
            doc = QJsonDocument(defaultValue.toObject());
        saveJsonFile(filePath, doc);
        return doc;
    }

    // 2) Try to read
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "loadJsonFile: cannot open" << filePath;
        // fallback
        if (defaultValue.isArray())
            return QJsonDocument(defaultValue.toArray());
        else
            return QJsonDocument(defaultValue.toObject());
    }
    QByteArray data = f.readAll();
    f.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        qWarning() << "loadJsonFile: invalid JSON in" << filePath;
        // fallback
        if (defaultValue.isArray())
            return QJsonDocument(defaultValue.toArray());
        else
            return QJsonDocument(defaultValue.toObject());
    }

    return doc;
}

bool DataManager::saveJsonFile(const QString &filePath,
                               const QJsonDocument &doc)
{
    QFileInfo fi(filePath);
    QDir().mkpath(fi.path());

    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "saveJsonFile: cannot write" << filePath;
        return false;
    }
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    return true;
}

//──────────── AppInfo ↔ JSON conversion ───────────────────────────────────
AppInfo DataManager::fromJson(const QJsonObject &o)
{
    AppInfo a;
    a.id          = o.value("_id").toString();
    a.name        = o.value("name").toString();
    if (o.value("storeId").isObject())
      a.author    = o.value("storeId").toObject()
                         .value("name").toString();
    a.rating      = o.value("rating").toDouble();
    a.downloads   = o.value("downloads").toInt();
    a.iconUrl     = o.value("thumbnail").toString();
    a.folderName  = a.id;
    a.packageLink = o.value("packageLink").toString();

    if (o.contains("dashboardConfig")
     && o.value("dashboardConfig").isString())
    {
        auto raw = 
          o.value("dashboardConfig").toString().toUtf8();
        auto cd  = QJsonDocument::fromJson(raw);
        if (cd.isObject())
            a.dashboardConfig =
              DashboardConfig::fromJson(cd.object());
        else
            qWarning()<<"DM::fromJson: bad dashCfg"
                     <<a.id;
    }
    return a;
}

QJsonObject DataManager::toJson(const AppInfo &app)
{
    QJsonObject o;
    o["_id"]       = app.id;
    o["name"]      = app.name;
    QJsonObject sid;
    sid["name"]    = app.author;
    o["storeId"]   = sid;
    o["rating"]    = app.rating;
    o["downloads"] = app.downloads;
    o["thumbnail"] = app.iconUrl;

    QJsonDocument cd(app.dashboardConfig.toJson());
    o["dashboardConfig"] =
      QString(cd.toJson(QJsonDocument::Compact));
    return o;
}

QList<AppInfo> DataManager::listFromJson(const QJsonArray &arr)
{
    QList<AppInfo> out;
    out.reserve(arr.size());
    for (auto v : arr)
      if (v.isObject())
        out.append(fromJson(v.toObject()));
    return out;
}

//─────────── Persist full list and per‐app configs ─────────────────────────
bool DataManager::saveAppList(const QList<AppInfo> &apps,
                              const QString &filePath)
{
    QJsonArray arr;
    for (auto &a : apps)
        arr.append(toJson(a));
    return saveJsonFile(filePath, QJsonDocument(arr));
}

bool DataManager::saveAppConfig(const AppInfo &app,
                                const QString &folderPath)
{
    QMutexLocker lk(&s_mutex);
    QString mirrorFn = "";

    // 1) ensure dir
    QString dirPath = folderPath + "/" + app.id;
    if (!QDir().mkpath(dirPath)) {
        qWarning() << "Failed to mkpath:" << dirPath;
    }

    // 2) write dashboard JSON
    QString jsonFn =
        QString("%1/%2_dashboard.json")
        .arg(dirPath, app.id);
    saveJsonFile(jsonFn,
                 QJsonDocument(app.dashboardConfig.toJson()));

    // 3) Determine the k3s node to target
    QString target = app.dashboardConfig.Target;
    QString image  = app.dashboardConfig.DockerImageURL;
    QString node;
    QString nodeXIP = "xip";
    QString nodeVIP = "vip";
    bool    isRemoteNode = true;

    if (target.isEmpty() || target == nodeXIP) {
        node = nodeXIP;
        isRemoteNode = false;
    } else {
        node = nodeVIP;
    }

    // Lower-case name
    QString lcName = app.name.toLower();

    // 4) Build env block (skip "node" and "args" keys)
    QStringList envLines;
    auto &rcfg = app.dashboardConfig.RuntimeCfg;
    for (auto it = rcfg.begin(); it != rcfg.end(); ++it) {
        const QString k = it.key();
        if (k == QLatin1String("node") || k == QLatin1String("args"))
            continue;
        QString v = it.value().toVariant().toString();
        envLines += QString(
            "            - name: %1\n"
            "              value: \"%2\"").arg(k, v);
    }
    QString envBlock = envLines.join("\n");
    if (envBlock.isEmpty())
        envBlock = "            # no environment variables";

    // 5) Build args block
    QStringList argLines;
    for (auto v : rcfg.value("args").toArray()) {
        QString s = v.toString();
        argLines += QString("           - \"%1\"").arg(s);
    }
    QString argBlock = argLines.join("\n");
    if (argBlock.isEmpty())
        argBlock = "           # no args";

    // 6) Write Deployment YAML
    const char *deployTpl = R"(apiVersion: apps/v1
kind: Deployment
metadata:
  name: ${name}
spec:
  replicas: 1
  selector:
    matchLabels:
      app: ${name}
  template:
    metadata:
      labels:
        app: ${name}
    spec:
      nodeSelector:
        kubernetes.io/hostname: ${node}
      hostNetwork: true
      containers:
      - name: ${name}
        image: ${image}
        env:
${env}
        args:
${args}
        securityContext:
          privileged: true
        tty: true
        stdin: true
)";
    QString deployYaml = QString(deployTpl)
        .replace("${name}",  lcName)
        .replace("${node}",  node)
        .replace("${image}", image)
        .replace("${env}",   envBlock)
        .replace("${args}",  argBlock);

    QString deployFn = folderPath + "/" + app.id + "/" + app.id + "_deployment.yaml";
    {
      QFile f(deployFn);
      if (f.open(QIODevice::WriteOnly)) {
        QTextStream(&f) << deployYaml;
        f.close();
      } else {
        qWarning() << "saveAppConfig: cannot write" << deployFn;
      }
    }
    // 4) prep names & images
    // if remote, rewrite image → localhost:5000/<rest>
    QString mirrorImage;
    if (isRemoteNode) {
        const auto parts = image.split('/', Qt::SkipEmptyParts);
        QString rest = (parts.size()>1
                        ? parts.mid(1).join('/')
                        : image);
        mirrorImage = QString("localhost:5000/%1").arg(rest);
    }

    // 5) if remote, also emit a skopeo‐based “mirror” Job
    if (isRemoteNode) {
        static const char *mirrorTpl = R"(apiVersion: batch/v1
kind: Job
metadata:
  name: pull-then-mirror
spec:
  backoffLimit: 1
  template:
    spec:
      hostNetwork: true
      nodeSelector:
        kubernetes.io/hostname: ${node}
      restartPolicy: Never
      initContainers:
      - name: pull-package
        image: ${src}
        command: ["true"]
      containers:
      - name: mirror-package
        image: quay.io/containers/skopeo:latest
        command: ["skopeo","copy"]
        args:
          - "--retry-times=3"
          - "--all"
          - "--dest-tls-verify=false"
          - "docker://${src}"
          - "docker://${dst}"
)";
        QString mirrorYaml = QString(mirrorTpl)
            .replace("${name}", lcName)
            .replace("${node}", nodeXIP)
            .replace("${src}",  image)
            .replace("${dst}",  mirrorImage);

        const QString mirrorFn = dirPath + "/" + app.id + "_mirror.yaml";
        QFile mf(mirrorFn);
        if (mf.open(QIODevice::WriteOnly)) {
            QTextStream(&mf) << mirrorYaml;
            mf.close();
            qDebug() << "Wrote mirror-job to" << mirrorFn;
        } else {
            qWarning() << "Cannot write" << mirrorFn;
        }
    }

    // 6) emit the “pull” Job YAML
    //    this will pull the image, then exit immediately (command "true")
    static const char *pullTpl = R"(apiVersion: batch/v1
kind: Job
metadata:
  name: pull-${name}
spec:
  template:
    spec:
      hostNetwork: true
      nodeSelector:
        kubernetes.io/hostname: ${node}
      restartPolicy: Never
      containers:
      - name: pull
        image: ${image}
        command: ["true"]
)";
    QString pullYaml = QString(pullTpl)
        .replace("${name}",  lcName)
        .replace("${node}",  node)
        .replace("${image}", image);

    const QString pullFn = dirPath + "/" + app.id + "_pull.yaml";
    {
        QFile f(pullFn);
        if (f.open(QIODevice::WriteOnly)) {
            QTextStream(&f) << pullYaml;
            f.close();
            qDebug() << "Wrote pull-job to" << pullFn;
        } else {
            qWarning() << "Cannot write" << pullFn;
        }
    }

    qDebug() << "[MarketPlace] Prepare Installation" << QString(
        "   - id  : %1\n"
        "   - node: %2\n"
        "   - url:  %3\n"
        "   - Deployment YAML: %4\n"
        "   - Mirror Job YAML: \"%5\"").arg(lcName, node, app.dashboardConfig.DockerImageURL, deployFn, mirrorFn);

    return true;
}

//──────────────────── façade: LOGIN→FETCH→LOAD→PARSE→PERSIST→RETURN ─────────
QList<AppInfo> DataManager::fetchAppList(const FetchOptions &opt)
{
    // 1) optional login
    QString token;
    if (!opt.loginUrl.isEmpty())
        token = marketplace_login(
                  opt.loginUrl,
                  opt.username,
                  opt.password);

    // 2) fetch ⇒ writes raw JSON array to:
    //    <rootFolder>/marketplace_data_installcfg.json
    bool ok = queryMarketplacePackages(
                opt.marketUrl,
                token,
                opt.page,
                opt.limit,
                opt.category);
    if (!ok) {
        qWarning()<<"DM::fetchAppList: fetch failed";
        return {};
    }

    // 3) load that file
    QString listPath = opt.rootFolder
                     + "/marketplace_data_installcfg.json";
    auto doc = loadJsonFile(listPath,
               QJsonValue(QJsonArray()));
    if (!doc.isArray()) {
        qWarning()<<"DM::fetchAppList: expected array in"
                 <<listPath;
        return {};
    }

    // 4) parse → AppInfo
    auto apps = listFromJson(doc.array());

    return apps;
}
