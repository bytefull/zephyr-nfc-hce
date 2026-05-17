/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(socket_server, LOG_LEVEL_DBG);

#include "net_sample_common.h"

static void socket_server_thread_entry(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(socket_server_thread, 4096, socket_server_thread_entry, NULL, NULL, NULL, 8, 0, 0);

static void socket_server_thread_entry(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Block thread to wait for getting an IP address from the network */
    wait_for_network();
    LOG_INF("Network is ready, starting TCP server...");

    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8082),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    bind(srv, (struct sockaddr *)&addr, sizeof(addr));
    listen(srv, 1);

    while (1) {
        int client = accept(srv, NULL, NULL);

        const char response[] =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 7\r\n"
            "\r\n"
            "Hello\r\n";

        send(client, response, sizeof(response) - 1, 0);

        close(client);
    }
}