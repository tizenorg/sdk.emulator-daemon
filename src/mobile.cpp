/*
 * emulator-daemon
 *
 * Copyright (c) 2000 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Jinhyung Choi <jinhyung2.choi@samsnung.com>
 * DaiYoung Kim <daiyoung777.kim@samsnung.com>
 * SooYoung Ha <yoosah.ha@samsnung.com>
 * Sungmin Ha <sungmin82.ha@samsung.com>
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

#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "emuld.h"
#include "mobile.h"


void process_evdi_command(ijcommand* ijcmd)
{
    int fd = -1;

    if (strncmp(ijcmd->cmd, "sensor", 6) == 0)
    {
        msgproc_sensor(fd, ijcmd);
    }
    else if (strncmp(ijcmd->cmd, "suspend", 7) == 0)
    {
        msgproc_suspend(fd, ijcmd);
    }
    else if (strncmp(ijcmd->cmd, "location", 8) == 0)
    {
        msgproc_location(fd, ijcmd);
    }
    else if (strncmp(ijcmd->cmd, "hds", 3) == 0)
    {
        msgproc_hds(fd, ijcmd);
    }
    else if (strncmp(ijcmd->cmd, "system", 6) == 0)
    {
        msgproc_system(fd, ijcmd);
    }
    else if (strncmp(ijcmd->cmd, "sdcard", 6) == 0)
    {
        msgproc_sdcard(fd, ijcmd);
    }
    else if (strncmp(ijcmd->cmd, "cmd", 3) == 0)
    {
        msgproc_cmd(fd, ijcmd);
    }
    else
    {
        LOGERR("Unknown packet: %s", ijcmd->cmd);
    }
}

bool server_process(void)
{
    int i,nfds;
    int fd_tmp;

    nfds = epoll_wait(g_epoll_fd, g_events, MAX_EVENTS, 100);

    if (nfds == -1 && errno != EAGAIN && errno != EINTR)
    {
        LOGERR("epoll wait(%d)", errno);
        return true;
    }

    for( i = 0 ; i < nfds ; i++ )
    {
        fd_tmp = g_events[i].data.fd;
        if (fd_tmp == g_fd[fdtype_device])
        {
            recv_from_evdi(fd_tmp);
        }
        else
        {
        	LOGERR("unknown request event fd : (%d)", fd_tmp);
        }
    }

    return false;
}

void init_profile(void)
{
}

void exit_profile(void)
{
}


