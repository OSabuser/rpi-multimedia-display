/**
 * @file src_media_ingest/mounter.h
 * @brief Монтирование / размонтирование USB block-устройств через mount(2).
 *
 * Требует CAP_SYS_ADMIN (задаётся через AmbientCapabilities в .service).
 * Монтирует только для чтения (MS_RDONLY): запись на носитель не нужна.
 * Пробует типы ФС в порядке: vfat → exfat → ext4.
 */

#pragma once

/**
 * mounter_mount — смонтировать устройство в точку монтирования.
 *
 * Перебирает типы ФС (vfat, exfat, ext4) до первого успеха.
 * MS_RDONLY | MS_NOEXEC | MS_NOSUID | MS_NODEV.
 *
 * @param p_dev         блочное устройство, например "/dev/sda1"
 * @param p_mountpoint  точка монтирования, например "/mnt/usb"
 * @return 0 при успехе; -1 при ошибке
 */
int mounter_mount(const char *p_dev, const char *p_mountpoint);

/**
 * mounter_umount — размонтировать точку монтирования.
 *
 * Использует MNT_DETACH (lazy umount): точка сразу недоступна для новых
 * открытий; ядро завершит размонтирование когда все fd будут закрыты.
 *
 * @param p_mountpoint  точка монтирования
 * @return 0 при успехе (включая «не смонтировано»); -1 при ошибке
 */
int mounter_umount(const char *p_mountpoint);

/**
 * mounter_is_mounted — проверить активность монтирования по /proc/mounts.
 *
 * @param p_mountpoint  точка монтирования
 * @return 1 если смонтировано; 0 если нет или при ошибке чтения
 */
int mounter_is_mounted(const char *p_mountpoint);
