/**
 * Copyright (C) 2007-2008 Felipe Contreras
 *
 * Purple is the legal property of its developers, whose names are too numerous
 * to list here.  Please refer to the COPYRIGHT file distributed with this
 * source distribution.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include "msn.h"
#include "servconn.h"
#include "error.h"

#include "msn_log.h"

/**************************************************************************
 * Main
 **************************************************************************/

MsnServConn *
msn_servconn_new(MsnSession *session, MsnServConnType type)
{
    MsnServConn *servconn;

    g_return_val_if_fail(session != NULL, NULL);

    msn_log ("begin");
    servconn = g_new0(MsnServConn, 1);

    servconn->type = type;

    servconn->session = session;
    servconn->cmdproc = msn_cmdproc_new(session);
    servconn->cmdproc->servconn = servconn;

#if 0
    servconn->httpconn = msn_httpconn_new(servconn);

    servconn->num = session->servconns_count++;

    servconn->tx_buf = purple_circ_buffer_new(MSN_BUF_LEN);
#endif

    msn_log ("end");

    return servconn;
}

void
msn_servconn_destroy(MsnServConn *servconn)
{
    g_return_if_fail(servconn != NULL);

    msn_log ("begin");

    if (servconn->processing)
    {
        servconn->wasted = TRUE;
        return;
    }

    conn_end_object_free (servconn->conn_end);

    if (servconn->destroy_cb)
        servconn->destroy_cb(servconn);

#if 0
    if (servconn->httpconn != NULL)
        msn_httpconn_destroy(servconn->httpconn);

    g_free(servconn->host);

    purple_circ_buffer_destroy(servconn->tx_buf);
#endif

    if (servconn->cmdproc)
    {
        msn_cmdproc_destroy (servconn->cmdproc);
    }

    g_free(servconn);
    msn_log ("end");
}

void
msn_servconn_set_destroy_cb(MsnServConn *servconn,
                            void (*destroy_cb)(MsnServConn *))
{
    g_return_if_fail(servconn != NULL);

    servconn->destroy_cb = destroy_cb;
}

/**************************************************************************
 * Connect
 **************************************************************************/

void
msn_servconn_disconnect(MsnServConn *servconn)
{
    g_return_if_fail(servconn != NULL);

    msn_log ("begin");

    if (servconn->connect_data != NULL)
    {
        purple_proxy_connect_cancel(servconn->connect_data);
        servconn->connect_data = NULL;
    }

    if (servconn->read_watch)
    {
        g_source_remove (servconn->read_watch);
        servconn->read_watch = 0;
    }

    conn_end_object_close (servconn->conn_end);

#if 0
    servconn->rx_buf = NULL;
    servconn->rx_len = 0;
    servconn->payload_len = 0;
#endif

    servconn->connected = FALSE;

    msn_log ("end");
}

#if 0
static int
create_listener(int port)
{
    int fd;
    int flags;
    const int on = 1;

#if 0
    struct addrinfo hints;
    struct addrinfo *c, *res;
    char port_str[5];

    snprintf(port_str, sizeof(port_str), "%d", port);

    memset(&hints, 0, sizeof(hints));

    hints.ai_flags = AI_PASSIVE;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(NULL, port_str, &hints, &res) != 0)
    {
        purple_debug_error("msn", "Could not get address info: %s.\n",
                           port_str);
        return -1;
    }

    for (c = res; c != NULL; c = c->ai_next)
    {
        fd = socket(c->ai_family, c->ai_socktype, c->ai_protocol);

        if (fd < 0)
            continue;

        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

        if (bind(fd, c->ai_addr, c->ai_addrlen) == 0)
            break;

        close(fd);
    }

    if (c == NULL)
    {
        purple_debug_error("msn", "Could not find socket: %s.\n", port_str);
        return -1;
    }

    freeaddrinfo(res);
#else
    struct sockaddr_in sockin;

    fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) != 0)
    {
        close(fd);
        return -1;
    }

    memset(&sockin, 0, sizeof(struct sockaddr_in));
    sockin.sin_family = AF_INET;
    sockin.sin_port = htons(port);

    if (bind(fd, (struct sockaddr *)&sockin, sizeof(struct sockaddr_in)) != 0)
    {
        close(fd);
        return -1;
    }
#endif

    if (listen (fd, 4) != 0)
    {
        close (fd);
        return -1;
    }

    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    return fd;
}
#endif
