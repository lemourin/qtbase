/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtCore module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QFUTURE_H
#error Do not include qfuture_impl.h directly
#endif

#if 0
#pragma qt_sync_skip_header_check
#pragma qt_sync_stop_processing
#endif

#include <QtCore/qglobal.h>
#include <QtCore/qfutureinterface.h>
#include <QtCore/qthreadpool.h>

QT_BEGIN_NAMESPACE

//
// forward declarations
//
template<class T>
class QFuture;
template<class T>
class QFutureInterface;

namespace QtFuture {
enum class Launch { Sync, Async, Inherit };
}

namespace QtPrivate {

template<class T>
using EnableForVoid = std::enable_if_t<std::is_same_v<T, void>>;

template<class T>
using EnableForNonVoid = std::enable_if_t<!std::is_same_v<T, void>>;

template<typename F, typename Arg, typename Enable = void>
struct ResultTypeHelper
{
};

// The callable takes an argument of type Arg
template<typename F, typename Arg>
struct ResultTypeHelper<
        F, Arg, typename std::enable_if_t<!std::is_invocable_v<std::decay_t<F>, QFuture<Arg>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Arg>>;
};

// The callable takes an argument of type QFuture<Arg>
template<class F, class Arg>
struct ResultTypeHelper<
        F, Arg, typename std::enable_if_t<std::is_invocable_v<std::decay_t<F>, QFuture<Arg>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>, QFuture<Arg>>;
};

// The callable takes an argument of type QFuture<void>
template<class F>
struct ResultTypeHelper<
        F, void, typename std::enable_if_t<std::is_invocable_v<std::decay_t<F>, QFuture<void>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>, QFuture<void>>;
};

// The callable doesn't take argument
template<class F>
struct ResultTypeHelper<
        F, void, typename std::enable_if_t<!std::is_invocable_v<std::decay_t<F>, QFuture<void>>>>
{
    using ResultType = std::invoke_result_t<std::decay_t<F>>;
};

// Helpers to resolve argument types of callables.
template<typename...>
struct ArgsType;

template<typename Arg, typename... Args>
struct ArgsType<Arg, Args...>
{
    using First = Arg;
    static const bool HasExtraArgs = (sizeof...(Args) > 0);
    using AllArgs =
            std::conditional_t<HasExtraArgs, std::tuple<std::decay_t<Arg>, std::decay_t<Args>...>,
                               std::decay_t<Arg>>;

    template<class Class, class Callable>
    static const bool CanInvokeWithArgs = std::is_invocable_v<Callable, Class, Arg, Args...>;
};

template<>
struct ArgsType<>
{
    using First = void;
    static const bool HasExtraArgs = false;
    using AllArgs = void;

    template<class Class, class Callable>
    static const bool CanInvokeWithArgs = std::is_invocable_v<Callable, Class>;
};

template<typename F>
struct ArgResolver : ArgResolver<decltype(&std::decay_t<F>::operator())>
{
};

template<typename R, typename... Args>
struct ArgResolver<R(Args...)> : public ArgsType<Args...>
{
};

template<typename R, typename... Args>
struct ArgResolver<R (*)(Args...)> : public ArgsType<Args...>
{
};

template<typename R, typename... Args>
struct ArgResolver<R (&)(Args...)> : public ArgsType<Args...>
{
};

template<typename Class, typename R, typename... Args>
struct ArgResolver<R (Class::*)(Args...)> : public ArgsType<Args...>
{
};

template<typename Class, typename R, typename... Args>
struct ArgResolver<R (Class::*)(Args...) noexcept> : public ArgsType<Args...>
{
};

template<typename Class, typename R, typename... Args>
struct ArgResolver<R (Class::*)(Args...) const> : public ArgsType<Args...>
{
};

template<typename Class, typename R, typename... Args>
struct ArgResolver<R (Class::*)(Args...) const noexcept> : public ArgsType<Args...>
{
};

template<class Class, class Callable>
using EnableIfInvocable = std::enable_if_t<
        QtPrivate::ArgResolver<Callable>::template CanInvokeWithArgs<Class, Callable>>;

