/*
 * Copyright (c) 2026 Bayrem Gharsellaoui
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_config.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(web, LOG_LEVEL_DBG);

#include "net_sample_common.h"

static void web_thread_entry(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(web_thread, 1024, web_thread_entry, NULL, NULL, NULL, 8, 0, 0);

static void web_thread_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Block thread to wait for getting an IP address from the network */
	wait_for_network();

	while (1) {
		k_msleep(2000);
	}
}
