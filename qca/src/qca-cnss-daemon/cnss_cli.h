/*
 * Copyright (c) 2019, 2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */


#ifndef __CNSS_CLI_H__
#define __CNSS_CLI_H__

#define LOCAL_LOOPBACK "127.0.0.1"
#define CNSS_USER_PORT 8080
#define CNSS_CLI_MAX_PAYLOAD 1024
#define CNSS_CLI_MAX_INTERFACE_NAME_LEN 20
#ifdef IPQ
#define CNSS_USER_SERVER "/var/run/cnss_user_server"
#define CNSS_USER_CLIENT "/var/run/cnss_user_client"
#else
#define CNSS_USER_SERVER ""
#define CNSS_USER_CLIENT ""
#endif

enum cnss_cli_cmd_type {
	CNSS_CLI_CMD_NONE = 0,
	QDSS_TRACE_START,
	QDSS_TRACE_STOP,
	QDSS_TRACE_CONFIG_DOWNLOAD,
	QDSS_TRACE_CONFIG_AND_START,
	QDSS_TRACE_DATA_FILE_SIZE,
	DAEMON_SUPPORT,
	COLD_BOOT_SUPPORT,
	HDS_SUPPORT,
	WLFW_QMI_TIMEOUT,
	REGDB_SUPPORT,
};

struct cnss_cli_msg_hdr {
	enum cnss_cli_cmd_type type;
	int len;
	int resp_status;
	char interface[CNSS_CLI_MAX_INTERFACE_NAME_LEN];
};

struct cnss_cli_config_param_data {
	unsigned int value;
};
#endif

