/**
 * @file src_media_ingest/mounter.c
 * @brief Монтирование USB через mount(2)/umount2(2) и проверка /proc/mounts.
 *
 * _GNU_SOURCE требуется для:
 *   umount2 / MNT_DETACH    <sys/mount.h>
 *   setmntent / getmntent   <mntent.h>
 */
#define _GNU_SOURCE

#include "mounter.h"

#include <errno.h>
#include <mntent.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <syslog.h>

/* ─── Константы ──────────────────────────────────────────────────────────── */

/** Типы ФС в порядке перебора; NULL — конец списка */
static const char *const FS_TYPES[] = { "vfat", "exfat", "ext4", NULL };

/** Флаги безопасного монтирования только для чтения */
static const unsigned long MOUNT_FLAGS = (unsigned long) MS_RDONLY | (unsigned long) MS_NOEXEC |
                                         (unsigned long) MS_NOSUID | (unsigned long) MS_NODEV;

/* ─── Реализация ─────────────────────────────────────────────────────────── */

int mounter_mount(const char *p_dev, const char *p_mountpoint)
{
    for (int i = 0; FS_TYPES[i] != NULL; i++)
    {
        if (mount(p_dev, p_mountpoint, FS_TYPES[i], MOUNT_FLAGS, NULL) == 0)
        {
            syslog(LOG_NOTICE, "mounter: %s → %s (fstype=%s)", p_dev, p_mountpoint, FS_TYPES[i]);
            return 0;
        }

        if (errno == ENODEV || errno == EINVAL)
        {
            /* ENODEV: модуль ФС не загружен.
             * EINVAL: суперблок не распознан для данного типа.
             * Оба — повод попробовать следующий тип. */
            continue;
        }

        /* Любая другая ошибка (ENOENT, EACCES, …) — устройство недоступно */
        syslog(LOG_ERR, "mounter: mount %s %s fstype=%s: %s", p_dev, p_mountpoint, FS_TYPES[i],
               strerror(errno));
        return -1;
    }

    syslog(LOG_ERR, "mounter: no supported filesystem on %s (tried vfat/exfat/ext4)", p_dev);
    return -1;
}

int mounter_umount(const char *p_mountpoint)
{
    if (umount2(p_mountpoint, MNT_DETACH) < 0)
    {
        if (errno == EINVAL)
        {
            /* Точка монтирования уже не активна — норма при double-umount */
            syslog(LOG_WARNING, "mounter: umount '%s': already unmounted", p_mountpoint);
            return 0;
        }
        syslog(LOG_ERR, "mounter: umount2 '%s': %s", p_mountpoint, strerror(errno));
        return -1;
    }

    syslog(LOG_NOTICE, "mounter: unmounted '%s'", p_mountpoint);
    return 0;
}

int mounter_is_mounted(const char *p_mountpoint)
{
    FILE *p_fp = setmntent("/proc/mounts", "r");
    if (p_fp == NULL)
    {
        syslog(LOG_ERR, "mounter: setmntent: %s", strerror(errno));
        return 0;
    }

    int found = 0;
    const struct mntent *p_ent;

    while ((p_ent = getmntent(p_fp)) != NULL)
    {
        if (strcmp(p_ent->mnt_dir, p_mountpoint) == 0)
        {
            found = 1;
            break;
        }
    }

    (void) endmntent(p_fp);
    return found;
}
