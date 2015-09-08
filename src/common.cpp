/*
 * emulator-daemon
 *
 * Copyright (c) 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Jinhyung Choi <jinhyung2.choi@samsnung.com>
 * SooYoung Ha <yoosah.ha@samsnung.com>
 * Sungmin Ha <sungmin82.ha@samsung.com>
 * Daiyoung Kim <daiyoung777.kim@samsung.com>
 * YeongKyoon Lee <yeongkyoon.lee@samsung.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <linux/limits.h>
#include <sys/time.h>
#include <sys/reboot.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <E_DBus.h>
#include <Ecore.h>

#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <mntent.h>

#include <vconf.h>
#include <vconf-keys.h>

#include "emuld.h"
#include "dd-display.h"

#define PMAPI_RETRY_COUNT   3
#define POWEROFF_DURATION   2

#define SUSPEND_UNLOCK      0
#define SUSPEND_LOCK        1

#define MAX_PKGS_BUF        1024

#define PATH_PACKAGE_INSTALL    "/opt/usr/apps/tmp/sdk_tools/"
#define SCRIPT_INSTALL          "post-install.sh"
#define RPM_CMD_QUERY           "-q"
#define RPM_CMD_INSTALL         "-U"
static pthread_mutex_t mutex_pkg = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_cmd = PTHREAD_MUTEX_INITIALIZER;

static struct timeval tv_start_poweroff;

void systemcall(const char* param)
{
    if (!param)
        return;

    if (system(param) == -1)
        LOGERR("system call failure(command = %s)", param);
}

int parse_val(char *buff, unsigned char data, char *parsbuf)
{
    int count=0;
    while(1)
    {
        if(count > 40)
            return -1;
        if(buff[count] == data)
        {
            count++;
            strncpy(parsbuf, buff, count);
            return count;
        }
        count++;
    }

    return 0;
}

void powerdown_by_force(void)
{
    struct timeval now;
    int poweroff_duration = POWEROFF_DURATION;

    gettimeofday(&now, NULL);
    /* Waiting until power off duration and displaying animation */
    while (now.tv_sec - tv_start_poweroff.tv_sec < poweroff_duration) {
        LOGINFO("power down wait");
        usleep(100000);
        gettimeofday(&now, NULL);
    }

    LOGINFO("Power off by force");
    LOGINFO("sync");

    sync();

    LOGINFO("poweroff");

    reboot(RB_POWER_OFF);
}

void msgproc_system(ijcommand* ijcmd)
{
    LOGDEBUG("msgproc_system");

    LOGINFO("/etc/rc.d/rc.shutdown, sync, reboot(RB_POWER_OFF)");

    sync();

    systemcall("/etc/rc.d/rc.shutdown &");

    gettimeofday(&tv_start_poweroff, NULL);

    powerdown_by_force();
}

static void set_lock_state(int state) {
    int i = 0;
    int ret = 0;
    // Now we blocking to enter "SLEEP".
    while(i < PMAPI_RETRY_COUNT ) {
        ret = display_lock_state(LCD_OFF, STAY_CUR_STATE, 0);
        LOGINFO("display_lock_state() return: %d", ret);
        if(ret == 0)
        {
            break;
        }
        ++i;
        sleep(10);
    }
    if (i == PMAPI_RETRY_COUNT) {
        LOGERR("Emulator Daemon: Failed to call display_lock_state().\n");
    }
}

void msgproc_suspend(ijcommand* ijcmd)
{
    if (ijcmd->msg.action == SUSPEND_LOCK) {
        set_lock_state(SUSPEND_LOCK);
    } else {
        set_lock_state(SUSPEND_UNLOCK);
    }

    LOGINFO("[Suspend] Set lock state as %d (1: lock, other: unlock)", ijcmd->msg.action);
}

