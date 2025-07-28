#include "k8sdockerinstaller.h"
#include <QProcess>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QtConcurrent>

namespace AppManager {

K8sDockerInstaller::K8sDockerInstaller(IConfigurationProvider* config, QObject* parent)
    : IAppInstaller(parent), m_config(config)
{
}

QFuture<bool> K8sDockerInstaller::installApp(const AppMetadata& app, const QByteArray& packageData) {
    return QtConcurrent::run([this, app, packageData]() -> bool {
        emit installStarted(app.id);
        
        // Create installation directory
        QString installPath = m_config->getInstallPath() + "/" + app.id;
        QDir().mkpath(installPath);
        
        // Generate YAML files
        QString deploymentYaml = generateDeploymentYaml(app);
        QString pullJobYaml = generatePullJobYaml(app);
        
        // Save YAML files
        QString deploymentFile = installPath + "/" + app.id + "_deployment.yaml";
        QString pullJobFile = installPath + "/" + app.id + "_pull.yaml";
        
        QFile(deploymentFile).write(deploymentYaml.toUtf8());
        QFile(pullJobFile).write(pullJobYaml.toUtf8());
        
        // Execute installation steps
        struct InstallTask {
            QString appId;
            QStringList commands;
            int currentStep = 0;
        };

        InstallTask task;
        task.appId = app.id;
        task.commands = {
            QString("kubectl apply -f %1").arg(pullJobFile),
            QString("kubectl wait --for=condition=complete job/pull-%1 --timeout=300s").arg(app.id.toLower()),
            QString("kubectl delete job pull-%1").arg(app.id.toLower()),
            QString("kubectl apply -f %1").arg(deploymentFile)
        };
        
        // Execute commands sequentially
        for (const QString& cmd : task.commands) {
            QProcess process;
            process.start("sh", QStringList{"-c", cmd});
            
            if (!process.waitForFinished(30000)) { // 30 second timeout
                emit error(app.id, "Installation timeout: " + cmd);
                return false;
            }
            
            if (process.exitCode() != 0) {
                emit error(app.id, "Installation failed: " + process.readAllStandardError());
                return false;
            }
            
            emit installProgress(app.id, (task.currentStep * 100) / task.commands.size());
            task.currentStep++;
        }
        
        // Save installation info
        AppInstallInfo installInfo;
        installInfo.appId = app.id;
        installInfo.installPath = installPath;
        installInfo.installedAt = QDateTime::currentDateTime();
        installInfo.installerVersion = "1.0";
        installInfo.isActive = true;
        
        // Update tracking file
        QString trackingFile = m_config->getValue("trackingFile").toString();
        QJsonDocument doc;
        QJsonArray apps;
        
        QFile file(trackingFile);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) apps = doc.array();
            file.close();
        }
        
        apps.append(installInfo.toJson());
        
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(apps).toJson());
            file.close();
        }
        
        emit installCompleted(app.id, true);
        return true;
    });
}

QFuture<bool> K8sDockerInstaller::uninstallApp(const QString& appId) {
    return QtConcurrent::run([this, appId]() -> bool {
        QString installPath = m_config->getInstallPath() + "/" + appId;
        QString deploymentFile = installPath + "/" + appId + "_deployment.yaml";
        
        // Delete Kubernetes resources
        QProcess process;
        process.start("kubectl", QStringList{"delete", "-f", deploymentFile});
        
        if (!process.waitForFinished(30000)) {
            emit error(appId, "Uninstall timeout");
            return false;
        }
        
        // Remove installation directory
        QDir(installPath).removeRecursively();
        
        // Update tracking file
        QString trackingFile = m_config->getValue("trackingFile").toString();
        QJsonDocument doc;
        QJsonArray apps;
        
        QFile file(trackingFile);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                QJsonArray originalApps = doc.array();
                for (const auto& value : originalApps) {
                    QJsonObject app = value.toObject();
                    if (app.value("appId").toString() != appId) {
                        apps.append(app);
                    }
                }
            }
            file.close();
        }
        
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(apps).toJson());
            file.close();
        }
        
        emit uninstallCompleted(appId, true);
        return true;
    });
}