template<class>
struct isTuple : std::false_type
{
};
template<class... T>
struct isTuple<std::tuple<T...>> : std::true_type
{
};
template<class T>
inline constexpr bool isTupleV = isTuple<T>::value;

template<typename Function, typename ResultType, typename ParentResultType>
class Continuation
{
public:
    Continuation(Function &&func, const QFuture<ParentResultType> &f,
                 const QFutureInterface<ResultType> &p)
        : promise(p), parentFuture(f), function(std::forward<Function>(func))
    {
    }
    virtual ~Continuation() = default;

    bool execute();

    static void create(Function &&func, QFuture<ParentResultType> *f,
                       QFutureInterface<ResultType> &p, QtFuture::Launch policy);

    static void create(Function &&func, QFuture<ParentResultType> *f,
                       QFutureInterface<ResultType> &p, QThreadPool *pool);

private:
    void fulfillPromiseWithResult();
    void fulfillVoidPromise();
    void fulfillPromiseWithVoidResult();

    template<class... Args>
    void fulfillPromise(Args &&... args);

protected:
    virtual void runImpl() = 0;

    void runFunction();

protected:
    QFutureInterface<ResultType> promise;
    QFuture<ParentResultType> parentFuture;
    Function function;
};

template<typename Function, typename ResultType, typename ParentResultType>
class SyncContinuation final : public Continuation<Function, ResultType, ParentResultType>
{
public:
    SyncContinuation(Function &&func, const QFuture<ParentResultType> &f,
                     const QFutureInterface<ResultType> &p)
        : Continuation<Function, ResultType, ParentResultType>(std::forward<Function>(func), f, p)
    {
    }

    ~SyncContinuation() override = default;

private:
    void runImpl() override { this->runFunction(); }
};

template<typename Function, typename ResultType, typename ParentResultType>
class AsyncContinuation final : public QRunnable,
                                public Continuation<Function, ResultType, ParentResultType>
{
public:
    AsyncContinuation(Function &&func, const QFuture<ParentResultType> &f,
                      const QFutureInterface<ResultType> &p, QThreadPool *pool = nullptr)
        : Continuation<Function, ResultType, ParentResultType>(std::forward<Function>(func), f, p),
          threadPool(pool)
    {
        this->promise.setRunnable(this);
    }

    ~AsyncContinuation() override = default;

private:
    void runImpl() override // from Continuation
    {
        QThreadPool *pool = threadPool ? threadPool : QThreadPool::globalInstance();
        pool->start(this);
    }

    void run() override // from QRunnable
    {
        this->runFunction();
    }

private:
    QThreadPool *threadPool;
};

#ifndef QT_NO_EXCEPTIONS

template<class Function, class ResultType>
class FailureHandler
{
public:
    static void create(Function &&function, QFuture<ResultType> *future,
                       const QFutureInterface<ResultType> &promise);

    FailureHandler(Function &&func, const QFuture<ResultType> &f,
                   const QFutureInterface<ResultType> &p)
        : promise(p), parentFuture(f), handler(std::forward<Function>(func))
    {
    }

public:
    void run();

private:
    template<class ArgType>
    void handleException();
    void handleAllExceptions();

private:
    QFutureInterface<ResultType> promise;
    QFuture<ResultType> parentFuture;
    Function handler;
};

#endif