void send_to_ecs(const char* cat, int group, int action, char* data)
{
    int datalen = 0;
    int tmplen = HEADER_SIZE;
    if (data != NULL) {
        datalen = strlen(data);
        tmplen += datalen;
    }

    char* tmp = (char*) malloc(tmplen);
    if (!tmp)
        return;

    memcpy(tmp, &datalen, 2);
    memcpy(tmp + 2, &group, 1);
    memcpy(tmp + 3, &action, 1);
    if (data != NULL) {
        memcpy(tmp + 4, data, datalen);
    }

    ijmsg_send_to_evdi(g_fd[fdtype_device], cat, (const char*) tmp, tmplen);

    if (tmp)
        free(tmp);
}

void send_emuld_connection(void)
{
    send_to_ecs(IJTYPE_GUEST, 0, 1, NULL);
}

void send_default_suspend_req(void)
{
    send_to_ecs(IJTYPE_SUSPEND, 5, 15, NULL);
}

static bool do_rpm_execute(char* pkgs)
{
    char buf[MAX_PKGS_BUF];
    int ret = 0;

    FILE* fp = popen(pkgs, "r");
    if (fp == NULL) {
        LOGERR("[rpm] Failed to popen %s", pkgs);
        return false;
    }

    memset(buf, 0, sizeof(buf));
    while(fgets(buf, sizeof(buf), fp)) {
        LOGINFO("[rpm]%s", buf);
        memset(buf, 0, sizeof(buf));
    }

    ret = pclose(fp);
    if (ret == -1) {
        LOGINFO("[rpm] pclose error: %d", errno);
        return false;
    }

    if (ret >= 0 && WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
        LOGINFO("[rpm] RPM execution success: %s", pkgs);
        return true;
    }

    LOGINFO("[rpm] RPM execution fail: [%x,%x,%x] %s", ret, WIFEXITED(ret), WEXITSTATUS(ret), pkgs);

    return false;
}

static void remove_package(char* data)
{
    char token[] = ", ";
    char pkg_list[MAX_PKGS_BUF];
    char *addon = NULL;
    char *copy = strdup(data);
    if (copy == NULL) {
        LOGERR("Failed to copy data.");
        return;
    }

    memset(pkg_list, 0, sizeof(pkg_list));

    strcpy(pkg_list, "rm -rf ");

    strcat(pkg_list, PATH_PACKAGE_INSTALL);
    addon = strtok(copy, token);
    strcat(pkg_list, addon);

    LOGINFO("remove packages: %s", pkg_list);

    systemcall(pkg_list);

    free(copy);
}

static void run_script(char* addon)
{
    int ret = -1;
    char file_name[MAX_PKGS_BUF];

    snprintf(file_name, sizeof(file_name), "%s%s/%s", PATH_PACKAGE_INSTALL, addon, SCRIPT_INSTALL);
    ret = access(file_name, F_OK);
    if( ret == 0 ) {
        ret = chmod(file_name, 0755);
        if (ret != 0) {
            LOGERR("[cmd] file permission setting is failed : [%d] %s", ret, file_name);
        }
        systemcall(file_name);
    } else {
        LOGERR("[cmd] file does not exist: %s", file_name);
    }
}

static bool do_package(int action, char* data)
{
    char token[] = ", ";
    char *pkg = NULL;
    char *addon = NULL;
    char pkg_list[MAX_PKGS_BUF];

    memset(pkg_list, 0, sizeof(pkg_list));

    strcpy(pkg_list, "rpm");

    if (action == 1) {
        strcat(pkg_list, " ");
        strcat(pkg_list, RPM_CMD_QUERY);
    } else if (action == 2) {
        strcat(pkg_list, " ");
        strcat(pkg_list, RPM_CMD_INSTALL);
    } else {
        LOGERR("Unknown action.");
        return false;
    }
    addon = strtok(data, token); // for addon path

    pkg = strtok(NULL, token);
    while (pkg != NULL) {
        if (action == 1) {
            pkg[strlen(pkg) - 4] = 0; //remove .rpm
        }
        strcat(pkg_list, " ");
        if (action == 2) {
            strcat(pkg_list, PATH_PACKAGE_INSTALL);
            strcat(pkg_list, addon);
            strcat(pkg_list, "/");
        }
        strcat(pkg_list, pkg);

        pkg = strtok(NULL, token);
    }

    strcat(pkg_list, " ");
    strcat(pkg_list, "2>&1");

    LOGINFO("[cmd] %s", pkg_list);
    if (action == 1 && do_rpm_execute(pkg_list)) {
        return true;
    } else if (action == 2 && do_rpm_execute(pkg_list)) {
        run_script(addon);
        return true;
    }

    return false;
}

