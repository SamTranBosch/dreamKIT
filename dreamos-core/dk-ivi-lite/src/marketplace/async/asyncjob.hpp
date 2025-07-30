#pragma once
//
// Tiny header-only async framework
//   • Job<T>         (one-shot, returns T, emits finished(bool))
//   • Chain          (sequential list of Job<void>)
//
// Works with Qt 6 (AUTOMOC) because the Q_OBJECT macro lives in a
// non-templated base class.
//

#include <QObject>
#include <QFuture>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <functional>

namespace Async {

/* ------------------------------------------------------------------ */
/* 0) Non-templated base that owns the meta-object                     */
/* ------------------------------------------------------------------ */
class JobBase : public QObject
{
    Q_OBJECT
public:
    explicit JobBase(QObject *p = nullptr) : QObject(p) {}

signals:
    void finished(bool ok);              // true == task ran w/o exception
};

/* ------------------------------------------------------------------ */
/* 1) Generic job that returns T                                       */
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
        connect(&m_watcher,
                &QFutureWatcher<T>::finished,
                this,
                [this]() { emit finished(true); });
    }

    T result() const { return m_future.result(); }

private:
    QFuture<T>          m_future;
    QFutureWatcher<T>   m_watcher;
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
        m_future  = QtConcurrent::run(std::move(fn));   // QFuture<void>
        m_watcher.setFuture(m_future);
        connect(&m_watcher,
                &QFutureWatcher<void>::finished,
                this,
                [this]() { emit finished(true); });
    }

private:
    QFuture<void>        m_future;
    QFutureWatcher<void> m_watcher;
};

/* ------------------------------------------------------------------ */
/* 2)  Very small sequential chain (void jobs)                         */
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
        connect(job,
                &JobBase::finished,          // same signal for all Jobs
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
