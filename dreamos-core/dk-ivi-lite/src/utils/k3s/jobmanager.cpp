#include "jobmanager.hpp"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include "../notifications/notificationmanager.hpp"

using namespace K3s;

// Static members
QMutex JobManager::s_instanceMutex;
JobManager* JobManager::s_instance = nullptr;

JobManager::JobManager(QObject *parent)
    : QObject(parent)
    , m_installer(new Installer(this))
    , m_mainThread(QThread::currentThread())
{
    connect(m_installer, &Installer::finished,
            this, &JobManager::onInstallerFinished);
}

JobManager::~JobManager()
{
    QMutexLocker locker(&s_instanceMutex);
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

JobManager* JobManager::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        // Create instance in main thread if possible
        if (QThread::currentThread() == qApp->thread()) {
            s_instance = new JobManager(qApp);
        } else {
            // If called from worker thread, create with no parent and move to main thread
            s_instance = new JobManager(nullptr);
            s_instance->moveToThread(qApp->thread());
        }
    }
    return s_instance;
}

template<typename T>
Async::Job<T>* JobManager::createJobSafely(std::function<T()> task)
{
    QMutexLocker locker(&m_mutex);
    
    try {
        // Always create jobs with no parent to avoid cross-thread parenting issues
        auto* job = new Async::Job<T>(task, nullptr);
        
        // If we're in the main thread, we can set this as parent after creation
        if (QThread::currentThread() == m_mainThread) {
            job->setParent(this);
        } else {
            // For worker threads, use QMetaObject::invokeMethod to set parent safely
            QMetaObject::invokeMethod(this, [this, job]() {
                if (job) { // Check if job still exists
                    job->setParent(this);
                }
            }, Qt::QueuedConnection);
        }
        
        return job;
        
    } catch (const std::bad_alloc &e) {
        qCritical() << "[JobManager] Memory allocation failed in createJobSafely:" << e.what();
        return nullptr;
    } catch (const std::exception &e) {
        qCritical() << "[JobManager] Exception in createJobSafely:" << e.what();
        return nullptr;
    }
}

Async::Chain* JobManager::createChainSafely()
{
    QMutexLocker locker(&m_mutex);
    
    Async::Chain* chain = nullptr;
    
    if (QThread::currentThread() == m_mainThread) {
        chain = new Async::Chain(this);
    } else {
        // Create with no parent first, then set parent in main thread
        chain = new Async::Chain(nullptr);
        QMetaObject::invokeMethod(this, [this, chain]() {
            chain->setParent(this);
        }, Qt::QueuedConnection);
    }
    
    return chain;
}

Async::Job<JobManager::JobResult>* JobManager::deployService(const DeploymentInfo &info)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Deploy %1").arg(info.name));
        
        try {
            // Check node ready if deploying
            if (info.subscribe) {
                bool nodeReady = false;
                try { 
                    nodeReady = Installer::nodeReady("vip", 5); 
                } catch(...) {}
                
                if (!nodeReady) {
                    result.errorMessage = "Worker node not ready. Deployment may fail.";
                    NOTIFY_WARNING("Deployment", result.errorMessage);
                }
            }
            
            // Prepare command
            const QString cmd = info.subscribe 
                ? QString("kubectl apply -f %1").arg(info.deploymentYaml)
                : QString("kubectl delete -f %1 --ignore-not-found").arg(info.deploymentYaml);
            
            // Execute command synchronously
            result = executeCommandsSync({cmd});
            
            const QString action = info.subscribe ? "deployed" : "stopped";
            const QString message = QString("Service '%1' %2").arg(info.name, action);
            
            emit jobFinished(QString("Deploy %1").arg(info.name), result.success, message);
            
            if (result.success) {
                NOTIFY_SUCCESS("Deployment", message);
            } else {
                NOTIFY_ERROR("Deployment", QString("Failed to %1 %2: %3")
                    .arg(action, info.name, result.errorMessage));
            }
            
        } catch (const std::exception &e) {
            result.success = false;
            result.errorMessage = QString("Exception: %1").arg(e.what());
            emit jobFinished(QString("Deploy %1").arg(info.name), false, result.errorMessage);
        }
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::undeployService(const DeploymentInfo &info)
{
    DeploymentInfo undeployInfo = info;
    undeployInfo.subscribe = false;
    return deployService(undeployInfo);
}