static void* package_thread(void* args)
{
    LOGINFO("install package_thread starts.");
    int action = 0;
    ijcommand* ijcmd = (ijcommand*)args;
    char* data = strdup(ijcmd->data);
    if (data == NULL) {
        LOGERR("install data is failed to copied.");
        return NULL;
    }

    if (ijcmd->msg.action == 1) { // validate packages
        if (do_package(1, data)) {
            action = 1; // already installed
        } else {
            action = 2; // need to install
        }
    } else if (ijcmd->msg.action == 2) { // install packages
        if (do_package(2, data)) {
            action = 3; // install success
        } else {
            action = 4; // failed to install
        }
        remove_package(ijcmd->data);
    } else {
        LOGERR("invalid command (action:%d)", ijcmd->msg.action);
    }

    LOGINFO("send %d, with %s", action, ijcmd->data);
    send_to_ecs(IJTYPE_PACKAGE, 0, action, ijcmd->data);

    free(data);

    return NULL;
}

void msgproc_package(ijcommand* ijcmd)
{
    _auto_mutex _(&mutex_pkg);
    int ret = 0;
    void* retval = NULL;
    pthread_t pkg_thread_id;

    if (!ijcmd->data) {
        LOGERR("package data is empty.");
        return;
    }

    LOGINFO("received %d, with %s", ijcmd->msg.action, ijcmd->data);

    if (pthread_create(&pkg_thread_id, NULL, package_thread, (void*)ijcmd) != 0)
    {
        LOGERR("validate package pthread creation is failed!");
    }
    ret = pthread_join(pkg_thread_id, &retval);
    if (ret < 0) {
        LOGERR("validate package pthread join is failed.");
    }
}

enum ioctl_cmd {
    IOCTL_CMD_BOOT_DONE,
};

void send_to_kernel(void)
{
    if(ioctl(g_fd[fdtype_device], IOCTL_CMD_BOOT_DONE, NULL) == -1) {
        LOGERR("Failed to send ioctl to kernel");
        return;
    }
    LOGINFO("[DBUS] sent booting done to kernel");
}

#define DBUS_PATH_BOOT_DONE  "/Org/Tizen/System/DeviceD/Core"
#define DBUS_IFACE_BOOT_DONE "org.tizen.system.deviced.core"
#define BOOT_DONE_SIGNAL     "BootingDone"

static void boot_done(void *data, DBusMessage *msg)
{
    if (dbus_message_is_signal(msg,
                DBUS_IFACE_BOOT_DONE,
                BOOT_DONE_SIGNAL) != 0) {
        LOGINFO("[DBUS] sending booting done to ecs.");
        send_to_ecs(IJTYPE_BOOT, 0, 0, NULL);
        LOGINFO("[DBUS] sending booting done to kernel for log.");
        send_to_kernel();
    }
}

static void sig_handler(int signo)
{
    LOGINFO("received signal: %d. EXIT!", signo);
    _exit(0);
}

static void add_sig_handler(int signo)
{
    sighandler_t sig;

    sig = signal(signo, sig_handler);
    if (sig == SIG_ERR) {
        LOGERR("adding %d signal failed : %d", signo, errno);
    }
}

