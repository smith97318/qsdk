/*
 * Copyright (c) 2015-2019, 2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

#ifndef __WLFW_QMI_CLIENT_H__
#define __WLFW_QMI_CLIENT_H__
#include <pthread.h>

enum wlfw_svc_flag {
	SVC_START,
	SVC_RECONNECT,
	SVC_DISCONNECTED,
	SVC_EXIT,
};

#define MODEM_BASEBAND_PROPERTY   "ro.baseband"
#if defined(__BIONIC_FORTIFY)
#define MAX_PROPERTY_SIZE  PROP_VALUE_MAX
#else
#define MAX_PROPERTY_SIZE  10
#endif
#define MODEM_BASEBAND_VALUE_APQ  "apq"
#define MODEM_BASEBAND_VALUE_SDA  "sda"
#define MODEM_BASEBAND_VALUE_QCS  "qcs"

enum wlfw_instance_id {
	ADRASTEA_ID = 0x0,
	HASTINGS_ID = 0x1,
	HAWKEYE_ID = 0x2,
	MOSELLE_ID = 0x3,
	PINE_1_ID = 0x27,
	PINE_2_ID = 0x28,
	PINE_3_ID = 0x29,
	PINE_4_ID = 0x2a,
	WAIKIKI_1_ID = 0x37,
	WAIKIKI_2_ID = 0x38,
	WAIKIKI_3_ID = 0x39,
	WAIKIKI_4_ID = 0x3a,
	SPRUCE_1_ID = 0x41,
	SPRUCE_2_ID = 0x42,
};

#ifdef ICNSS_QMI
#define MAX_NUM_RADIOS 4
extern uint16_t g_instance_id_array[MAX_NUM_RADIOS];

int wlfw_start(enum wlfw_svc_flag flag, uint8_t index);
int wlfw_stop(enum wlfw_svc_flag flag, uint8_t index);
int wlfw_qdss_trace_start(uint32_t instance_id);
int wlfw_qdss_trace_stop(unsigned long long option, uint32_t instance_id);
int wlfw_send_qdss_trace_config_download_req(uint32_t instance_id);
void wlfw_qdss_trace_config_download_and_start(uint32_t instance_id);
void wlfw_set_qdss_max_file_len(uint32_t instance_id, uint32_t value);
const char *get_device_name_by_instance_id(uint32_t instance_id);
const char *get_file_suffix_by_instance_id(uint32_t instance_id);
int get_instance_id_by_device_name(char *device_name,
				   uint32_t *instance_id);
void wlfw_set_qmi_timeout(uint32_t instance_id, uint32_t msec);
#else
static inline int wlfw_start(enum wlfw_svc_flag flag, uint8_t index)
{
	flag;
	return 0;
}
static inline int wlfw_stop(enum wlfw_svc_flag flag, uint8_t index)
{
	flag;
	return 0;
}
static inline int wlfw_qdss_trace_start(uint32_t instance_id)
{
	return 0;
}
static inline int wlfw_qdss_trace_stop(unsigned int option,
				       uint32_t instance_id)
{
	return 0;
}
static inline int wlfw_send_qdss_trace_config_download_req(uint32_t instance_id)
{
	return 0;
}
static inline void wlfw_qdss_trace_config_download_and_start(uint32_t
							     instance_id)
{
}
static inline void wlfw_set_qdss_max_file_len(uint32_t instance_id,
					      uint32_t value)
{
}
static inline const char *get_device_name_by_instance_id(uint32_t instance_id)
{
	return NULL;
}
static inline const char *get_file_suffix_by_instance_id(uint32_t instance_id)
{
	return NULL;
}
static inline int get_instance_id_by_device_name(char *device_name,
						 uint32_t *instance_id)
{
	return 0;
}
static inline void wlfw_set_qmi_timeout(uint32_t instance_id,
					uint32_t msec)
{
}
#endif /* ICNSS_QMI */


#endif /* __WLFW_QMI_CLIENT_H__ */