Async::Job<JobManager::JobResult>* JobManager::removeService(const QString &id, const QString &deploymentYaml)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        result.success = true;
        
        emit jobStarted(QString("Remove %1").arg(id));
        
        try {
            // Step 1: Delete deployment
            const QString deleteCmd = QString("kubectl delete -f %1 --ignore-not-found").arg(deploymentYaml);
            result = executeCommandsSync({deleteCmd});
            
            if (!result.success) {
                result.errorMessage = "kubectl delete failed";
            }
            
            emit jobFinished(QString("Remove %1").arg(id), result.success, 
                result.success ? QString("%1 removed").arg(id) : result.errorMessage);
            
        } catch (const std::exception &e) {
            result.success = false;
            result.errorMessage = QString("Exception: %1").arg(e.what());
            emit jobFinished(QString("Remove %1").arg(id), false, result.errorMessage);
        }
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::scaleDeployment(const QString &deploymentName, int replicas)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Scale %1 to %2 replicas").arg(deploymentName).arg(replicas));
        
        const QString cmd = QString("kubectl scale deployment %1 --replicas=%2 -n default")
            .arg(deploymentName).arg(replicas);
        
        result = executeCommandsSync({cmd});
        
        const QString message = result.success 
            ? QString("Scaled %1 to %2 replicas").arg(deploymentName).arg(replicas)
            : QString("Failed to scale %1").arg(deploymentName);
            
        emit jobFinished(QString("Scale %1").arg(deploymentName), result.success, message);
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::restartDeployment(const QString &deploymentName)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Restart %1").arg(deploymentName));
        
        const QString cmd = QString("kubectl rollout restart deployment/%1 -n default")
            .arg(deploymentName);
        
        result = executeCommandsSync({cmd});
        
        const QString message = result.success 
            ? QString("Restart initiated for %1").arg(deploymentName)
            : QString("Failed to restart %1").arg(deploymentName);
            
        emit jobFinished(QString("Restart %1").arg(deploymentName), result.success, message);
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::runInstallationCommands(const QStringList &commands)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted("Installation Commands");
        
        // Execute commands synchronously in GUI thread
        bool ok = false;
        QMetaObject::invokeMethod(this, [&]() {
            QEventLoop loop;
            connect(m_installer, &Installer::finished, &loop,
                   [&](bool success) { ok = success; loop.quit(); });
            m_installer->queueAndRun(commands);
            loop.exec();
        }, Qt::BlockingQueuedConnection);
        
        result.success = ok;
        if (!ok) {
            result.errorMessage = "Installation commands failed";
        }
        
        emit jobFinished("Installation Commands", result.success, 
            result.success ? "Installation completed" : result.errorMessage);
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::cleanupInstallationJobs(const QString &appId)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Cleanup %1").arg(appId));
        
        QStringList cleanupCmds;
        cleanupCmds << QString("kubectl delete job mirror-%1 --ignore-not-found").arg(appId)
                   << QString("kubectl delete job pull-%1 --ignore-not-found").arg(appId);
        
        // Execute in GUI thread
        QMetaObject::invokeMethod(this, [&]() {
            QEventLoop loop;
            connect(m_installer, &Installer::finished, &loop,
                   [&](bool) { loop.quit(); }); // Don't care about result for cleanup
            m_installer->queueAndRun(cleanupCmds);
            loop.exec();
        }, Qt::BlockingQueuedConnection);
        
        result.success = true; // Cleanup always "succeeds"
        
        emit jobFinished(QString("Cleanup %1").arg(appId), true, 
            QString("Cleanup completed for %1").arg(appId));
        
        return result;
    });
}

Async::Job<bool>* JobManager::checkNodeReady(const QString &nodeName, int timeoutSec)
{
    return createJobSafely<bool>([=]() -> bool {
        try {
            // Use a simpler, faster approach for node checking
            const QString cmd = QString("kubectl get node %1 --no-headers -o custom-columns=STATUS:.status.conditions[?(@.type==\"Ready\")].status 2>/dev/null | grep -q True; echo $?")
                .arg(nodeName);
            
            JobResult result = executeCommandsSync({cmd});
            
            // Check if the exit code indicates success (node ready)
            bool ready = result.success && result.output.trimmed() == "0";
            
            qDebug() << "[JobManager] Node" << nodeName << "ready check:"
                     << "Success:" << result.success 
                     << "Output:" << result.output.trimmed()
                     << "Ready:" << ready;
            
            return ready;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception in node ready check:" << e.what();
            return false;
        } catch (...) {
            qCritical() << "[JobManager] Unknown exception in node ready check";
            return false;
        }
    });
}