void* dbus_booting_done_check(void* data)
{
    E_DBus_Connection *connection;
    E_DBus_Signal_Handler *boot_handler = NULL;

    ecore_init();
    e_dbus_init();

    connection = e_dbus_bus_get(DBUS_BUS_SYSTEM);
    if (!connection) {
        LOGERR("[DBUS] Failed to get dbus bus.");
        e_dbus_shutdown();
        ecore_shutdown();
        return NULL;
    }

    boot_handler = e_dbus_signal_handler_add(
            connection,
            NULL,
            DBUS_PATH_BOOT_DONE,
            DBUS_IFACE_BOOT_DONE,
            BOOT_DONE_SIGNAL,
            boot_done,
            NULL);
    if (!boot_handler) {
        LOGERR("[DBUS] Failed to register handler");
        e_dbus_signal_handler_del(connection, boot_handler);
        e_dbus_shutdown();
        ecore_shutdown();
        return NULL;
    }
    LOGINFO("[DBUS] signal handler is added.");

    add_sig_handler(SIGINT);
    add_sig_handler(SIGTERM);
    add_sig_handler(SIGUSR1);

    ecore_main_loop_begin();

    e_dbus_signal_handler_del(connection, boot_handler);
    e_dbus_shutdown();
    ecore_shutdown();

    return NULL;
}

static char SDpath[PATH_MAX];

/*
 * SD Card functions
 */

static char* get_mount_info() {
    struct mntent *ent;
    FILE *aFile;

    aFile = setmntent("/proc/mounts", "r");
    if (aFile == NULL) {
        LOGERR("/proc/mounts is not exist");
        return NULL;
    }
    char* mountinfo = new char[MAX_CMD_LEN];
    memset(mountinfo, 0, MAX_CMD_LEN);

    while (NULL != (ent = getmntent(aFile))) {

        if (strcmp(ent->mnt_dir, "/opt/storage/sdcard") == 0)
        {
            LOGDEBUG(",%s,%s,%d,%s,%d,%s",
                            ent->mnt_fsname, ent->mnt_dir, ent->mnt_freq, ent->mnt_opts, ent->mnt_passno, ent->mnt_type);
            sprintf(mountinfo,",%s,%s,%d,%s,%d,%s\n",
                            ent->mnt_fsname, ent->mnt_dir, ent->mnt_freq, ent->mnt_opts, ent->mnt_passno, ent->mnt_type);
            break;
        }
    }
    endmntent(aFile);

    return mountinfo;
}

int is_mounted()
{
    int ret = -1, i = 0;
    struct stat buf;
    char file_name[PATH_MAX];

    for(i = 0; i < 10; i++)
    {
        sprintf(file_name, "/dev/mmcblk%d", i);
        ret = access(file_name, F_OK);
        if( ret == 0 )
        {
            lstat(file_name, &buf);
            if(S_ISBLK(buf.st_mode))
                return 1;
            else
                return 0;
        }
    }

    return 0;
}

void* mount_sdcard(void* data)
{
    int ret = -1, i = 0;
    struct stat buf;
    char file_name[PATH_MAX], command[MAX_CMD_LEN];
    memset(file_name, '\0', sizeof(file_name));

    LXT_MESSAGE* packet = (LXT_MESSAGE*)malloc(sizeof(LXT_MESSAGE));
    memset(packet, 0, sizeof(LXT_MESSAGE));

    LOGINFO("start sdcard mount thread");

    pthread_detach(pthread_self());

    while (ret < 0)
    {
        for (i = 0; i < 10; i++)
        {
            sprintf(file_name, "/dev/mmcblk%d", i);
            ret = access( file_name, F_OK );
            if( ret == 0 )
            {
                lstat(file_name, &buf);
                if(!S_ISBLK(buf.st_mode))
                {
                    sprintf(command, "rm -rf %s", file_name);
                    systemcall(command);
                }
                else
                    break;
            }
        }

        if (i != 10)
        {
            LOGDEBUG( "%s is exist", file_name);
            packet->length = strlen(SDpath);        // length
            packet->group = 11;             // sdcard
            if (ret == 0)
                packet->action = 1; // mounted
            else
                packet->action = 5; // failed

            //
            LOGDEBUG("SDpath is %s", SDpath);

            const int tmplen = HEADER_SIZE + packet->length;
            char* tmp = (char*) malloc(tmplen);

            if (tmp)
            {
                memcpy(tmp, packet, HEADER_SIZE);
                if (packet->length > 0)
                {
                    memcpy(tmp + HEADER_SIZE, SDpath, packet->length);
                }

                ijmsg_send_to_evdi(g_fd[fdtype_device], IJTYPE_SDCARD, (const char*) tmp, tmplen);

                free(tmp);
            }

            break;
        }
        else
        {
            LOGERR( "%s is not exist", file_name);
        }
    }

    if(packet)
    {
        free(packet);
        packet = NULL;
    }

    pthread_exit((void *) 0);
}