QFuture<QList<AppInstallInfo>> K8sDockerInstaller::getInstalledApps() {
    return QtConcurrent::run([this]() -> QList<AppInstallInfo> {
        QList<AppInstallInfo> installedApps;
        
        QString trackingFile = m_config->getValue("trackingFile").toString();
        QFile file(trackingFile);
        
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isArray()) {
                for (const auto& value : doc.array()) {
                    installedApps.append(AppInstallInfo::fromJson(value.toObject()));
                }
            }
            file.close();
        }
        
        return installedApps;
    });
}

QFuture<bool> K8sDockerInstaller::updateApp(const QString& appId, const QByteArray& packageData) {
    return QtConcurrent::run([this, appId, packageData]() -> bool {
        // For Kubernetes, update means recreate the deployment
        uninstallApp(appId).waitForFinished();
        
        // Get app metadata and reinstall
        // This would require access to the repository to get updated metadata
        // Implementation depends on specific requirements
        
        return true;
    });
}

QString K8sDockerInstaller::generateDeploymentYaml(const AppMetadata& app) const {
    // Extract configuration from extended properties
    QJsonObject dashboardConfig = app.extendedProperties.value("dashboardConfig").toObject();
    QString image = dashboardConfig.value("DockerImageURL").toString();
    QString target = dashboardConfig.value("Target").toString();
    QJsonObject runtimeCfg = dashboardConfig.value("RuntimeCfg").toObject();
    
    QString node = target.isEmpty() || target == "xip" ? "xip" : "vip";
    QString lcName = app.name.toLower();
    
    // Build environment variables
    QStringList envLines;
    for (auto it = runtimeCfg.begin(); it != runtimeCfg.end(); ++it) {
        if (it.key() != "node" && it.key() != "args") {
            envLines += QString("            - name: %1\n              value: \"%2\"")
                       .arg(it.key(), it.value().toString());
        }
    }
    QString envBlock = envLines.isEmpty() ? "            # no environment variables" : envLines.join("\n");
    
    // Build args
    QStringList argLines;
    QJsonArray args = runtimeCfg.value("args").toArray();
    for (const auto& arg : args) {
        argLines += QString("           - \"%1\"").arg(arg.toString());
    }
    QString argBlock = argLines.isEmpty() ? "           # no args" : argLines.join("\n");
    
    QString deploymentTemplate = R"(apiVersion: apps/v1
kind: Deployment
metadata:
  name: %1
spec:
  replicas: 1
  selector:
    matchLabels:
      app: %1
  template:
    metadata:
      labels:
        app: %1
    spec:
      nodeSelector:
        kubernetes.io/hostname: %2
      hostNetwork: true
      containers:
      - name: %1
        image: %3
        env:
%4
        args:
%5
        securityContext:
          privileged: true
        tty: true
        stdin: true
)";
    
    return deploymentTemplate.arg(lcName, node, image, envBlock, argBlock);
}

QString K8sDockerInstaller::generatePullJobYaml(const AppMetadata& app) const {
    QJsonObject dashboardConfig = app.extendedProperties.value("dashboardConfig").toObject();
    QString image = dashboardConfig.value("DockerImageURL").toString();
    QString target = dashboardConfig.value("Target").toString();
    QString node = target.isEmpty() || target == "xip" ? "xip" : "vip";
    QString lcName = app.name.toLower();
    
    QString pullTemplate = R"(apiVersion: batch/v1
kind: Job
metadata:
  name: pull-%1
spec:
  template:
    spec:
      hostNetwork: true
      nodeSelector:
        kubernetes.io/hostname: %2
      restartPolicy: Never
      containers:
      - name: pull
        image: %3
        command: ["true"]
)";
    
    return pullTemplate.arg(lcName, node, image);
}

} // namespace AppManager