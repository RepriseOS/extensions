/* SPDX-License-Identifier: GPL-3.0-only */

/* What File Info offers other extensions through api->extension_service. */

#ifndef FILE_INFO_SERVICE_H
#define FILE_INFO_SERVICE_H

#define FILE_INFO_ID "file-info"

struct file_info_service {
    /* Opens File Info above the top screen for `record` (a media record of
     * the symbols header); RV_OK or the ui_push error. UI task. */
    int (*open)(void *record);
};

#endif