static int umount_sdcard(void)
{
    int ret = -1, i = 0;
    char file_name[PATH_MAX];

    LXT_MESSAGE* packet = (LXT_MESSAGE*)malloc(sizeof(LXT_MESSAGE));
    if(packet == NULL){
        return ret;
    }
    memset(packet, 0, sizeof(LXT_MESSAGE));

    LOGINFO("start sdcard umount");

    pthread_cancel(tid[TID_SDCARD]);

    for (i = 0; i < 10; i++)
    {
        sprintf(file_name, "/dev/mmcblk%d", i);
        ret = access(file_name, F_OK);
        if (ret == 0)
        {
            LOGDEBUG("SDpath is %s", SDpath);

            packet->length = strlen(SDpath);        // length
            packet->group = 11;                     // sdcard
            packet->action = 0;                     // unmounted

            const int tmplen = HEADER_SIZE + packet->length;
            char* tmp = (char*) malloc(tmplen);
            if (!tmp)
                break;

            memcpy(tmp, packet, HEADER_SIZE);
            memcpy(tmp + HEADER_SIZE, SDpath, packet->length);

            ijmsg_send_to_evdi(g_fd[fdtype_device], IJTYPE_SDCARD, (const char*) tmp, tmplen);

            free(tmp);

            memset(SDpath, '\0', sizeof(SDpath));
            sprintf(SDpath, "umounted");

            break;
        }
        else
        {
            LOGERR( "%s is not exist", file_name);
        }
    }

    if(packet){
        free(packet);
        packet = NULL;
    }
    return ret;
}


void msgproc_sdcard(ijcommand* ijcmd)
{
    LOGDEBUG("msgproc_sdcard");

    const int tmpsize = ijcmd->msg.length;
    char token[] = "\n";
    char tmpdata[tmpsize + 1];
    char* ret = NULL;

    memset(tmpdata, 0, sizeof(tmpdata));
    memcpy(tmpdata, ijcmd->data, tmpsize);

    ret = strtok(tmpdata, token);
    if (!ret)
        return;

    LOGDEBUG("%s", ret);

    int mount_val = atoi(ret);
    int mount_status = 0;

    switch (mount_val)
    {
        case 0:                         // umount
            {
                mount_status = umount_sdcard();
            }
            break;
        case 1:                         // mount
            {
                memset(SDpath, '\0', sizeof(SDpath));
                ret = strtok(NULL, token);
                if (!ret) {
                    LOGERR("SDpath is missing from data.");
                    break;;
                }
                strncpy(SDpath, ret, strlen(ret));
                LOGDEBUG("sdcard path is %s", SDpath);

                if (pthread_create(&tid[TID_SDCARD], NULL, mount_sdcard, NULL) != 0)
                    LOGERR("mount sdcard pthread create fail!");
            }

            break;
        case 2:                         // mount status
            {
                mount_status = is_mounted();
                LXT_MESSAGE* mntData = (LXT_MESSAGE*) malloc(sizeof(LXT_MESSAGE));
                if (mntData == NULL) {
                    LOGERR("mntData allocation is failed.");
                    break;
                }
                memset(mntData, 0, sizeof(LXT_MESSAGE));

                mntData->length = strlen(SDpath);   // length
                mntData->group = 11;            // sdcard

                LOGDEBUG("SDpath is %s", SDpath);

                switch (mount_status)
                {
                    case 0:
                        {
                            mntData->action = 2;            // umounted status

                            const int tmplen = HEADER_SIZE + mntData->length;
                            char* tmp = (char*) malloc(tmplen);
                            if (!tmp) {
                                LOGERR("unmounted status message allocation is failed.");
                                break;
                            }

                            memcpy(tmp, mntData, HEADER_SIZE);
                            if (mntData->length > 0)
                            {
                                memcpy(tmp + HEADER_SIZE, SDpath, mntData->length);
                            }

                            ijmsg_send_to_evdi(g_fd[fdtype_device], IJTYPE_SDCARD, (const char*) tmp, tmplen);

                            free(tmp);

                            sprintf(SDpath, "umounted");
                        }
                        break;
                    case 1:
                        {
                            mntData->action = 3;            // mounted status

                            int mountinfo_size = 0;
                            char* mountinfo = get_mount_info();
                            if (mountinfo)
                            {
                                mountinfo_size = strlen(mountinfo);
                            }

                            const int tmplen = HEADER_SIZE + mntData->length + mountinfo_size;
                            char* tmp = (char*) malloc(tmplen);

                            if (tmp)
                            {
                                memcpy(tmp, mntData, HEADER_SIZE);
                                if (mntData->length > 0)
                                {
                                    memcpy(tmp + HEADER_SIZE, SDpath, mntData->length);
                                }

                                if (mountinfo)
                                {
                                    memcpy(tmp + HEADER_SIZE + mntData->length, mountinfo, mountinfo_size);
                                    mntData->length += mountinfo_size;
                                    memcpy(tmp, mntData, HEADER_SIZE);
                                    free(mountinfo);
                                }

                                ijmsg_send_to_evdi(g_fd[fdtype_device], IJTYPE_SDCARD, (const char*) tmp, tmplen);

                                free(tmp);
                            } else {
                                if (mountinfo) {
                                    free(mountinfo);
                                }
                            }
                        }
                        break;
                    default:
                        break;
                }
                free(mntData);
            }
            break;
        default:
            LOGERR("unknown data %s", ret);
            break;
    }
}

