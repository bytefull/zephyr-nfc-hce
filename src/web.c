/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(web, LOG_LEVEL_DBG);

#include "net_sample_common.h"

static void web_thread_entry(void *p1, void *p2, void *p3);
K_THREAD_DEFINE(web_thread, 4096, web_thread_entry, NULL, NULL, NULL, 8, 0, 0);

static const uint16_t http_service_port = 8080;
HTTP_SERVICE_DEFINE(my_service, "0.0.0.0", &http_service_port, 1, 10, NULL, NULL, NULL);

static int buzzer_post_handler(struct http_client_ctx *client, enum http_transaction_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx, void *user_data);

/* *************************** index.html *************************** */
static const uint8_t index_html_gz[] = {
#include "index.html.gz.inc"
};
static struct http_resource_detail_static index_html_gz_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/html",
		},
	.static_data = index_html_gz,
	.static_data_len = sizeof(index_html_gz),
};
HTTP_RESOURCE_DEFINE(index_html_gz_resource, my_service, "/", &index_html_gz_resource_detail);

/* *************************** bootstrap.min.css *************************** */
static const uint8_t bootstrap_min_css_gz[] = {
#include "bootstrap.min.css.gz.inc"
};
static struct http_resource_detail_static bootstrap_min_css_gz_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/css",
		},
	.static_data = bootstrap_min_css_gz,
	.static_data_len = sizeof(bootstrap_min_css_gz),
};
HTTP_RESOURCE_DEFINE(bootstrap_min_css_gz_resource, my_service, "/bootstrap.min.css",
		     &bootstrap_min_css_gz_resource_detail);

/* *************************** style.css *************************** */
static const uint8_t style_css_gz[] = {
#include "style.css.gz.inc"
};
static struct http_resource_detail_static style_css_gz_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "text/css",
		},
	.static_data = style_css_gz,
	.static_data_len = sizeof(style_css_gz),
};
HTTP_RESOURCE_DEFINE(style_css_gz_resource, my_service, "/style.css",
		     &style_css_gz_resource_detail);

/* *************************** script.js *************************** */
static const uint8_t script_js_gz[] = {
#include "script.js.gz.inc"
};
static struct http_resource_detail_static script_js_gz_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_encoding = "gzip",
			.content_type = "application/javascript",
		},
	.static_data = script_js_gz,
	.static_data_len = sizeof(script_js_gz),
};
HTTP_RESOURCE_DEFINE(script_js_gz_resource, my_service, "/script.js",
		     &script_js_gz_resource_detail);

/* *************************** json data *************************** */
static struct http_resource_detail_static json_message_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_STATIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_GET),
			.content_type = "application/json",
		},
	.static_data = "{\"message\":\"HTTP server is running\"}",
	.static_data_len = sizeof("{\"message\":\"HTTP server is running\"}") - 1,
};
HTTP_RESOURCE_DEFINE(json_message_resource, my_service, "/data", &json_message_resource_detail);

/* *************************** buzzer *************************** */
static struct http_resource_detail_dynamic buzzer_resource_detail = {
	.common =
		{
			.type = HTTP_RESOURCE_TYPE_DYNAMIC,
			.bitmask_of_supported_http_methods = BIT(HTTP_POST),
			.content_type = "application/json",
		},
	.cb = buzzer_post_handler,
	.user_data = NULL,
};
HTTP_RESOURCE_DEFINE(buzzer_resource, my_service, "/buzzer", &buzzer_resource_detail);

static int buzzer_post_handler(struct http_client_ctx *client, enum http_transaction_status status,
			       const struct http_request_ctx *request_ctx,
			       struct http_response_ctx *response_ctx, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(response_ctx);
	ARG_UNUSED(user_data);

	static size_t cursor = 0;
	static char payload[64] = {'\0'};

	LOG_INF("Buzzer handler status %d, len %zu", status, request_ctx->data_len);

	/* Reset on abort/complete */
	if ((status == HTTP_SERVER_TRANSACTION_ABORTED) ||
	    (status == HTTP_SERVER_TRANSACTION_COMPLETE)) {
		cursor = 0;
		return 0;
	}

	/* Prevent overflow */
	if ((cursor + request_ctx->data_len) >= sizeof(payload)) {
		cursor = 0;
		return -ENOMEM;
	}

	/* Append chunk */
	memcpy(payload + cursor, request_ctx->data, request_ctx->data_len);

	cursor += request_ctx->data_len;

	/* Null terminate */
	payload[cursor] = '\0';

	/* Wait until final chunk */
	if (status == HTTP_SERVER_REQUEST_DATA_FINAL) {
		LOG_INF("Received JSON: %s", payload);
		if (strcmp(payload, "{\"buzzer\":true}") == 0) {
			LOG_INF("BUZZER ON");
		} else if (strcmp(payload, "{\"buzzer\":false}") == 0) {
			LOG_INF("BUZZER OFF");
		} else {
			LOG_INF("Unknown payload");
		}
		cursor = 0;
	}

	return 0;
}

static void web_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Block thread to wait for getting an IP address from the network */
	wait_for_network();

	LOG_INF("Network is ready, starting HTTP server...");

	http_server_start();

	while (1) {
		k_msleep(2000);
	}
}
