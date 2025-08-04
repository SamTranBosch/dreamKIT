// k3s/manifestbuilder.cpp
#include "manifestbuilder.hpp"
#include "../core/jsonstorage.hpp"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDebug>

using namespace K3s;
using Core::JsonStorage;

static QString writeFile(const QString &fn, const QString &txt)
{
    QFile f(fn);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "ManifestBuilder: cannot write" << fn;
        return {};
    }
    QTextStream(&f) << txt;
    return fn;
}

ManifestInfo ManifestBuilder::write(const AppInfo &app)
{
    ManifestInfo info;
    QString rootDir = DK_CONTAINER_ROOT + "dk_marketplace/";
    info.dir = QString("%1/%2").arg(rootDir, app.id);
    QDir().mkpath(info.dir);

    // ── dashboard JSON ──────────────────────────────────────────────
    info.dashboardJson = QString("%1/%2_dashboard.json")
                             .arg(info.dir, app.id);
    JsonStorage::save(info.dashboardJson,
                      QJsonDocument(app.dashboardConfig.toJson()));

    // ── target / node decision ──────────────────────────────────────
    QString nodeXIP = "xip";
    QString nodeVIP = "vip";
    QString target  = app.dashboardConfig.Target;
    QString node    = (target.isEmpty() || target == nodeXIP)
                        ? nodeXIP : nodeVIP;
    info.isRemoteNode = (node == nodeVIP);
    info.deployNodeName = (info.isRemoteNode ? nodeVIP : nodeXIP);

    qDebug() << "[ManifestBuilder::write] Instaling on node:"
             << info.deployNodeName
             << "isRemoteNode:" << info.isRemoteNode;
    const QString lcName = app.name.toLower();
    const QString appId  = app.id;
    const QString image  = app.dashboardConfig.DockerImageURL;

    // ── environment list ────────────────────────────────────────────
    QStringList envLines;
    auto &rcfg = app.dashboardConfig.RuntimeCfg;
    for (auto it = rcfg.begin(); it != rcfg.end(); ++it) {
        if (it.key() == QLatin1String("node") ||
            it.key() == QLatin1String("args"))
            continue;
        envLines << QString(
            "            - name: %1\n"
            "              value: \"%2\"")
            .arg(it.key(), it.value().toVariant().toString());
    }
    const QString envBlock = envLines.isEmpty()
                           ? "            # no environment variables"
                           : envLines.join("\n");

    // ── args list ───────────────────────────────────────────────────
    QStringList argLines;
    for (auto v : rcfg.value("args").toArray())
        argLines << QString("           - \"%1\"").arg(v.toString());
    const QString argBlock = argLines.isEmpty()
                           ? "           # no args"
                           : argLines.join("\n");

    // ── deployment yaml ─────────────────────────────────────────────
    static const char *deployTpl = R"(apiVersion: apps/v1
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
            .replace("${name}",  appId)
            .replace("${node}",  node)
            .replace("${image}", image)
            .replace("${env}",   envBlock)
            .replace("${args}",  argBlock);

    info.deploymentYaml = writeFile(
        QString("%1/%2_deployment.yaml").arg(info.dir, app.id),
        deployYaml);

    // ── pull job yaml ───────────────────────────────────────────────
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
        imagePullPolicy: Always
        command: ["true"]
)";
    QString pullYaml = QString(pullTpl)
            .replace("${name}",  appId)
            .replace("${node}",  node)
            .replace("${image}", image);

    info.pullJobYaml = writeFile(
        QString("%1/%2_pull.yaml").arg(info.dir, app.id), pullYaml);

    // ── mirror job yaml (only if remote) ────────────────────────────
    if (info.isRemoteNode) {
        const auto parts = image.split('/', Qt::SkipEmptyParts);
        const QString rest = (parts.size() > 1 ? parts.mid(1).join('/') : image);
        const QString mirrorImg = QString("localhost:5000/%1").arg(rest);

        static const char *mirrorTpl = R"(apiVersion: batch/v1
kind: Job
metadata:
  name: mirror-${name}
spec:
  backoffLimit: 1
  template:
    spec:
      hostNetwork: true
      nodeSelector:
        kubernetes.io/hostname: ${node}
      restartPolicy: Never
      initContainers:
      - name: pull
        image: ${src}
        command: ["true"]
      containers:
      - name: mirror
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
                .replace("${name}",  appId)
                .replace("${node}",  nodeXIP)
                .replace("${src}",   image)
                .replace("${dst}",   mirrorImg);
        info.mirrorJobYaml = writeFile(
            QString("%1/%2_mirror.yaml").arg(info.dir, app.id), mirrorYaml);
    }
    return info;
}