void* exec_cmd_thread(void *args)
{
    char *command = (char*)args;

    systemcall(command);
    LOGDEBUG("executed cmd: %s", command);
    free(command);

    pthread_exit(NULL);
}

void msgproc_cmd(ijcommand* ijcmd)
{
    _auto_mutex _(&mutex_cmd);
    pthread_t cmd_thread_id;
    char *cmd = (char*) malloc(ijcmd->msg.length + 1);

    if (!cmd) {
        LOGERR("malloc failed.");
        return;
    }

    memset(cmd, 0x00, ijcmd->msg.length + 1);
    strncpy(cmd, ijcmd->data, ijcmd->msg.length);
    LOGDEBUG("cmd: %s, length: %d", cmd, ijcmd->msg.length);

    if (pthread_create(&cmd_thread_id, NULL, exec_cmd_thread, (void*)cmd) != 0)
    {
        LOGERR("cmd pthread create fail!");
    }
}

// Location
#define STATUS              15
#define LOCATION_STATUS     120

/*
 * Location function
 */
static char* get_location_status(void* p)
{
    int mode, ret;
    char* message = (char*)malloc(MAX_CMD_LEN);
    if (!message) {
        LOGERR("get location message allocation is failed.");
        return NULL;
    }

    ret = vconf_get_int("db/location/replay/ReplayMode", &mode);
    if (ret != 0) {
        LOGERR("vconf_get_int ReplayMode failed %d", ret);
        return NULL;
    }

    if (mode == 0)
    { // STOP
        ret = sprintf(message, "%d", mode);
        if (ret < 0) {
            free(message);
            return NULL;
        }
    }
    else if (mode == 1)
    { // NMEA MODE(LOG MODE)
        char* temp = 0;
        temp = (char*) vconf_get_str("db/location/replay/FileName");
        if (!temp) {
            LOGERR("vconf_get_Str FileName failed.");
            return NULL;
        }

        ret = sprintf(message, "%d,%s", mode, temp);
        if (ret < 0) {
            free(message);
            return NULL;
        }
    } else if (mode == 2) { // MANUAL MODE
        double latitude;
        double logitude;
        double altitude;
        double accuracy;
        ret = vconf_get_dbl("db/location/replay/ManualLatitude", &latitude);
        if (ret != 0) {
            return 0;
        }
        ret = vconf_get_dbl("db/location/replay/ManualLongitude", &logitude);
        if (ret != 0) {
            return 0;
        }
        ret = vconf_get_dbl("db/location/replay/ManualAltitude", &altitude);
        if (ret != 0) {
            return 0;
        }
         ret = vconf_get_dbl("db/location/replay/ManualHAccuracy", &accuracy);
        if (ret != 0) {
            return 0;
        }

        ret = sprintf(message, "%d,%f,%f,%f,%f", mode, latitude, logitude, altitude, accuracy);
        if (ret < 0) {
            free(message);
            return NULL;
        }
    }

    LXT_MESSAGE* packet = (LXT_MESSAGE*)p;
    memset(packet, 0, sizeof(LXT_MESSAGE));
    packet->length = strlen(message);
    packet->group  = STATUS;
    packet->action = LOCATION_STATUS;

    return message;
}

