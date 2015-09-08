/*
 * emulator-daemon
 *
 * Copyright (c) 2000 - 2013 Samsung Electronics Co., Ltd. All rights reserved.
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

#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#include "emuld.h"

static pthread_mutex_t mutex_climap = PTHREAD_MUTEX_INITIALIZER;

CliMap g_climap;

void clipool_add(int fd, unsigned short port, const int fdtype)
{
    _auto_mutex _(&mutex_climap);

    static CliSN s_id = 0;

    CliSN id = s_id;
    s_id++;
    Cli* cli = new Cli(id, fdtype, fd, port);
    if (!cli)
        return;

    if (!g_climap.insert(CliMap::value_type(fd, cli)).second)
        return;

    LOGINFO("clipool_add fd = %d, port = %d, type = %d", fd, port, fdtype);
}


void close_cli(int cli_fd)
{
    clipool_delete(cli_fd);
    close(cli_fd);
}

void clipool_delete(int fd)
{
    _auto_mutex _(&mutex_climap);

    CliMap::iterator it = g_climap.find(fd);

    if (it != g_climap.end())
    {
        Cli* cli = it->second;
        g_climap.erase(it);

        if (cli)
        {
            delete cli;
            cli = NULL;
        }
    }

    LOGINFO("clipool_delete fd = %d", fd);
}

// for thread safe
bool send_to_cli(const int fd, char* data, const int len)
{
    _auto_mutex _(&mutex_climap);

    CliMap::iterator it = g_climap.find(fd);
    if (it == g_climap.end())
        return false;

    Cli* cli = it->second;

    if (send(cli->sockfd, data, len, 0) == -1)
        return false;

    return true;
}

bool send_to_all_ij(char* data, const int len)
{
    _auto_mutex _(&mutex_climap);

    bool result = false;
    CliMap::iterator it, itend = g_climap.end();

    for (it = g_climap.begin(); it != itend; it++)
    {
        Cli* cli = it->second;

        if (!cli)
            continue;

        int sent = send(cli->sockfd, data, len, 0);
        result = (sent == -1) ? false : true;
        if (sent == -1)
        {
            LOGERR("failed to send to ij");
        }

        LOGDEBUG("send_len: %d, err= %d", sent, errno);
    }
    return result;
}

bool is_ij_exist()
{
    _auto_mutex _(&mutex_climap);

    bool result = (g_climap.size() > 0) ? true : false;
    return result;
}

void stop_listen(void)
{
    pthread_mutex_destroy(&mutex_climap);;
}