template<typename Function, typename ResultType, typename ParentResultType>
void Continuation<Function, ResultType, ParentResultType>::runFunction()
{
    promise.reportStarted();

    Q_ASSERT(parentFuture.isFinished());

#ifndef QT_NO_EXCEPTIONS
    try {
#endif
        if constexpr (!std::is_void_v<ResultType>) {
            if constexpr (std::is_void_v<ParentResultType>) {
                fulfillPromiseWithVoidResult();
            } else if constexpr (std::is_invocable_v<Function, ParentResultType>) {
                fulfillPromiseWithResult();
            } else {
                // This assert normally should never fail, this is to make sure
                // that nothing unexpected happend.
                static_assert(std::is_invocable_v<Function, QFuture<ParentResultType>>,
                              "The continuation is not invocable with the provided arguments");
                fulfillPromise(parentFuture);
            }
        } else {
            if constexpr (std::is_void_v<ParentResultType>) {
                if constexpr (std::is_invocable_v<Function, QFuture<void>>)
                    function(parentFuture);
                else
                    function();
            } else if constexpr (std::is_invocable_v<Function, ParentResultType>) {
                fulfillVoidPromise();
            } else {
                // This assert normally should never fail, this is to make sure
                // that nothing unexpected happend.
                static_assert(std::is_invocable_v<Function, QFuture<ParentResultType>>,
                              "The continuation is not invocable with the provided arguments");
                function(parentFuture);
            }
        }
#ifndef QT_NO_EXCEPTIONS
    } catch (...) {
        promise.reportException(std::current_exception());
    }
#endif
    promise.reportFinished();
}

template<typename Function, typename ResultType, typename ParentResultType>
bool Continuation<Function, ResultType, ParentResultType>::execute()
{
    Q_ASSERT(parentFuture.isFinished());

    if (parentFuture.isCanceled()) {
#ifndef QT_NO_EXCEPTIONS
        if (parentFuture.d.exceptionStore().hasException()) {
            // If the continuation doesn't take a QFuture argument, propagate the exception
            // to the caller, by reporting it. If the continuation takes a QFuture argument,
            // the user may want to catch the exception inside the continuation, to not
            // interrupt the continuation chain, so don't report anything yet.
            if constexpr (!std::is_invocable_v<std::decay_t<Function>, QFuture<ParentResultType>>) {
                promise.reportStarted();
                promise.reportException(parentFuture.d.exceptionStore().exception());
                promise.reportFinished();
                return false;
            }
        } else
#endif
        {
            promise.reportStarted();
            promise.reportCanceled();
            promise.reportFinished();
            return false;
        }
    }

    runImpl();
    return true;
}

template<typename Function, typename ResultType, typename ParentResultType>
void Continuation<Function, ResultType, ParentResultType>::create(Function &&func,
                                                                  QFuture<ParentResultType> *f,
                                                                  QFutureInterface<ResultType> &p,
                                                                  QtFuture::Launch policy)
{
    Q_ASSERT(f);

    QThreadPool *pool = nullptr;

    bool launchAsync = (policy == QtFuture::Launch::Async);
    if (policy == QtFuture::Launch::Inherit) {
        launchAsync = f->d.launchAsync();

        // If the parent future was using a custom thread pool, inherit it as well.
        if (launchAsync && f->d.threadPool()) {
            pool = f->d.threadPool();
            p.setThreadPool(pool);
        }
    }

    Continuation<Function, ResultType, ParentResultType> *continuationJob = nullptr;
    if (launchAsync) {
        continuationJob = new AsyncContinuation<Function, ResultType, ParentResultType>(
                std::forward<Function>(func), *f, p, pool);
    } else {
        continuationJob = new SyncContinuation<Function, ResultType, ParentResultType>(
                std::forward<Function>(func), *f, p);
    }

    p.setLaunchAsync(launchAsync);

    auto continuation = [continuationJob, launchAsync]() mutable {
        bool isLaunched = continuationJob->execute();
        // If continuation is successfully launched, AsyncContinuation will be deleted
        // by the QThreadPool which has started it. Synchronous continuation will be
        // executed immediately, so it's safe to always delete it here.
        if (!(launchAsync && isLaunched)) {
            delete continuationJob;
            continuationJob = nullptr;
        }
    };

    f->d.setContinuation(std::move(continuation));
}