static void* getting_location(void* data)
{
    pthread_detach(pthread_self());

    setting_device_param* param = (setting_device_param*) data;

    if (!param)
        return 0;

    char* msg = 0;
    LXT_MESSAGE* packet = (LXT_MESSAGE*)malloc(sizeof(LXT_MESSAGE));

    switch(param->ActionID)
    {
        case LOCATION_STATUS:
            msg = get_location_status((void*)packet);
            if (msg == 0) {
                LOGERR("failed getting location status");
            }
            break;
        default:
            LOGERR("Wrong action ID. %d", param->ActionID);
        break;
    }

    if (msg == 0)
    {
        LOGDEBUG("send error message to injector");
        memset(packet, 0, sizeof(LXT_MESSAGE));
        packet->length = 0;
        packet->group = STATUS;
        packet->action = param->ActionID;
    }
    else
    {
        LOGDEBUG("send data to injector");
    }

    const int tmplen = HEADER_SIZE + packet->length;
    char* tmp = (char*) malloc(tmplen);
    if (tmp)
    {
        memcpy(tmp, packet, HEADER_SIZE);
        if (packet->length > 0)
            memcpy(tmp + HEADER_SIZE, msg, packet->length);

        ijmsg_send_to_evdi(g_fd[fdtype_device], param->type_cmd, (const char*) tmp, tmplen);

        free(tmp);
    }

    if(msg != 0)
    {
        free(msg);
        msg = 0;
    }
    if (packet != NULL) {
        free(packet);
    }
    if (param)
        delete param;

    pthread_exit((void *) 0);
}

void setting_location(char* databuf)
{
    char command[MAX_CMD_LEN];
    char* s = strchr(databuf, ',');
    memset(command, 0, sizeof(command));
    if (s == NULL) { // SET MODE
        int mode = atoi(databuf);
        switch (mode) {
        case 0: // STOP MODE
            sprintf(command, "vconftool set -t int db/location/replay/ReplayMode 0 -f");
            break;
        case 1: // NMEA MODE (LOG MODE)
            sprintf(command, "vconftool set -t int db/location/replay/ReplayMode 1 -f");
            break;
        case 2: // MANUAL MODE
            sprintf(command, "vconftool set -t int db/location/replay/ReplayMode 2 -f");
            break;
        default:
            LOGERR("error(%s) : stop replay mode", databuf);
            sprintf(command, "vconftool set -t int db/location/replay/ReplayMode 0 -f");
            break;
        }
        LOGDEBUG("Location Command : %s", command);
        systemcall(command);
    } else {
        *s = '\0';
        int mode = atoi(databuf);
        if(mode == 1) { // NMEA MODE (LOG MODE)
            sprintf(command, "vconftool set -t string db/location/replay/FileName \"%s\"", s+1);
            LOGDEBUG("%s", command);
            systemcall(command);
            memset(command, 0, sizeof(command));
            sprintf(command, "vconftool set -t int db/location/replay/ReplayMode 1 -f");
            LOGDEBUG("%s", command);
            systemcall(command);
        } else if(mode == 2) {
            char* ptr = strtok(s+1, ",");

            // Latitude
            sprintf(command, "vconftool set -t double db/location/replay/ManualLatitude %s -f", ptr);
            LOGINFO("%s", command);
            systemcall(command);

            // Longitude
            ptr = strtok(NULL, ",");
            sprintf(command, "vconftool set -t double db/location/replay/ManualLongitude %s -f", ptr);
            LOGINFO("%s", command);
            systemcall(command);

            // Altitude
            ptr = strtok(NULL, ",");
            sprintf(command, "vconftool set -t double db/location/replay/ManualAltitude %s -f", ptr);
            LOGINFO("%s", command);
            systemcall(command);

            // accuracy
            ptr = strtok(NULL, ",");
            sprintf(command, "vconftool set -t double db/location/replay/ManualHAccuracy %s -f", ptr);
            LOGINFO("%s", command);
            systemcall(command);
        }
    }
}

