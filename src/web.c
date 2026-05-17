/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(web, LOG_LEVEL_DBG);

#include "net_sample_common.h"

static void web_thread_entry(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(web_thread, 4096, web_thread_entry, NULL, NULL, NULL, 8, 0, 0);

static const uint16_t http_service_port = 8082;

static const uint8_t json_message[] = "{\"message\":\"HTTP server is running\"}";

HTTP_SERVICE_DEFINE(my_service, "0.0.0.0", &http_service_port, 1, 10, NULL, NULL, NULL);

static const struct http_resource_detail_static json_message_resource_detail = {
    .common = {
        .type = HTTP_RESOURCE_TYPE_STATIC,
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        .content_type = "application/json",
    },
    .static_data = json_message,
    .static_data_len = sizeof(json_message) - 1,
};

HTTP_RESOURCE_DEFINE(json_message_resource, my_service, "/", &json_message_resource_detail);

static void web_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Block thread to wait for getting an IP address from the network */
	wait_for_network();
	LOG_INF("Network is ready, starting HTTP server...");

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
            "Content-Length: 5\r\n"
            "\r\n"
            "Hello";

        send(client, response, sizeof(response) - 1, 0);

        close(client);
	}
}