template<typename Function, typename ResultType, typename ParentResultType>
void Continuation<Function, ResultType, ParentResultType>::create(Function &&func,
                                                                  QFuture<ParentResultType> *f,
                                                                  QFutureInterface<ResultType> &p,
                                                                  QThreadPool *pool)
{
    Q_ASSERT(f);

    auto continuationJob = new AsyncContinuation<Function, ResultType, ParentResultType>(
            std::forward<Function>(func), *f, p, pool);
    p.setLaunchAsync(true);
    p.setThreadPool(pool);

    auto continuation = [continuationJob]() mutable {
        bool isLaunched = continuationJob->execute();
        // If continuation is successfully launched, AsyncContinuation will be deleted
        // by the QThreadPool which has started it.
        if (!isLaunched) {
            delete continuationJob;
            continuationJob = nullptr;
        }
    };

    f->d.setContinuation(continuation);
}

template<typename Function, typename ResultType, typename ParentResultType>
void Continuation<Function, ResultType, ParentResultType>::fulfillPromiseWithResult()
{
    if constexpr (std::is_copy_constructible_v<ParentResultType>)
        fulfillPromise(parentFuture.result());
    else
        fulfillPromise(parentFuture.takeResult());
}

template<typename Function, typename ResultType, typename ParentResultType>
void Continuation<Function, ResultType, ParentResultType>::fulfillVoidPromise()
{
    if constexpr (std::is_copy_constructible_v<ParentResultType>)
        function(parentFuture.result());
    else
        function(parentFuture.takeResult());
}

template<typename Function, typename ResultType, typename ParentResultType>
void Continuation<Function, ResultType, ParentResultType>::fulfillPromiseWithVoidResult()
{
    if constexpr (std::is_invocable_v<Function, QFuture<void>>)
        fulfillPromise(parentFuture);
    else
        fulfillPromise();
}

template<typename Function, typename ResultType, typename ParentResultType>
template<class... Args>
void Continuation<Function, ResultType, ParentResultType>::fulfillPromise(Args &&... args)
{
    if constexpr (std::is_copy_constructible_v<ResultType>)
        promise.reportResult(std::invoke(function, std::forward<Args>(args)...));
    else
        promise.reportAndMoveResult(std::invoke(function, std::forward<Args>(args)...));
}

template<class T>
void fulfillPromise(QFutureInterface<T> &promise, QFuture<T> &future)
{
    if constexpr (!std::is_void_v<T>) {
        if constexpr (std::is_copy_constructible_v<T>)
            promise.reportResult(future.result());
        else
            promise.reportAndMoveResult(future.takeResult());
    }
}

template<class T, class Function>
void fulfillPromise(QFutureInterface<T> &promise, Function &&handler)
{
    if constexpr (std::is_void_v<T>)
        handler();
    else if constexpr (std::is_copy_constructible_v<T>)
        promise.reportResult(handler());
    else
        promise.reportAndMoveResult(handler());
}

#ifndef QT_NO_EXCEPTIONS

template<class Function, class ResultType>
void FailureHandler<Function, ResultType>::create(Function &&function, QFuture<ResultType> *future,
                                                  const QFutureInterface<ResultType> &promise)
{
    Q_ASSERT(future);

    FailureHandler<Function, ResultType> *failureHandler = new FailureHandler<Function, ResultType>(
            std::forward<Function>(function), *future, promise);

    auto failureContinuation = [failureHandler]() mutable {
        failureHandler->run();
        delete failureHandler;
    };

    future->d.setContinuation(std::move(failureContinuation));
}

template<class Function, class ResultType>
void FailureHandler<Function, ResultType>::run()
{
    Q_ASSERT(parentFuture.isFinished());

    promise.reportStarted();

    if (parentFuture.d.exceptionStore().hasException()) {
        using ArgType = typename QtPrivate::ArgResolver<Function>::First;
        if constexpr (std::is_void_v<ArgType>) {
            handleAllExceptions();
        } else {
            handleException<ArgType>();
        }
    } else {
        QtPrivate::fulfillPromise(promise, parentFuture);
    }
    promise.reportFinished();
}

