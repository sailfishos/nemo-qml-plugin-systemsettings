/*
 * Copyright (C) 2018 Jolla Ltd. <raine.makelainen@jolla.com>
 *
 * You may use this file under the terms of the BSD license as follows:
 *
 * "Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Nemo Mobile nor the names of its contributors
 *     may be used to endorse or promote products derived from this
 *     software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
 */

#ifndef UDISKS2_DEFINES
#define UDISKS2_DEFINES

#define DBUS_OBJECT_MANAGER_INTERFACE    QLatin1String("org.freedesktop.DBus.ObjectManager")
#define DBUS_OBJECT_PROPERTIES_INTERFACE QLatin1String("org.freedesktop.DBus.Properties")
#define DBUS_GET_ALL                     QLatin1String("GetAll")

#define UDISKS2_SERVICE QLatin1String("org.freedesktop.UDisks2")
#define UDISKS2_PATH    QLatin1String("/org/freedesktop/UDisks2")

// Interfaces
#define UDISKS2_ENCRYPTED_INTERFACE  QLatin1String("org.freedesktop.UDisks2.Encrypted")
#define UDISKS2_BLOCK_INTERFACE      QLatin1String("org.freedesktop.UDisks2.Block")
#define UDISKS2_FILESYSTEM_INTERFACE QLatin1String("org.freedesktop.UDisks2.Filesystem")
#define UDISKS2_PARTITION_INTERFACE  QLatin1String("org.freedesktop.UDisks2.Partition")
#define UDISKS2_JOB_INTERFACE        QLatin1String("org.freedesktop.UDisks2.Job")

// Jobs
#define UDISKS2_JOB_OP_ENC_LOCK   QLatin1String("encrypted-lock")
#define UDISKS2_JOB_OP_ENC_UNLOCK QLatin1String("encrypted-unlock")
#define UDISKS2_JOB_OP_FS_UNMOUNT QLatin1String("filesystem-unmount")
#define UDISKS2_JOB_OP_FS_MOUNT   QLatin1String("filesystem-mount")
#define UDISKS2_JOB_OP_CLEANUP    QLatin1String("cleanup")
#define UDISKS2_JOB_OF_FS_FORMAT  QLatin1String("format-mkfs")

// Job keys
#define UDISKS2_JOB_KEY_OPERATION QLatin1String("Operation")
#define UDISKS2_JOB_KEY_OBJECTS   QLatin1String("Objects")

// Lock, Unlock, Mount, Unmount, Format
#define UDISKS2_BLOCK_DEVICE_PATH  QString("/org/freedesktop/UDisks2/block_devices/%1")
#define UDISKS2_BLOCK_FORMAT       QLatin1String("Format")
#define UDISKS2_ENCRYPTED_LOCK     QLatin1String("Lock")
#define UDISKS2_ENCRYPTED_UNLOCK   QLatin1String("Unlock")
#define UDISKS2_FILESYSTEM_MOUNT   QLatin1String("Mount")
#define UDISKS2_FILESYSTEM_UNMOUNT QLatin1String("Unmount")

// Errors
#define UDISKS2_ERROR_DEVICE_BUSY        QLatin1String("org.freedesktop.UDisks2.Error.DeviceBusy")
#define UDISKS2_ERROR_TARGET_BUSY        QLatin1String("target is busy")
#define UDISKS2_ERROR_ALREADY_MOUNTED    "org.freedesktop.UDisks2.Error.AlreadyMounted"
#define UDISKS2_ERROR_ALREADY_UNMOUNTING "org.freedesktop.UDisks2.Error.AlreadyUnmounting"

#endif