Async::Job<bool>* JobManager::checkDeploymentAvailable(const QString &deploymentId, int timeoutSec)
{
    return createJobSafely<bool>([=]() -> bool {
        bool available = false;
        try {
            available = Installer::deploymentAvailable(deploymentId, timeoutSec);
        } catch(...) {}
        return available;
    });
}

Async::Chain* JobManager::createAutoRestartChain(const QString &deploymentName)
{
    auto *chain = createChainSafely();
    
    // Step 1: Check if deployment exists - simplified
    auto deploymentExists = std::make_shared<bool>(false);
    
    chain->add([=]() -> bool {
        *deploymentExists = this->deploymentExists(deploymentName);
        return true; // Always continue
    });
    
    // Step 2: Scale down to 0
    chain->add([=]() -> bool {
        if (!*deploymentExists) {
            qDebug() << "[JobManager] Skipping scale down - deployment doesn't exist";
            return true;
        }
        
        qDebug() << "[JobManager] Scaling down deployment to 0 replicas";
        
        try {
            const QString cmd = QString("kubectl scale deployment %1 --replicas=0 -n default")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (result.success) {
                qDebug() << "[JobManager] Scale down successful";
                emit jobFinished(QString("Scale %1").arg(deploymentName), true, 
                    QString("Scaled %1 to 0 replicas").arg(deploymentName));
            } else {
                qWarning() << "[JobManager] Scale down failed:" << result.errorMessage;
                emit jobFinished(QString("Scale %1").arg(deploymentName), false, result.errorMessage);
            }
            
            return result.success;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Scale down exception:" << e.what();
            return false;
        }
    });
    
    // Step 3: Wait for deployment to scale down
    chain->add([=]() -> bool {
        if (!*deploymentExists) return true;
        
        qDebug() << "[JobManager] Waiting for deployment to scale down";
        bool scaled = waitForPodTermination(deploymentName, 30); // 30 second timeout
        
        if (!scaled) {
            qDebug() << "[JobManager] Deployment didn't scale down within timeout, force deleting pods...";
            forceDeletePods(deploymentName);
            QThread::sleep(5); // Wait after force delete
        }
        
        return true; // Always continue
    });
    
    // Step 4: Wait for clean state
    chain->add([=]() -> bool {
        if (!*deploymentExists) return true;
        
        qDebug() << "[JobManager] Waiting for clean state (3 seconds)";
        QThread::sleep(5);
        return true;
    });
    
    // Step 5: Scale back up to 1
    chain->add([=]() -> bool {
        if (!*deploymentExists) {
            qDebug() << "[JobManager] Skipping scale up - deployment doesn't exist";
            return true;
        }
        
        qDebug() << "[JobManager] Scaling up deployment to 1 replica";
        
        try {
            const QString cmd = QString("kubectl scale deployment %1 --replicas=1 -n default")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (result.success) {
                qDebug() << "[JobManager] Scale up successful";
                emit jobFinished(QString("Scale %1").arg(deploymentName), true, 
                    QString("Scaled %1 to 1 replica").arg(deploymentName));
            } else {
                qWarning() << "[JobManager] Scale up failed:" << result.errorMessage;
                emit jobFinished(QString("Scale %1").arg(deploymentName), false, result.errorMessage);
            }
            
            return result.success;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Scale up exception:" << e.what();
            return false;
        }
    });
    
    // Step 6: Wait for deployment to be ready
    chain->add([=]() -> bool {
        if (!*deploymentExists) return true;
        
        qDebug() << "[JobManager] Waiting for deployment to be ready";
        
        try {
            bool ready = waitForPodsReady(deploymentName, 60); // 60 second timeout for startup
            
            if (ready) {
                qDebug() << "[JobManager] Auto-restart sequence completed successfully";
                emit jobFinished(QString("Restart %1").arg(deploymentName), true, 
                    QString("Auto-restart completed successfully for %1").arg(deploymentName));
            } else {
                qWarning() << "[JobManager] Auto-restart completed but deployment may not be ready";
                emit jobFinished(QString("Restart %1").arg(deploymentName), false, 
                    QString("Auto-restart completed but deployment may not be ready for %1").arg(deploymentName));
            }
            
            return ready;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception in deployment ready check:" << e.what();
            emit jobFinished(QString("Restart %1").arg(deploymentName), false, 
                QString("Auto-restart failed: %1").arg(e.what()));
            return false;
        }
    });
    
    return chain;
}

