// k3s/installer.cpp
#include "installer.hpp"
#include <QDebug>

using namespace K3s;

Installer::Installer(QObject *p) : QObject(p)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    if (!path.contains("/usr/local/bin"))
        path += ":/usr/local/bin";
    env.insert("PATH", path);
    m_proc.setProcessEnvironment(env);

    m_proc.setProcessChannelMode(QProcess::MergedChannels);

    connect(&m_proc, &QProcess::started, this, [](){
        qDebug() << "[K3s::Installer] process started";
    });
    connect(&m_proc,
            static_cast<void(QProcess::*)(QProcess::ProcessError)>(&QProcess::errorOccurred),
            this, [this](QProcess::ProcessError e){
        qWarning() << "[K3s::Installer] errorOccurred:" << e
                   << m_proc.errorString();
        m_proc.kill();
        m_busy = false;
        emit busyChanged(false);
        emit finished(false);
    });
    connect(&m_proc,
            QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st){
        const bool ok = (st == QProcess::NormalExit && code == 0);
        qDebug() << "[K3s::Installer] step" << m_idx
                 << "finished; ok=" << ok
                 << "exit code:" << code
                 << "exit status:" << st;
        if (ok) runNext();
        else {
            m_busy = false;
            emit busyChanged(false);
            emit finished(false);
        }
    });
}

void Installer::queueAndRun(const QStringList &commands)
{
    if (m_busy) return;
    m_cmds = commands;
    m_idx  = 0;
    m_busy = true;
    emit busyChanged(true);
    runNext();
}

void Installer::runNext()
{
    if (m_idx >= m_cmds.size()) {
        qDebug() << "[K3s::Installer] all steps done.";
        m_busy = false;
        emit busyChanged(false);
        emit finished(true);
        return;
    }
    const QString cmd = m_cmds.at(m_idx++);
    qDebug() << "[K3s::Installer] running" << cmd;
    
    // Prepend a command to dump environment and then run the actual command
    QString fullCmd = QString("echo 'PATH:' $PATH; echo 'KUBECONFIG:' $KUBECONFIG; %1").arg(cmd);
    m_proc.start("bash", {"-l", "-c", fullCmd});
    
    // Wait briefly and log stderr
    if (!m_proc.waitForFinished(5000)) {
        qWarning() << "[K3s::Installer] Process timed out";
    }
    QByteArray err = m_proc.readAllStandardError();
    if (!err.isEmpty()) {
        qWarning() << "[K3s::Installer] stderr:" << err;
    }
}

/* static */
bool Installer::deploymentAvailable(const QString &deploymentId,
                                    int timeoutSec,
                                    QString *stdoutText)
{
    // 1) prepare env   make sure kubectl is found
    QProcess proc;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString p = env.value("PATH");
    if (!p.contains("/usr/local/bin"))
        p += ":/usr/local/bin";
    env.insert("PATH", p);
    proc.setProcessEnvironment(env);

    // 2) configure the command
    proc.setProgram("kubectl");
    proc.setArguments({ "wait",
                        "--for=condition=available",
                        "deployment/" + deploymentId,
                        QString("--timeout=%1s").arg(timeoutSec) });
    proc.setProcessChannelMode(QProcess::MergedChannels);

    // 3) run synchronously
    proc.start();
    if (!proc.waitForStarted(1000)) {
        qWarning() << "[Installer] kubectl did not start";
        return false;
    }
    proc.waitForFinished((timeoutSec + 1) * 1000);   // block

    // 4) collect output and evaluate result
    const QString out = QString::fromUtf8(proc.readAll()).trimmed();
    if (stdoutText)
        *stdoutText = out;

    const bool ok = proc.exitStatus() == QProcess::NormalExit
                    && proc.exitCode() == 0;

    if (!ok)
        qWarning() << "[Installer] kubectl wait failed for" << deploymentId
                   << "exitCode=" << proc.exitCode()
                   << "output:"   << out;

    return ok;
}

Async::Job<DeploymentCheck>*
Installer::deploymentAvailableAsync(const QString &id,
                                    int timeoutSec, QObject *parent)
{
    return new Async::Job<DeploymentCheck>(
        [=](){
            DeploymentCheck r;
            r.deploymentId = id;
            QString out;
            r.available = deploymentAvailable(id, timeoutSec, &out);
            r.output    = out;
            return r;
        },
        parent);
}