template<class Function, class ResultType>
template<class ArgType>
void FailureHandler<Function, ResultType>::handleException()
{
    try {
        parentFuture.d.exceptionStore().throwPossibleException();
    } catch (const ArgType &e) {
        try {
            // Handle exceptions matching with the handler's argument type
            if constexpr (std::is_void_v<ResultType>) {
                handler(e);
            } else {
                if constexpr (std::is_copy_constructible_v<ResultType>)
                    promise.reportResult(handler(e));
                else
                    promise.reportAndMoveResult(handler(e));
            }
        } catch (...) {
            promise.reportException(std::current_exception());
        }
    } catch (...) {
        // Exception doesn't match with handler's argument type, propagate
        // the exception to be handled later.
        promise.reportException(std::current_exception());
    }
}

template<class Function, class ResultType>
void FailureHandler<Function, ResultType>::handleAllExceptions()
{
    try {
        parentFuture.d.exceptionStore().throwPossibleException();
    } catch (...) {
        try {
            QtPrivate::fulfillPromise(promise, std::forward<Function>(handler));
        } catch (...) {
            promise.reportException(std::current_exception());
        }
    }
}

#endif // QT_NO_EXCEPTIONS

template<class Function, class ResultType>
class CanceledHandler
{
public:
    static QFuture<ResultType> create(Function &&handler, QFuture<ResultType> *future,
                                      QFutureInterface<ResultType> promise)
    {
        Q_ASSERT(future);

        auto canceledContinuation = [parentFuture = *future, promise,
                                     handler = std::move(handler)]() mutable {
            promise.reportStarted();

            if (parentFuture.isCanceled()) {
#ifndef QT_NO_EXCEPTIONS
                if (parentFuture.d.exceptionStore().hasException()) {
                    // Propagate the exception to the result future
                    promise.reportException(parentFuture.d.exceptionStore().exception());
                } else {
                    try {
#endif
                        QtPrivate::fulfillPromise(promise, std::forward<Function>(handler));
#ifndef QT_NO_EXCEPTIONS
                    } catch (...) {
                        promise.reportException(std::current_exception());
                    }
                }
#endif
            } else {
                QtPrivate::fulfillPromise(promise, parentFuture);
            }

            promise.reportFinished();
        };
        future->d.setContinuation(std::move(canceledContinuation));
        return promise.future();
    }
};

} // namespace QtPrivate

namespace QtFuture {

template<class Signal>
using ArgsType = typename QtPrivate::ArgResolver<Signal>::AllArgs;

template<class Sender, class Signal, typename = QtPrivate::EnableIfInvocable<Sender, Signal>>
static QFuture<ArgsType<Signal>> connect(Sender *sender, Signal signal)
{
    using ArgsType = ArgsType<Signal>;
    QFutureInterface<ArgsType> promise;
    promise.reportStarted();

    using Connections = std::pair<QMetaObject::Connection, QMetaObject::Connection>;
    auto connections = std::make_shared<Connections>();

    if constexpr (std::is_void_v<ArgsType>) {
        connections->first =
                QObject::connect(sender, signal, sender, [promise, connections]() mutable {
                    promise.reportFinished();
                    QObject::disconnect(connections->first);
                    QObject::disconnect(connections->second);
                });
    } else if constexpr (QtPrivate::isTupleV<ArgsType>) {
        connections->first = QObject::connect(sender, signal, sender,
                                              [promise, connections](auto... values) mutable {
                                                  promise.reportResult(std::make_tuple(values...));
                                                  promise.reportFinished();
                                                  QObject::disconnect(connections->first);
                                                  QObject::disconnect(connections->second);
                                              });
    } else {
        connections->first = QObject::connect(sender, signal, sender,
                                              [promise, connections](ArgsType value) mutable {
                                                  promise.reportResult(value);
                                                  promise.reportFinished();
                                                  QObject::disconnect(connections->first);
                                                  QObject::disconnect(connections->second);
                                              });
    }

    connections->second =
            QObject::connect(sender, &QObject::destroyed, sender, [promise, connections]() mutable {
                promise.reportCanceled();
                promise.reportFinished();
                QObject::disconnect(connections->first);
                QObject::disconnect(connections->second);
            });

    return promise.future();
}

} // namespace QtFuture

QT_END_NAMESPACE
