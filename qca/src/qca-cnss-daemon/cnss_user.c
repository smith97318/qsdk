/*
 * Copyright (c) 2019, 2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <errno.h>

#include "debug.h"
#include "wlfw_qmi_client.h"
#include "cnss_cli.h"
#include "cnss_genl.h"
#include "cnss_genl_msg.h"

static int cnss_user_sock;

int cnss_user_get_fd(void)
{
	if (cnss_user_sock > 0)
		return cnss_user_sock;
	else
		return -1;
}

int cnss_user_socket_init(int sock_type)
{
	int sockfd = -1;
	struct sockaddr_un un_address;
	struct sockaddr_in in_address;
	int ret = 0;

	switch (sock_type) {
	case AF_UNIX:
		if (strlen(CNSS_USER_SERVER) >
		    (sizeof(un_address.sun_path) - 1)) {
			wsvc_printf_err("Invalid server path %s\n",
					CNSS_USER_SERVER);
			ret = -EINVAL;
			break;
		}

		sockfd = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (sockfd < 0) {
			wsvc_printf_err("Fail to create user socket %s\n",
					strerror(errno));
			ret = -errno;
			break;
		}

		memset(&un_address, 0, sizeof(struct sockaddr_un));
		un_address.sun_family = AF_UNIX;
		strlcpy(un_address.sun_path, CNSS_USER_SERVER,
			(sizeof(un_address.sun_path) - 1));

		if (bind(sockfd, (struct sockaddr *)&un_address,
			 sizeof(un_address)) < 0) {
			wsvc_printf_err("Fail to bind user socket %s\n",
					strerror(errno));
			close(sockfd);
			ret = -errno;
		}
		break;
	case AF_INET:
		sockfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sockfd < 0) {
			wsvc_printf_err("Fail to create user socket %s\n",
					strerror(errno));
			ret = -errno;
			break;
		}

		in_address.sin_family = AF_INET;
		in_address.sin_addr.s_addr = INADDR_ANY;
		in_address.sin_port = htons(CNSS_USER_PORT);

		if (bind(sockfd, (struct sockaddr *)&in_address,
			 sizeof(in_address)) < 0) {
			wsvc_printf_err("Fail to bind user socket %s\n",
					strerror(errno));
			close(sockfd);
			ret = -errno;
		}
		break;
	default:
		wsvc_printf_err("%s: Unknown sock type %d\n",
				__func__, sock_type);
		ret = -EINVAL;
	}

	cnss_user_sock = sockfd;
	return ret;
}

void cnss_user_socket_deinit(void)
{
	struct sockaddr sock_addr = {0};
	socklen_t sock_len = sizeof(struct sockaddr);

	if (!cnss_user_sock)
		return;

	if (getsockname(cnss_user_sock, &sock_addr, &sock_len) < 0) {
		wsvc_printf_err("Failed to get sock name %s\n",
				strerror(errno));
		goto out;
	}

	if (sock_addr.sa_family == AF_UNIX)
		remove(CNSS_USER_SERVER);

out:
	close(cnss_user_sock);
	cnss_user_sock = 0;
}

static void handle_qdss_trace_start(void)
{
	wlfw_qdss_trace_start(0);
}

static
void handle_qdss_trace_stop(uint32_t instance_id,
			    struct cnss_cli_config_param_data *data)
{
	wlfw_qdss_trace_stop(data->value, instance_id);
}

static void handle_qdss_trace_config_download(void)
{
	wlfw_send_qdss_trace_config_download_req(0);
}

static void handle_qdss_trace_config_and_start(uint32_t instance_id)
{
	wlfw_qdss_trace_config_download_and_start(instance_id);
}

static void handle_config_param(enum cnss_cli_cmd_type type,
				uint32_t instance_id,
				struct cnss_cli_config_param_data *data)
{
	switch (type) {
	case QDSS_TRACE_DATA_FILE_SIZE:
		/* value is in MB, convert it to bytes */
		wlfw_set_qdss_max_file_len(instance_id,
					   data->value*1024*1024);
		break;
	case WLFW_QMI_TIMEOUT:
		wlfw_set_qmi_timeout(instance_id, data->value);
		break;
	case DAEMON_SUPPORT:
		cnss_genl_send_data(CNSS_GENL_MSG_TYPE_DAEMON_SUPPORT,
				    instance_id, data->value);
		break;
	case COLD_BOOT_SUPPORT:
		cnss_genl_send_data(CNSS_GENL_MSG_TYPE_COLD_BOOT_SUPPORT,
				    instance_id, data->value);
		break;
	case HDS_SUPPORT:
		cnss_genl_send_data(CNSS_GENL_MSG_TYPE_HDS_SUPPORT,
				    instance_id, data->value);
		break;
	case REGDB_SUPPORT:
		cnss_genl_send_data(CNSS_GENL_MSG_TYPE_REGDB_SUPPORT,
				    instance_id, data->value);
		break;
	default:
		wsvc_printf_err("Unknown config param type %d\n", type);
	}
}

