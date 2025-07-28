#include "k8sdockerruntime.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtConcurrent>

namespace AppManager {

K8sDockerRuntime::K8sDockerRuntime(IConfigurationProvider* config, QObject* parent)
    : IAppRuntime(parent), m_config(config)
{
}

QFuture<bool> K8sDockerRuntime::startApp(const QString& appId) {
    return QtConcurrent::run([this, appId]() -> bool {
        QString deploymentFile = m_config->getInstallPath() + "/" + appId + "/" + appId + "_deployment.yaml";

        QProcess process;
        process.start("kubectl", QStringList{"apply", "-f", deploymentFile});

        if (!process.waitForFinished(30000)) {
            emit error(appId, "Start timeout");
            return false;
        }

        if (process.exitCode() == 0) {
            emit appStarted(appId);
            emit appStatusChanged(appId, AppStatus::Running);
            return true;
        } else {
            emit error(appId, "Failed to start: " + process.readAllStandardError());
            return false;
        }
    });
}

QFuture<bool> K8sDockerRuntime::stopApp(const QString& appId) {
    return QtConcurrent::run([this, appId]() -> bool {
        QString deploymentFile = m_config->getInstallPath() + "/" + appId + "/" + appId + "_deployment.yaml";

        QProcess process;
        process.start("kubectl", QStringList{"delete", "-f", deploymentFile});

        if (!process.waitForFinished(30000)) {
            emit error(appId, "Stop timeout");
            return false;
        }

        if (process.exitCode() == 0) {
            emit appStopped(appId);
            emit appStatusChanged(appId, AppStatus::Stopped);
            return true;
        } else {
            emit error(appId, "Failed to stop: " + process.readAllStandardError());
            return false;
        }
    });
}

QFuture<AppStatus> K8sDockerRuntime::getAppStatus(const QString& appId) {
    return QtConcurrent::run([this, appId]() -> AppStatus {
        QString lcName = appId.toLower();

        QProcess process;
        process.start("kubectl", QStringList{"get", "deployment", lcName, "-o", "json"});

        if (!process.waitForFinished(10000)) {
            return AppStatus::Unknown;
        }

        if (process.exitCode() != 0) {
            return AppStatus::Stopped;
        }

        QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
        QJsonObject deployment = doc.object();
        QJsonObject status = deployment.value("status").toObject();

        int readyReplicas = status.value("readyReplicas").toInt();
        int replicas = status.value("replicas").toInt();

        if (readyReplicas > 0 && readyReplicas == replicas) {
            return AppStatus::Running;
        } else if (replicas > 0) {
            return AppStatus::Installing; // Pods are starting
        } else {
            return AppStatus::Stopped;
        }
    });
}

QFuture<QList<QString>> K8sDockerRuntime::getRunningApps() {
    return QtConcurrent::run([this]() -> QList<QString> {
        QList<QString> runningApps;

        QProcess process;
        process.start("kubectl", QStringList{"get", "deployments", "-o", "json"});

        if (!process.waitForFinished(10000)) {
            return runningApps;
        }

        if (process.exitCode() == 0) {
            QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput());
            QJsonObject result = doc.object();
            QJsonArray items = result.value("items").toArray();

            for (const auto& item : items) {
                QJsonObject deployment = item.toObject();
                QJsonObject metadata = deployment.value("metadata").toObject();
                QString name = metadata.value("name").toString();

                QJsonObject status = deployment.value("status").toObject();
                int readyReplicas = status.value("readyReplicas").toInt();

                if (readyReplicas > 0) {
                    runningApps.append(name);
                }
            }
        }

        return runningApps;
    });
}

bool K8sDockerRuntime::supportsApp(const AppMetadata& app) const {
    // Check if the app has Kubernetes/Docker configuration
    QJsonObject dashboardConfig = app.extendedProperties.value("dashboardConfig").toObject();
    return !dashboardConfig.value("DockerImageURL").toString().isEmpty();
}

}
