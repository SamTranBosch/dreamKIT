#pragma once
#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <functional>

namespace Async {

/* ------------------------------------------------------------------ */
/* 0) meta-object base                                                */
/* ------------------------------------------------------------------ */
class JobBase : public QObject
{
    Q_OBJECT
public:
    explicit JobBase(QObject *p = nullptr) : QObject(p) {}
signals:
    void finished(bool ok);
};

/* ------------------------------------------------------------------ */
/* 1) Generic job  (T != void)                                         */
/* ------------------------------------------------------------------ */
template<class T>
class Job : public JobBase
{
public:
    using Fn = std::function<T()>;

    explicit Job(Fn fn, QObject *parent = nullptr)
        : JobBase(parent)
    {
        m_future  = QtConcurrent::run(std::move(fn));
        m_watcher.setFuture(m_future);

        connect(&m_watcher, &QFutureWatcher<T>::finished,
                this,
                [this]() {
            bool ok = true;
            try {
                m_result = m_future.result();      // may throw
            } catch (...) {
                ok = false;
            }
            emit finished(ok);
        });
    }

    T result() const { return m_result; }

private:
    QFuture<T>          m_future;
    QFutureWatcher<T>   m_watcher;
    T                   m_result {};
};

/* ------------------------------------------------------------------ */
/* 1b) Specialisation for void                                         */
/* ------------------------------------------------------------------ */
template<>
class Job<void> : public JobBase
{
public:
    using Fn = std::function<void()>;

    explicit Job(Fn fn, QObject *parent = nullptr)
        : JobBase(parent)
    {
        m_future  = QtConcurrent::run(std::move(fn));
        m_watcher.setFuture(m_future);

        connect(&m_watcher, &QFutureWatcher<void>::finished,
                this,
                [this]() {
            bool ok = !m_future.isCanceled();   // simple success flag
            emit finished(ok);
        });
    }

private:
    QFuture<void>        m_future;
    QFutureWatcher<void> m_watcher;
};

/* ------------------------------------------------------------------ */
/* 2) Sequential chain (void jobs)                                     */
/* ------------------------------------------------------------------ */
class Chain : public QObject
{
    Q_OBJECT
public:
    using Fn = Job<void>::Fn;

    explicit Chain(QObject *p = nullptr) : QObject(p) {}

    void add(Fn fn) { m_fns << std::move(fn); }

    void start()
    {
        if (m_idx >= m_fns.size()) { emit finished(true); return; }
        auto *job = new Job<void>(m_fns[m_idx], this);
        connect(job, &JobBase::finished,
                this,
                [this](bool ok){
            if (!ok) { emit finished(false); return; }
            ++m_idx; start();
        });
    }

signals:
    void finished(bool ok);

private:
    QList<Fn> m_fns;
    int       m_idx {0};
};

} // namespace Async