#ifdef IPQ
static int cnss_send_user_response(int fd, char *buffer, int resp_status)
{
	int ret = 0;
	char *resp_buffer = NULL;
	struct cnss_cli_msg_hdr *hdr = NULL;
	struct cnss_cli_msg_hdr *resp_hdr = NULL;
	struct sockaddr_un remote_addr;

	if (strlen(CNSS_USER_CLIENT) > (sizeof(remote_addr.sun_path) - 1)) {
		wsvc_printf_err("Invalid client path %s\n",
				CNSS_USER_CLIENT);
		return -EINVAL;
	}

	resp_buffer = calloc(1, CNSS_CLI_MAX_PAYLOAD);
	if (!resp_buffer) {
		wsvc_printf_err("Failed to allocate memory for user event resp_buffer");
		return -ENOMEM;
	}

	hdr = (struct cnss_cli_msg_hdr *)buffer;
	resp_hdr = (struct cnss_cli_msg_hdr *)resp_buffer;
	resp_hdr->type = hdr->type;
	resp_hdr->resp_status = resp_status;

	memset(&remote_addr, 0, sizeof(remote_addr));
	remote_addr.sun_family = AF_UNIX;
	strlcpy(remote_addr.sun_path, CNSS_USER_CLIENT,
		(sizeof(remote_addr.sun_path) - 1));

	wsvc_printf_dbg("Sending Response for type %d\n", hdr->type);
	if (sendto(fd, resp_buffer, CNSS_CLI_MAX_PAYLOAD, 0,
		   (struct sockaddr *)&remote_addr, sizeof(remote_addr)) < 0) {
		wsvc_printf_err("Failed to send to user socket %s",
				strerror(errno));
		ret = -errno;
	}

	free(resp_buffer);
	return ret;
}
#else
static int cnss_send_user_response(int fd, char *buffer, int resp_status)
{
	return 0;
}
#endif

void handle_cnss_user_event(int fd)
{
	unsigned int rbytes = 0;
	char *buffer = NULL;
	struct cnss_cli_msg_hdr *hdr = NULL;
	struct sockaddr sock_addr = {0};
	socklen_t sock_len = sizeof(struct sockaddr);
	struct sockaddr_un un_remote_addr;
	struct sockaddr_in in_remote_addr;
	unsigned int un_addrlen = sizeof(un_remote_addr);
	unsigned int in_addrlen = sizeof(in_remote_addr);
	int ret, resp_status = 0;
	void *data = NULL;
	uint32_t instance_id = 0;

	if (getsockname(fd, &sock_addr, &sock_len) < 0) {
		wsvc_printf_err("Failed to get sock name %s\n",
				strerror(errno));
		return;
	}

	buffer = calloc(1, CNSS_CLI_MAX_PAYLOAD);
	if (!buffer) {
		wsvc_printf_err("Failed to allocate memory for user event buffer");
		return;
	}

	if (sock_addr.sa_family == AF_UNIX)
		rbytes = recvfrom(fd, buffer, CNSS_CLI_MAX_PAYLOAD, 0,
				  (struct sockaddr *)&un_remote_addr,
				  &un_addrlen);
	else if (sock_addr.sa_family == AF_INET)
		rbytes = recvfrom(fd, buffer, CNSS_CLI_MAX_PAYLOAD, 0,
				  (struct sockaddr *)&in_remote_addr,
				  &in_addrlen);

	if (rbytes <= 0) {
		wsvc_printf_err("Failed to receive from socket %s",
				strerror(errno));
		resp_status = -EBUSY;
		goto out;
	}

	if (rbytes < sizeof(struct cnss_cli_msg_hdr)) {
		wsvc_printf_err("Invalid number of bytes (%d) received on socket",
				rbytes);
		resp_status = -EINVAL;
		goto send_resp;
	}

	hdr = (struct cnss_cli_msg_hdr *)buffer;
	wsvc_printf_dbg("Receive user message: %d, len %d interface %s\n",
			hdr->type, hdr->len, hdr->interface);

	if (get_instance_id_by_device_name(hdr->interface, &instance_id)) {
		wsvc_printf_err("Invalid interface name %s\n",
				hdr->interface);
		resp_status = -ENODEV;
		goto send_resp;
	}
	wsvc_printf_dbg("Instance_id = %d\n", instance_id);

	switch (hdr->type) {
	case QDSS_TRACE_START:
		handle_qdss_trace_start();
		break;
	case QDSS_TRACE_STOP:
		if (hdr->len != sizeof(struct cnss_cli_config_param_data)) {
			wsvc_printf_err("Invalid data length: %d\n", hdr->len);
			resp_status = -EINVAL;
			goto send_resp;
		}
		data = (char *)hdr + sizeof(struct cnss_cli_msg_hdr);
		handle_qdss_trace_stop(instance_id, data);
		break;
	case QDSS_TRACE_CONFIG_DOWNLOAD:
		handle_qdss_trace_config_download();
		break;
	case QDSS_TRACE_CONFIG_AND_START:
		handle_qdss_trace_config_and_start(instance_id);
		break;
	case WLFW_QMI_TIMEOUT:
	case QDSS_TRACE_DATA_FILE_SIZE:
	case DAEMON_SUPPORT:
	case COLD_BOOT_SUPPORT:
	case HDS_SUPPORT:
	case REGDB_SUPPORT:
		if (hdr->len != sizeof(struct cnss_cli_config_param_data)) {
			wsvc_printf_err("Invalid data length %d for type %d\n",
					hdr->len, hdr->type);
			resp_status = -EINVAL;
			goto send_resp;
		}
		data = (char *)hdr + sizeof(struct cnss_cli_msg_hdr);
		handle_config_param(hdr->type, instance_id, data);
		break;
	default:
		wsvc_printf_err("Unknown command %d\n", hdr->type);
		resp_status = -EINVAL;
		break;
	}

send_resp:
	ret = cnss_send_user_response(fd, buffer, resp_status);
	if (ret)
		wsvc_printf_err("Failed to send response to cnsscli %d", ret);

out:
	free(buffer);
}