void JobManager::onInstallerFinished(bool success)
{
    qDebug() << "[JobManager] Installer finished with result:" << success;
}

JobManager::JobResult JobManager::executeCommandsSync(const QStringList &commands)
{
    JobResult result;
    result.success = false;
    
    try {
        if (commands.isEmpty()) {
            result.errorMessage = "No commands provided";
            return result;
        }
        
        // For simple kubectl commands, use QProcess directly to ensure output capture
        QString command = commands.first();
        
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        
        // Set working directory and environment
        process.setWorkingDirectory("/tmp");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        process.setProcessEnvironment(env);
        
        qDebug() << "[JobManager] Executing command:" << command;
        
        // Start the process
        process.start("/bin/bash", QStringList() << "-c" << command);
        
        if (!process.waitForStarted(3000)) { // Reduced from 5000 to 3000
            result.errorMessage = "Failed to start process: " + process.errorString();
            return result;
        }
        
        // Reduced timeout for node checks to prevent hanging
        int timeout = command.contains("get node") ? 5000 : 30000; // 5s for node checks, 30s for others
        
        if (!process.waitForFinished(timeout)) {
            qWarning() << "[JobManager] Process timed out after" << timeout << "ms";
            process.kill();
            process.waitForFinished(1000); // Brief wait for cleanup
            result.errorMessage = "Process timed out";
            return result;
        }
        
        int exitCode = process.exitCode();
        QProcess::ExitStatus exitStatus = process.exitStatus();
        
        result.output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        
        result.success = (exitStatus == QProcess::NormalExit && exitCode == 0);
        
        if (!result.success) {
            result.errorMessage = QString("Process failed with exit code %1: %2")
                .arg(exitCode).arg(error.isEmpty() ? "Unknown error" : error);
        }
        
        qDebug() << "[JobManager] Command result - Success:" << result.success 
                 << "Exit code:" << exitCode
                 << "Output:'" << result.output << "'"
                 << "Error:'" << error << "'";
        
    } catch (const std::exception &e) {
        result.success = false;
        result.errorMessage = QString("Exception: %1").arg(e.what());
        qWarning() << "[JobManager] Exception in executeCommandsSync:" << e.what();
    } catch (...) {
        result.success = false;
        result.errorMessage = "Unknown error occurred";
        qCritical() << "[JobManager] Unknown exception in executeCommandsSync";
    }
    
    return result;
}

bool JobManager::waitForPodTermination(const QString &deploymentName, int maxWaitSec)
{
    qDebug() << "[JobManager] Waiting for deployment to scale down (max wait:" << maxWaitSec << "seconds)";
    
    int checkCount = 0;
    int maxChecks = maxWaitSec; // 1 check per second
    
    while (checkCount < maxChecks) {
        try {
            // Check deployment replicas
            const QString cmd = QString("kubectl get deployment %1 -n default -o jsonpath='{.status.replicas}' 2>/dev/null")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (result.success) {
                QString output = result.output.trimmed();
                qDebug() << "[JobManager] Deployment replicas:" << output;
                
                // If output is empty or "0", no replicas are running
                if (output.isEmpty() || output == "0") {
                    qDebug() << "[JobManager] Deployment scaled down after" << checkCount << "checks";
                    return true;
                }
            } else {
                qDebug() << "[JobManager] Deployment check failed:" << result.errorMessage;
                // If deployment doesn't exist, consider it terminated
                return true;
            }
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception during deployment check:" << e.what();
            return true;
        }
        
        checkCount++;
        if (checkCount % 10 == 0) {
            qDebug() << "[JobManager] Still waiting for deployment to scale down... (check" << checkCount << "/" << maxChecks << ")";
        }
        
        QThread::sleep(5);
    }
    
    qWarning() << "[JobManager] Deployment didn't scale down within" << maxWaitSec << "seconds";
    return false;
}

