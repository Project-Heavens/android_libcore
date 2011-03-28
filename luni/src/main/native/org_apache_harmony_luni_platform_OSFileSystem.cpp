/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#define LOG_TAG "OSFileSystem"

#include "JNIHelp.h"
#include "JniConstants.h"
#include "JniException.h"
#include "ScopedPrimitiveArray.h"
#include "ScopedUtfChars.h"
#include "UniquePtr.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#if HAVE_SYS_SENDFILE_H
#include <sys/sendfile.h>
#else
/*
 * Define a small adapter function: sendfile() isn't part of a standard,
 * and its definition differs between Linux, BSD, and OS X. This version
 * works for OS X but will probably not work on other BSDish systems.
 * Note: We rely on function overloading here to define a same-named
 * function with different arguments.
 */
#include <sys/socket.h>
#include <sys/types.h>
static inline ssize_t sendfile(int out_fd, int in_fd, off_t* offset, size_t count) {
    off_t len = count;
    int result = sendfile(in_fd, out_fd, *offset, &len, NULL, 0);
    if (result < 0) {
        return -1;
    }
    return len;
}
#endif

// Translate three Java int[]s to a native iovec[] for readv and writev.
static iovec* initIoVec(JNIEnv* env,
        jintArray jBuffers, jintArray jOffsets, jintArray jLengths, jint size) {
    UniquePtr<iovec[]> vectors(new iovec[size]);
    if (vectors.get() == NULL) {
        jniThrowOutOfMemoryError(env, NULL);
        return NULL;
    }
    ScopedIntArrayRO buffers(env, jBuffers);
    if (buffers.get() == NULL) {
        return NULL;
    }
    ScopedIntArrayRO offsets(env, jOffsets);
    if (offsets.get() == NULL) {
        return NULL;
    }
    ScopedIntArrayRO lengths(env, jLengths);
    if (lengths.get() == NULL) {
        return NULL;
    }
    for (int i = 0; i < size; ++i) {
        vectors[i].iov_base = reinterpret_cast<void*>(buffers[i] + offsets[i]);
        vectors[i].iov_len = lengths[i];
    }
    return vectors.release();
}

static jlong OSFileSystem_readv(JNIEnv* env, jobject, jint fd,
        jintArray jBuffers, jintArray jOffsets, jintArray jLengths, jint size) {
    UniquePtr<iovec[]> vectors(initIoVec(env, jBuffers, jOffsets, jLengths, size));
    if (vectors.get() == NULL) {
        return -1;
    }
    long result = readv(fd, vectors.get(), size);
    if (result == 0) {
        return -1;
    }
    if (result == -1) {
        jniThrowIOException(env, errno);
    }
    return result;
}

static jlong OSFileSystem_writev(JNIEnv* env, jobject, jint fd,
        jintArray jBuffers, jintArray jOffsets, jintArray jLengths, jint size) {
    UniquePtr<iovec[]> vectors(initIoVec(env, jBuffers, jOffsets, jLengths, size));
    if (vectors.get() == NULL) {
        return -1;
    }
    long result = writev(fd, vectors.get(), size);
    if (result == -1) {
        jniThrowIOException(env, errno);
    }
    return result;
}

static jlong OSFileSystem_transfer(JNIEnv* env, jobject, jint fd, jobject sd,
        jlong offset, jlong count) {

    int socket = jniGetFDFromFileDescriptor(env, sd);
    if (socket == -1) {
        return -1;
    }

    /* Value of offset is checked in jint scope (checked in java layer)
       The conversion here is to guarantee no value lost when converting offset to off_t
     */
    off_t off = offset;

    ssize_t rc = sendfile(socket, fd, &off, count);
    if (rc == -1) {
        jniThrowIOException(env, errno);
    }
    return rc;
}

static JNINativeMethod gMethods[] = {
    NATIVE_METHOD(OSFileSystem, readv, "(I[I[I[II)J"),
    NATIVE_METHOD(OSFileSystem, transfer, "(ILjava/io/FileDescriptor;JJ)J"),
    NATIVE_METHOD(OSFileSystem, writev, "(I[I[I[II)J"),
};
int register_org_apache_harmony_luni_platform_OSFileSystem(JNIEnv* env) {
    return jniRegisterNativeMethods(env, "org/apache/harmony/luni/platform/OSFileSystem", gMethods,
            NELEM(gMethods));
}
