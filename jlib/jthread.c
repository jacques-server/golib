/*
 * Copyright (C) 2015  Wiky L
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with main.c; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */
#include "jthread.h"
#include "jmem.h"
#include "jstrfuncs.h"
#include "jatomic.h"
#include <stdlib.h>

struct _JThread {
    pthread_t posix;
    JMutex lock;
    jboolean joined;

    JThreadFunc func;
    jpointer data;
    jboolean joinable;

    jchar *name;
    jboolean ours;
    jint ref;
    jpointer retval;
};

void j_mutex_init(JMutex * mutex)
{
    pthread_mutex_init(&mutex->posix, NULL);
}

void j_mutex_clear(JMutex * mutex)
{
    pthread_mutex_destroy(&mutex->posix);
}

void j_mutex_lock(JMutex * mutex)
{
    pthread_mutex_lock(&mutex->posix);
}

jboolean j_mutex_trylock(JMutex * mutex)
{
    return pthread_mutex_trylock(&mutex->posix) == 0;
}

void j_mutex_unlock(JMutex * mutex)
{
    pthread_mutex_unlock(&mutex->posix);
}

void j_cond_init(JCond * cond)
{
    pthread_cond_init(&cond->posix, NULL);
}

void j_cond_clear(JCond * cond)
{
    pthread_cond_destroy(&cond->posix);
}

void j_cond_wait(JCond * cond, JMutex * mutex)
{
    pthread_cond_wait(&cond->posix, &mutex->posix);
}

void j_cond_signal(JCond * cond)
{
    pthread_cond_signal(&cond->posix);
}

static inline pthread_key_t *j_private_get_key(JPrivate * priv)
{
    pthread_key_t *key =
        (pthread_key_t *) j_atomic_pointer_get(&priv->posix);
    if (J_UNLIKELY(key == NULL)) {
        key = (pthread_key_t *) j_malloc(sizeof(pthread_key_t));
        if (!j_atomic_pointer_compare_and_exchange
            (&priv->posix, NULL, key)) {
            j_free(key);
            key = priv->posix;
        } else {
            pthread_key_create(key, priv->destroy);
        }
    }
    return key;
}

jpointer j_private_get(JPrivate * priv)
{
    pthread_key_t *key = j_private_get_key(priv);
    return pthread_getspecific(*key);
}

void j_private_set(JPrivate * priv, jpointer data)
{
    pthread_key_t *key = j_private_get_key(priv);
    pthread_setspecific(*key, data);
}

static void j_thread_cleanup(jpointer data);

J_LOCK_DEFINE_STATIC(j_thread_new);
J_PRIVATE_DEFINE_STATIC(j_thread_specific_private, j_thread_cleanup);


#include <sys/prctl.h>
static inline void j_thread_set_name(const jchar * name)
{
    prctl(PR_SET_NAME, name, 0, 0, 0, 0);
}

static jpointer thread_func_proxy(jpointer data)
{
    if (data == NULL) {
        return NULL;
    }
    JThread *thread = (JThread *) data;
    j_private_set(&j_thread_specific_private, data);
    J_LOCK(j_thread_new);
    J_UNLOCK(j_thread_new);
    if (thread->name) {
        /* set thread name */
        j_thread_set_name(thread->name);
    }
    thread->retval = thread->func(thread->data);
    return NULL;
}

static inline JThread *j_thread_new_internal(JThreadFunc func,
                                             jpointer data)
{
    JThread *thread = (JThread *) j_malloc(sizeof(JThread));
    jint ret =
        pthread_create(&thread->posix, NULL, thread_func_proxy, thread);
    if (ret) {
        j_free(thread);
        return NULL;
    }
    j_mutex_init(&thread->lock);
    return thread;
}

JThread *j_thread_new(const jchar * name, JThreadFunc func, jpointer data)
{
    JThread *thread = NULL;
    J_LOCK(j_thread_new);
    thread = j_thread_new_internal(func, data);
    if (thread) {
        thread->name = j_strdup(name);
        thread->joinable = TRUE;
        thread->joined = FALSE;
        thread->func = func;
        thread->data = data;
        thread->ours = TRUE;
        thread->ref = 2;
    }
    J_UNLOCK(j_thread_new);
    return thread;
}

static inline void j_thread_join_internal(JThread * thread)
{
    j_mutex_lock(&thread->lock);
    if (!thread->joined) {
        pthread_join(thread->posix, NULL);
        thread->joined = TRUE;
    }

    j_mutex_unlock(&thread->lock);

}

jpointer j_thread_join(JThread * thread)
{
    jpointer retval;

    j_thread_join_internal(thread);
    retval = thread->retval;
    thread->joinable = FALSE;
    j_thread_unref(thread);
    return retval;
}

static void j_thread_cleanup(jpointer data)
{
    j_thread_unref(data);
}

static inline void j_thread_free(JThread * thread)
{
    if (!thread->joined) {
        pthread_detach(thread->posix);
    }
    j_free(thread->name);
    j_mutex_clear(&thread->lock);
    j_free(thread);
}

void j_thread_unref(JThread * thread)
{
    if (j_atomic_int_dec_and_test(&thread->ref)) {
        if (thread->ours) {
            j_thread_free(thread);
        } else {
            j_free(thread);
        }
    }
}

void j_thread_ref(JThread * thread)
{
    j_atomic_int_inc(&thread->ref);
}