bool JobManager::forceDeletePods(const QString &deploymentName)
{
    qDebug() << "[JobManager] Force deleting pods for deployment:" << deploymentName;
    
    try {
        // Force delete all pods for this deployment
        const QString cmd = QString("kubectl delete pods -l app=%1 -n default --force --grace-period=0 --ignore-not-found")
            .arg(deploymentName);
        
        JobResult result = executeCommandsSync({cmd});
        
        if (result.success) {
            qDebug() << "[JobManager] Force delete completed successfully";
            // Wait a moment for cleanup to take effect
            QThread::sleep(5);
        } else {
            qWarning() << "[JobManager] Force delete failed:" << result.errorMessage;
        }
        
        return result.success;
        
    } catch (const std::exception &e) {
        qWarning() << "[JobManager] Exception during force delete:" << e.what();
        return false;
    }
}

bool JobManager::waitForPodsReady(const QString &deploymentName, int maxWaitSec)
{
    qDebug() << "[JobManager] Waiting for deployment to be ready (max wait:" << maxWaitSec << "seconds)";
    
    int checkCount = 0;
    int maxChecks = maxWaitSec; // 1 check per 10 second
    
    while (checkCount < maxChecks) {
        try {
            // Check deployment status in one command
            const QString cmd = QString("kubectl get deployment %1 -n default -o jsonpath='{.status.readyReplicas}/{.status.replicas}' 2>/dev/null")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            qDebug() << "[JobManager] Deployment status check - Success:" << result.success 
                     << "Output:'" << result.output.trimmed() << "'";
            
            if (result.success) {
                QString output = result.output.trimmed();
                
                if (!output.isEmpty() && output.contains('/')) {
                    QStringList parts = output.split('/');
                    if (parts.size() == 2) {
                        int ready = parts[0].toInt();
                        int total = parts[1].toInt();
                        
                        qDebug() << "[JobManager] Deployment status: Ready=" << ready << "Total=" << total;
                        
                        // Check if all replicas are ready and we have at least 1
                        if (ready > 0 && ready == total) {
                            qDebug() << "[JobManager] Deployment is ready with" << ready << "replicas after" << checkCount << "checks";
                            return true;
                        } else {
                            qDebug() << "[JobManager] Deployment not fully ready yet. Ready:" << ready << "Total=" << total;
                        }
                    } else {
                        qDebug() << "[JobManager] Could not parse deployment status:" << output;
                    }
                } else if (output.isEmpty()) {
                    qDebug() << "[JobManager] No deployment status yet, continuing to wait...";
                } else {
                    qDebug() << "[JobManager] Unexpected deployment status format:" << output;
                }
            } else {
                qDebug() << "[JobManager] Deployment status check failed:" << result.errorMessage;
                // Don't immediately fail - deployment might be starting
            }
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception during deployment ready check:" << e.what();
            return true; // Don't fail the chain
        } catch (...) {
            qCritical() << "[JobManager] Unknown exception during deployment ready check";
            return true;
        }
        
        checkCount++;
        if (checkCount % 10 == 0) {
            qDebug() << "[JobManager] Waiting for deployment to be ready... (check" << checkCount << "/" << maxChecks << ")";
        }
        
        QThread::sleep(10);
    }
    
    qWarning() << "[JobManager] Deployment may not be fully ready after" << maxWaitSec << "seconds";
    return true; // Don't fail the chain, just warn
}

bool JobManager::deploymentExists(const QString &deploymentName)
{
    try {
        const QString cmd = QString("kubectl get deployment %1 -n default --no-headers 2>/dev/null")
            .arg(deploymentName);
        
        JobResult result = executeCommandsSync({cmd});
        
        bool exists = result.success && !result.output.trimmed().isEmpty();
        qDebug() << "[JobManager] Deployment" << deploymentName << "exists:" << exists;
        
        return exists;
        
    } catch (const std::exception &e) {
        qWarning() << "[JobManager] Exception checking deployment existence:" << e.what();
        return false;
    }
}
