<!-- Source: _sources/api-reference/system/pthread.rst.txt (ESP-IDF v6.0 documentation) -->

# POSIX Support (Including POSIX Threads Support)

## Overview

ESP-IDF is based on FreeRTOS but offers a range of POSIX-compatible APIs that allow easy porting of third-party code. This includes support for common parts of the POSIX Threads `pthread` API.

POSIX Threads are implemented in ESP-IDF as wrappers around equivalent FreeRTOS features. The runtime memory or performance overhead of using the pthreads API is quite low, but not every feature available in either pthreads or FreeRTOS is available via the ESP-IDF pthreads support.

Pthreads can be used in ESP-IDF by including standard `pthread.h` header, which is included in the toolchain libc. An additional ESP-IDF specific header, `esp_pthread.h`, provides additional non-POSIX APIs for using some ESP-IDF features with pthreads.

Besides POSIX Threads, ESP-IDF also supports `POSIX message queues <posix_message_queues>`.

C++ Standard Library implementations for `std::thread`, `std::mutex`, `std::condition_variable`, etc., are realized using pthreads and other POSIX APIs (via GCC libstdc++). Therefore, restrictions mentioned here also apply to the equivalent C++ standard library functionality.

If you identify a useful API that you would like to see implemented in ESP-IDF, please open a [feature request on GitHub](https://github.com/espressif/esp-idf/issues) with the details.

## RTOS Integration

Unlike many operating systems using POSIX Threads, ESP-IDF is a real-time operating system with a real-time scheduler. This means that a thread will only stop running if a higher priority task is ready to run, the thread blocks on an OS synchronization structure like a mutex, or the thread calls any of the functions `sleep`, `vTaskDelay`, or `usleep`.

> **Note**
>
> >
> Note

When calling a standard libc or C++ sleep function, such as `usleep` defined in `unistd.h`, the task will only block and yield the core if the sleep time is longer than `one FreeRTOS tick period <CONFIG_FREERTOS_HZ>`. If the time is shorter, the thread will busy-wait instead of yielding to another RTOS task.

> **Note**
>
> By default, all POSIX Threads have the same RTOS priority, but it is possible to change this by calling a `custom API <esp-pthread>`.

## Standard Features

The following standard APIs are implemented in ESP-IDF.

Refer to [standard POSIX Threads documentation](https://man7.org/linux/man-pages/man7/pthreads.7.html), or `pthread.h`, for details about the standard arguments and behaviour of each function. Differences or limitations compared to the standard APIs are noted below.

### Thread APIs

- `pthread_create()`  
  - The `attr` argument is supported for setting stack size and detach state only. Other attribute fields are ignored.
  - Unlike FreeRTOS task functions, the `start_routine` function is allowed to return. A detached type thread is automatically deleted if the function returns. The default joinable type thread will be suspended until `pthread_join()` is called on it.

- `pthread_join()`

- `pthread_detach()`

- `pthread_exit()`

- `sched_yield()`

- `pthread_self()`  
  - An assert will fail if this function is called from a FreeRTOS task which is not a pthread.

- `pthread_equal()`

### Thread Attributes

- `pthread_attr_init()`

- `pthread_attr_destroy()`  
  - This function does not need to free any resources and instead resets the `attr` structure to defaults. The implementation is the same as `pthread_attr_init()`.

- `pthread_attr_getstacksize()` / `pthread_attr_setstacksize()`

- `pthread_attr_getdetachstate()` / `pthread_attr_setdetachstate()`

### Once

- `pthread_once()`

Static initializer constant `PTHREAD_ONCE_INIT` is supported.

> **Note**
>
> ### Mutexes

POSIX Mutexes are implemented as FreeRTOS Mutex Semaphores (normal type for "fast" or "error check" mutexes, and Recursive type for "recursive" mutexes). This means that they have the same priority inheritance behavior as mutexes created with `xSemaphoreCreateMutex`.

- `pthread_mutex_init()`
- `pthread_mutex_destroy()`
- `pthread_mutex_lock()`
- `pthread_mutex_timedlock()`
- `pthread_mutex_trylock()`
- `pthread_mutex_unlock()`
- `pthread_mutexattr_init()`
- `pthread_mutexattr_destroy()`
- `pthread_mutexattr_gettype()` / `pthread_mutexattr_settype()`

Static initializer constant `PTHREAD_MUTEX_INITIALIZER` is supported, but the non-standard static initializer constants for other mutex types are not supported.

> **Note**
>
> ### Condition Variables

- `pthread_cond_init()`  
  - The `attr` argument is not implemented and is ignored.

- `pthread_cond_destroy()`

- `pthread_cond_signal()`

- `pthread_cond_broadcast()`

- `pthread_cond_wait()`

- `pthread_cond_timedwait()`

Static initializer constant `PTHREAD_COND_INITIALIZER` is supported.

- The resolution of `pthread_cond_timedwait()` timeouts is the RTOS tick period (see `CONFIG_FREERTOS_HZ`). Timeouts may be delayed up to one tick period after the requested timeout.

> **Note**
>
> ### Semaphores

In ESP-IDF, POSIX **unnamed** semaphores are implemented. The accessible API is described below. It implements [semaphores as specified in the POSIX standard](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/semaphore.h.html), unless specified otherwise.

- [sem_init()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_init.html)

- [sem_destroy()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_destroy.html)

  > - `pshared` is ignored. Semaphores can always be shared between FreeRTOS tasks.

- [sem_post()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_post.html)

  > - If the semaphore has a value of `SEM_VALUE_MAX` already, `-1` is returned and `errno` is set to `EAGAIN`.

- [sem_wait()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_wait.html)

- [sem_trywait()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_trywait.html)

- [sem_timedwait()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_timedwait.html)

  > - The time value passed by abstime will be rounded up to the next FreeRTOS tick.
  > - The actual timeout happens after the tick that the time was rounded to and before the following tick.
  > - It is possible, though unlikely, that the task is preempted directly after the timeout calculation, delaying the timeout of the following blocking operating system call by the duration of the preemption.

- [sem_getvalue()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_getvalue.html)

### Read/Write Locks

The following API functions of the POSIX reader-writer locks specification are implemented:

- [pthread_rwlock_init()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_init.html)

  > - The `attr` argument is not implemented and is ignored.

- [pthread_rwlock_destroy()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_destroy.html)

- [pthread_rwlock_rdlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_rdlock.html)

- [pthread_rwlock_tryrdlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_tryrdlock.html)

- [pthread_rwlock_wrlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_wrlock.html)

- [pthread_rwlock_trywrlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_trywrlock.html)

- [pthread_rwlock_unlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_unlock.html)

- [pthread_rwlock_timedwrlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_timedwrlock.html)

- [pthread_rwlock_timedrdlock()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_timedrdlock.html)

The static initializer constant `PTHREAD_RWLOCK_INITIALIZER` is supported.

> **Note**
>
> ### Thread-Specific Data

- `pthread_key_create()`  
  - The `destr_function` argument is supported and will be called if a thread function exits normally, calls `pthread_exit()`, or if the underlying task is deleted directly using the FreeRTOS function `vTaskDelete`.

- `pthread_key_delete()`

- `pthread_setspecific()` / `pthread_getspecific()`

> **Note**
>
> > **Note**
>
> ### Message Queues

The message queue implementation is based on the [FreeRTOS-Plus-POSIX](https://www.freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_POSIX/index.html) project. Message queues are not made available in any filesystem on ESP-IDF. Message priorities are not supported.

The following API functions of the POSIX message queue specification are implemented:

- [mq_open()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_open.html)

  > - The `name` argument has, besides the POSIX specification, the following additional restrictions:  
  >   - It has to begin with a leading slash.
  >   - It has to be no more than 255 + 2 characters long (including the leading slash, excluding the terminating null byte). However, memory for `name` is dynamically allocated internally, so the shorter it is, the fewer memory it will consume.
  >
  > - The `mode` argument is not implemented and is ignored.
  >
  > - Supported `oflags`: `O_RDWR`, `O_CREAT`, `O_EXCL`, and `O_NONBLOCK`.

- [mq_close()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_close.html)

- [mq_unlink()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_unlink.html)

- [mq_receive()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_receive.html)

  > - Since message priorities are not supported, `msg_prio` is unused.

- [mq_timedreceive()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_receive.html)

  > - Since message priorities are not supported, `msg_prio` is unused.

- [mq_send()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_send.html)

  > - Since message priorities are not supported, `msg_prio` has no effect.

- [mq_timedsend()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_send.html)

  > - Since message priorities are not supported, `msg_prio` has no effect.

- [mq_getattr()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_getattr.html)

[mq_notify()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_notify.html) and [mq_setattr()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_setattr.html) are not implemented.

#### Building

To use the POSIX message queue API, please add `rt` as a requirement in your component's `CMakeLists.txt`.

> **Note**
>
> ## Not Implemented

The `pthread.h` header is a standard header and includes additional APIs and features which are not implemented in ESP-IDF. These include:

- `pthread_cancel()` returns `ENOSYS` if called.
- `pthread_condattr_init()` returns `ENOSYS` if called.
- [mq_notify()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_notify.html) returns `ENOSYS` if called.
- [mq_setattr()](https://pubs.opengroup.org/onlinepubs/9699919799/functions/mq_setattr.html) returns `ENOSYS` if called.

Other POSIX Threads functions (not listed here) are not implemented and will produce either a compiler or a linker error if referenced from an ESP-IDF application.

## ESP-IDF Extensions

The API `esp_pthread_set_cfg` defined in the `esp_pthreads.h` header offers custom extensions to control how subsequent calls to `pthread_create()` behaves. Currently, the following configuration can be set:

- Default stack size of new threads, if not specified when calling `pthread_create()` (overrides `CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT`).
- Stack memory capabilities determine which kind of memory is used for allocating pthread stacks. The field takes ESP-IDF heap capability flags, as defined in `heap/include/esp_heap_caps.h`. The memory must be 8-bit accessible (MALLOC_CAP_8BIT), besides other custom flags the user can choose from. The user is responsible for ensuring the correctness of the stack memory capabilities. For more information about memory locations, refer to the documentation of `memory_capabilities`.

\- RTOS priority of new threads (overrides `CONFIG_PTHREAD_TASK_PRIO_DEFAULT`). :SOC_HP_CPU_HAS_MULTIPLE_CORES: - Core affinity / core pinning of new threads (overrides `CONFIG_PTHREAD_TASK_CORE_DEFAULT`). - FreeRTOS task name for new threads (overrides `CONFIG_PTHREAD_TASK_NAME_DEFAULT`)

This configuration is scoped to the calling thread (or FreeRTOS task), meaning that `esp_pthread_set_cfg` can be called independently in different threads or tasks. If the `inherit_cfg` flag is set in the current configuration then any new thread created will inherit the creator's configuration (if that thread calls `pthread_create()` recursively), otherwise the new thread will have the default configuration.

## Application Examples

- `system/pthread` demonstrates using the pthreads API to create threads.
- `cxx/pthread` demonstrates using C++ Standard Library functions with threads.

## API Reference

inc/esp_pthread.inc