void msgproc_location(ijcommand* ijcmd)
{
    LOGDEBUG("msgproc_location");
    if (ijcmd->msg.group == STATUS)
    {
        setting_device_param* param = new setting_device_param();
        if (!param)
            return;

        param->ActionID = ijcmd->msg.action;
        memcpy(param->type_cmd, ijcmd->cmd, ID_SIZE);

        if (pthread_create(&tid[TID_LOCATION], NULL, getting_location, (void*) param) != 0)
        {
            LOGERR("location pthread create fail!");
            return;
        }
    }
    else
    {
        setting_location(ijcmd->data);
    }
}

static char* make_header_msg(int group, int action)
{
    char *tmp = (char*) malloc(HEADER_SIZE);
    if (!tmp)
        return NULL;

    memset(tmp, 0, HEADER_SIZE);

    memcpy(tmp + 2, &group, 1);
    memcpy(tmp + 3, &action, 1);

    return tmp;
}

#define MSG_GROUP_HDS   100

void* mount_hds(void* data)
{
    int i, ret = 0;
    char* tmp;
    int group, action;

    LOGINFO("start hds mount thread");

    pthread_detach(pthread_self());

    usleep(500000);

    for (i = 0; i < 20; i++)
    {
        ret = mount("fileshare", "/mnt/host", "9p", 0,
                    "trans=virtio,version=9p2000.L,msize=65536");
        if(ret == 0) {
            action = 1;
            break;
        } else {
            LOGERR("%d trial: mount is failed with errno: %d", i, errno);
        }
        usleep(500000);
    }

    group = MSG_GROUP_HDS;

    if (i == 20 || ret != 0)
        action = 2;

    tmp = make_header_msg(group, action);
    if (!tmp) {
        LOGERR("failed to alloc: out of resource.");
        pthread_exit((void *) 0);
        return NULL;
    }

    ijmsg_send_to_evdi(g_fd[fdtype_device], IJTYPE_HDS, (const char*) tmp, HEADER_SIZE);

    free(tmp);

    pthread_exit((void *) 0);
}

int umount_hds(void)
{
    int ret = 0;
    char* tmp;
    int group, action;

    pthread_cancel(tid[TID_HDS]);

    LOGINFO("unmount /mnt/host.");

    ret = umount("/mnt/host");
    if (ret != 0) {
        LOGERR("unmount failed with error num: %d", errno);
        action = 4;
    } else {
        action = 3;
    }

    group = MSG_GROUP_HDS;

    tmp = make_header_msg(group, action);
    if (!tmp) {
        LOGERR("failed to alloc: out of resource.");
        return -1;
    }

    LOGINFO("send result with action %d to evdi", action);

    ijmsg_send_to_evdi(g_fd[fdtype_device], IJTYPE_HDS, (const char*) tmp, HEADER_SIZE);

    free(tmp);

    return 0;
}

void msgproc_hds(ijcommand* ijcmd)
{
    LOGDEBUG("msgproc_hds");

    if (ijcmd->msg.action == 1) {
        if (pthread_create(&tid[TID_HDS], NULL, mount_hds, NULL) != 0)
            LOGERR("mount hds pthread create fail!");
    } else if (ijcmd->msg.action == 2) {
        umount_hds();
    } else {
        LOGERR("unknown action cmd.");
    }
}

