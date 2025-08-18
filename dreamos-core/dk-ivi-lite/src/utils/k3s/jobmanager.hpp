#pragma once
#include <QObject>
#include <QTimer>
#include <QProcess>
#include <QEventLoop>
#include <QMutex>
#include <QThread>
#include <memory>
#include "../async/asyncjob.hpp"
#include "installer.hpp"

namespace K3s {

/**
 * @brief Thread-safe manager for K3s job operations
 * 
 * This class encapsulates all K3s-related job operations and ensures
 * thread-safe creation and execution of async jobs.
 */
class JobManager : public QObject
{
    Q_OBJECT
    
public:
    struct JobResult {
        bool success = false;
        QString errorMessage;
        QString output;
    };
    
    struct DeploymentInfo {
        QString id;
        QString name;
        QString deploymentYaml;
        bool subscribe = false;
    };
    
    explicit JobManager(QObject *parent = nullptr);
    ~JobManager();
    
    // Thread-safe job creation methods
    Q_INVOKABLE Async::Job<JobResult>* deployService(const DeploymentInfo &info);
    Q_INVOKABLE Async::Job<JobResult>* undeployService(const DeploymentInfo &info);
    Q_INVOKABLE Async::Job<JobResult>* removeService(const QString &id, const QString &deploymentYaml);
    
    // Scaling operations
    Q_INVOKABLE Async::Job<JobResult>* scaleDeployment(const QString &deploymentName, int replicas);
    Q_INVOKABLE Async::Job<JobResult>* restartDeployment(const QString &deploymentName);
    
    // Installation operations
    Q_INVOKABLE Async::Job<JobResult>* runInstallationCommands(const QStringList &commands);
    Q_INVOKABLE Async::Job<JobResult>* cleanupInstallationJobs(const QString &appId);
    
    // Status checking
    Q_INVOKABLE Async::Job<bool>* checkNodeReady(const QString &nodeName = "vip", int timeoutSec = 5);
    Q_INVOKABLE Async::Job<bool>* checkDeploymentAvailable(const QString &deploymentId, int timeoutSec = 10);
    
    // Auto-restart functionality
    Q_INVOKABLE Async::Chain* createAutoRestartChain(const QString &deploymentName = "sdv-runtime");
    
    // Singleton access for thread-safe usage
    static JobManager* instance();
    
signals:
    void jobStarted(const QString &operation);
    void jobFinished(const QString &operation, bool success, const QString &message);
    void deploymentStatusChanged(const QString &deploymentId, bool available);

private slots:
    void onInstallerFinished(bool success);
    
private:
    // Thread-safe job creation helper
    template<typename T>
    Async::Job<T>* createJobSafely(std::function<T()> task);
    
    // Thread-safe chain creation helper
    Async::Chain* createChainSafely();
    
    // Helper methods
    JobResult executeCommandsSync(const QStringList &commands);
    bool waitForPodTermination(const QString &deploymentName, int maxWaitSec = 30);
    bool waitForPodsReady(const QString &deploymentName, int maxWaitSec = 180);
    bool forceDeletePods(const QString &deploymentName);
    bool deploymentExists(const QString &deploymentName);
    
    Installer *m_installer;
    QThread *m_mainThread;
    static QMutex s_instanceMutex;
    static JobManager *s_instance;
    
    mutable QMutex m_mutex;
};

} // namespace K3s
