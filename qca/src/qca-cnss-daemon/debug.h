/*
 * Copyright (c) 2014, 2017, 2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 */

#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>

enum log_level {
	MSG_INVAL,
	MSG_ERROR,
	MSG_INFO_HIGH,
	MSG_INFO,
	MSG_DEBUG,
	MSG_DUMP
};

#ifdef USE_GLIB
#include <glib.h>
#define strlcat g_strlcat
#define strlcpy g_strlcpy
#endif

extern int wsvc_debug_level;
extern int wsvc_kmsg_logging;

#define MSG_DEFAULT      MSG_INFO

#define UNUSED(x) (void)(x)

#ifdef __GNUC__
#define PRINTF_FORMAT(a,b) __attribute__ ((format (printf, (a), (b))))
#else
#define PRINTF_FORMAT(a,b)
#endif

#define wsvc_printf_err(_fmt...) wsvc_printf(MSG_ERROR, _fmt)
#define wsvc_perror(_str) wsvc_printf(MSG_ERROR, "%s: %s(%d)",\
				      _str, strerror(errno), errno)

#ifdef CONFIG_DEBUG

void _wsvc_hexdump(enum log_level level, const char *title, const void *p,
		   int len);
void _wsvc_printf_mac_addr(enum log_level level, const char *fmt,
			const unsigned char *addr);

#define wsvc_hexdump(title, p, len)  _wsvc_hexdump(MSG_DUMP, title, p, len)
#define wsvc_printf_info_high(_fmt...) wsvc_printf(MSG_INFO_HIGH, _fmt)
#define wsvc_printf_info(_fmt...) wsvc_printf(MSG_INFO, _fmt)
#define wsvc_printf_dbg(_fmt...) wsvc_printf(MSG_DEBUG, _fmt)
#define wsvc_printf_mac_addr(level, str, addr) \
	_wsvc_printf_mac_addr(level, str, addr)

#else

#define wsvc_hexdump(tittle, p, len) do { } while (0)
#define wsvc_printf_info_high(_fmt...) do { } while (0)
#define wsvc_printf_info(_fmt...) do { } while (0)
#define wsvc_printf_dbg(_fmt...) do { } while (0)
#define wsvc_printf_mac_addr(level, str, addr) do { } while (0)

#endif

int wsvc_debug_init(void);
void wsvc_debug_deinit(void);
void wsvc_printf(enum log_level level,
		 const char *fmt, ...) PRINTF_FORMAT(2, 3);

#ifdef CONFIG_RECORD_DAEMON_QMI_LOG
int daemon_debuglog_file;
pthread_mutex_t qmi_record_mutex;
void daemon_qmihist_record(uint8_t instance_id, int8_t msg_id, int8_t err_msg,
			   int8_t resp_err_msg);
#else
inline void daemon_qmihist_record(uint8_t instance_id, int8_t msg_id,
				  int8_t err_msg, int8_t resp_err_msg)
{
	UNUSED(instance_id);
	UNUSED(msg_id);
	UNUSED(err_msg);
	UNUSED(resp_err_msg);
}
#endif

#ifdef CONFIG_DEBUG_FILE
int wsvc_debug_open_file(const char *path);
void wsvc_debug_close_file(void);
#else
static inline int wsvc_debug_open_file(const char *path)
{
	UNUSED(path);
	return 0;
}

static inline void wsvc_debug_close_file(void) { return; }
#endif

#ifdef CONFIG_DEBUG_SYSLOG
void wsvc_debug_open_syslog(void);
void wsvc_debug_close_syslog(void);
#else
static inline void wsvc_debug_open_syslog(void) { return; }
static inline void wsvc_debug_close_syslog(void) { return; }
#endif

#endif /* __DEBUG_H */
