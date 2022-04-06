/******************************************************************************
  ---------------------------------------------------------------------------
  Copyright (c) 2017-2018 Qualcomm Technologies, Inc.
  All Rights Reserved.
  Confidential and Proprietary - Qualcomm Technologies, Inc.
  ---------------------------------------------------------------------------
*******************************************************************************/

#include <stdio.h>
#include <limits.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <fcntl.h>
#include "qmi_cci_target.h"
#include "qmi_client.h"
#include "qmi_idl_lib.h"
#include "qmi_cci_common.h"
#include "qmi_fw_debug.h"
#include <linux/qrtr.h>
#include "libqrtr.h"
#include "ns.h"

#define ALIGN_SIZE(x) ((4 - ((x) & 3)) & 3)
/*
 * QCCI treats instance and version fields separately and uses IDL version
 * as the instance during service lookup. IPC Router passes the instance
 * (MS 24 bits) + IDL Version(LS 8 bits) fields together as the instance info.
 *
 * Macro to translate between IPC Router specific instance information
 * to QCCI specific instance information.
 */
#define GET_XPORT_SVC_INSTANCE(x) GET_VERSION(x)

struct xport_ipc_router_server_addr {
        uint32_t service;
        uint32_t instance;
        uint32_t node_id;
        uint32_t port_id;
};

struct reader_tdata {
  pthread_attr_t reader_tattr;
  pthread_t reader_tid;
  int wakeup_pipe[2];
};

struct msm_ipc_port_name { // TODO : does it really required or replace with normal address ?
	uint32_t service;
	uint32_t instance;
};

struct xport_handle
{
  qmi_cci_client_type *clnt;
  int fd;
  struct reader_tdata rdr_tdata;
  uint32_t max_rx_len;
  struct msm_ipc_port_name srv_name;
  int srv_conn_reset;
  uint8_t svc_addr[MAX_ADDR_LEN];
  uint8_t is_client;
  LINK(struct xport_handle, link);
};

struct xport_ctrl_port {
  int ctl_fd;
  struct reader_tdata rdr_tdata;
  qmi_cci_lock_type xport_list_lock;
  LIST(struct xport_handle, xport);
};

static struct xport_ctrl_port *ctrl_port;
static pthread_mutex_t ctrl_port_init_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lookup_fd_lock = PTHREAD_MUTEX_INITIALIZER;
static int lookup_sock_fd = -1;

static void close_lookup_sock_fd(void);
static void qmi_cci_xport_ctrl_port_deinit(void);

int qmi_cci_xprt_qrtr_supported(void)
{
    int fd;
    fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (fd < 0)
    {
      if(errno == EAFNOSUPPORT)
	{
		QMI_CCI_LOGD("%s: QRTR NOT SUPPORTED %d %d\n", __func__, fd, errno);
		return 0;
	}
      else
	{
		QMI_FW_LOGE("%s: failed errno[%d]\n", __func__, errno);
		return 1;
	}
    }
    QMI_CCI_LOGD("%s: QRTR SUPPORTED\n", __func__);
    close(fd);
    return 1;
}

void qmi_cci_xport_qrtr_deinit(void)
{
  qmi_cci_xport_ctrl_port_deinit();
}

static int open_lookup_sock_fd(void)
{
  pthread_mutex_lock(&lookup_fd_lock);
  if (lookup_sock_fd < 0)
  {
    lookup_sock_fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
    if (lookup_sock_fd < 0)
    {
      pthread_mutex_unlock(&lookup_fd_lock);
      QMI_FW_LOGE("%s: Lookup sock fd creation failed\n", __func__);
      return -1;
    }
    fcntl(lookup_sock_fd, F_SETFD, FD_CLOEXEC);
  }
  pthread_mutex_unlock(&lookup_fd_lock);
  return 0;
}

static void close_lookup_sock_fd(void)
{
  pthread_mutex_lock(&lookup_fd_lock);
  close(lookup_sock_fd);
  lookup_sock_fd = -1;
  pthread_mutex_unlock(&lookup_fd_lock);
}

static int send_lookup_cmd(int sock, uint32_t service, uint32_t version)
{
	struct qrtr_ctrl_pkt pkt;
	struct sockaddr_qrtr sq = {0};
	socklen_t sl = sizeof(sq);
	int rc;

	memset(&pkt, 0, sizeof(pkt));
	pkt.cmd = cpu_to_le32(QRTR_TYPE_NEW_LOOKUP);
	pkt.server.service = service;
	pkt.server.instance = version;

	rc = getsockname(sock, (void *)&sq, &sl);
	if (rc || sq.sq_family != AF_QIPCRTR) {
		QMI_FW_LOGE("%s: getsockname failed rc [%d]\n", __func__, rc);
		return -1;
	}

	sq.sq_port = QRTR_PORT_CTRL;

	rc = sendto(sock, &pkt, sizeof(pkt), 0, (void *)&sq, sizeof(sq));
	if (rc < 0) {
		QMI_FW_LOGE("%s: sendto failed rc [%d]\n", __func__, rc);
		return -1;
	}

	return 0;
}


/*===========================================================================
    FUNCTION  release_xp
 ===========================================================================*/
/*!
 * @brief
 *
 *   This function releases the xport handle associated with the control port.
 *
 *   @return
 *     None
 *
 */
/*=========================================================================*/
static void release_xp(struct xport_handle *xp)
{
  struct xport_handle *temp;

  LOCK(&ctrl_port->xport_list_lock);
  LIST_FIND(ctrl_port->xport, temp, link, temp == xp);
  if (temp)
    LIST_REMOVE(ctrl_port->xport, temp, link);
  UNLOCK(&ctrl_port->xport_list_lock);
  qmi_cci_xport_closed(xp->clnt);
  free(xp);
}


/*===========================================================================
    FUNCTION  ctrl_msg_reader_thread
 ===========================================================================*/
/*!
 * @brief
 *
 *   This function reads all the control messages from control port for a
 *   specific process.
 *
 *   @return
 *     None
 *
 */
/*=========================================================================*/
static void *ctrl_msg_reader_thread(void *arg)
{
  struct xport_handle *xp;
  struct xport_ipc_router_server_addr src_addr;
  unsigned char ch;
  struct qrtr_ctrl_pkt rx_ctl_msg = {0};
  int rx_len, i;
  struct pollfd pbits[2];
  struct xport_ipc_router_server_addr *s_addr;

  while(1)
  {
    pbits[0].fd = ctrl_port->rdr_tdata.wakeup_pipe[0];
    pbits[0].events = POLLIN;
    pbits[1].fd = ctrl_port->ctl_fd;
    pbits[1].events = POLLIN;

    i = poll(pbits, 2, -1);
    if(i < 0)
    {
      if (errno == EINTR)
        QMI_CCI_LOGD("%s: poll error (%d)\n", __func__, errno);
      else
        QMI_FW_LOGE("%s: poll error (%d)\n", __func__, errno);
      continue;
    }

    if(pbits[1].revents & POLLIN)
    {
      rx_len = recvfrom(ctrl_port->ctl_fd, &rx_ctl_msg, sizeof(rx_ctl_msg), MSG_DONTWAIT,
                        NULL, NULL);
      if (rx_len < 0)
      {
        QMI_FW_LOGE("%s: Error recvfrom ctl_fd : %d\n", __func__, rx_len);
        break;
      }
      else if (rx_len == 0)
      {
        QMI_FW_LOGE("%s: No data read from %d\n", __func__, ctrl_port->ctl_fd);
        continue;
      }

      src_addr.service = rx_ctl_msg.server.service;
      src_addr.instance = rx_ctl_msg.server.instance;
      src_addr.node_id = rx_ctl_msg.server.node;
      src_addr.port_id = rx_ctl_msg.server.port;
      if (rx_ctl_msg.cmd == QRTR_TYPE_NEW_SERVER)
      {
        QMI_CCI_LOGD("Received NEW_SERVER cmd for %08x:%08x\n",
            rx_ctl_msg.server.service, rx_ctl_msg.server.instance);
        LOCK(&ctrl_port->xport_list_lock);
        for(xp = (ctrl_port->xport).head; xp; xp = (xp)->link.next)
          if (xp->srv_name.service ==  rx_ctl_msg.server.service &&
              xp->srv_name.instance == GET_XPORT_SVC_INSTANCE(rx_ctl_msg.server.instance))
            qmi_cci_xport_event_new_server(xp->clnt, &src_addr);
        UNLOCK(&ctrl_port->xport_list_lock);
      }
      else if (rx_ctl_msg.cmd == QRTR_TYPE_DEL_SERVER)
      {
        QMI_CCI_LOGD("Received REMOVE_SERVER cmd for %08x:%08x\n",
            rx_ctl_msg.server.service, rx_ctl_msg.server.instance);
        LOCK(&ctrl_port->xport_list_lock);
        for(xp = (ctrl_port->xport).head; xp; xp = (xp)->link.next)
        {
          if (xp->srv_name.service ==  rx_ctl_msg.server.service &&
              xp->srv_name.instance == GET_XPORT_SVC_INSTANCE(rx_ctl_msg.server.instance))
          {
            if (xp->is_client)
            {
              s_addr = (struct xport_ipc_router_server_addr *)xp->svc_addr;
              if (s_addr->node_id == src_addr.node_id &&
                  s_addr->port_id == src_addr.port_id)
              {
                xp->srv_conn_reset = 1;
                /* Wake up the client reader thread only if the REMOVE_SERVER is
                 * intended for this client.
                 */
                if (write(xp->rdr_tdata.wakeup_pipe[1], "r", 1) < 0)
                  QMI_FW_LOGE("%s: Error writing to pipe\n", __func__);
              }
            }
            else
            {
              /*It is a notifier port*/
              qmi_cci_xport_event_remove_server(xp->clnt, &src_addr);
            }
          }
        }
        UNLOCK(&ctrl_port->xport_list_lock);
      }
    }
    if(pbits[0].revents & POLLIN)
    {
      read(ctrl_port->rdr_tdata.wakeup_pipe[0], &ch, 1);
      QMI_CCI_LOGD("%s: wakeup_pipe[0]=%x ch=%c\n", __func__, pbits[0].revents, ch);
      if(ch == 'd')
      {
        close(ctrl_port->rdr_tdata.wakeup_pipe[0]);
        close(ctrl_port->rdr_tdata.wakeup_pipe[1]);
        close(ctrl_port->ctl_fd);
        pthread_attr_destroy(&ctrl_port->rdr_tdata.reader_tattr);
        LOCK(&ctrl_port->xport_list_lock);
        while(NULL != (xp = LIST_HEAD(ctrl_port->xport)))
          LIST_REMOVE(ctrl_port->xport, xp, link);
        UNLOCK(&ctrl_port->xport_list_lock);
        pthread_mutex_lock(&ctrl_port_init_lock);
        FREE(ctrl_port);
        pthread_mutex_unlock(&ctrl_port_init_lock);
        break;
      }
    }
    if (pbits[1].revents & POLLERR)
    {
      rx_len = recvfrom(ctrl_port->ctl_fd, &rx_ctl_msg, sizeof(rx_ctl_msg), MSG_DONTWAIT,
                        NULL, NULL);
      if (errno != ENETRESET)
        continue;

      QMI_FW_LOGE("%s: control thread received ENETRESET %d\n", __func__, errno);
      pthread_mutex_lock(&ctrl_port_init_lock);
      close(ctrl_port->ctl_fd);
      ctrl_port->ctl_fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
      if(ctrl_port->ctl_fd < 0)
        break;
      if(fcntl(ctrl_port->ctl_fd, F_SETFD, FD_CLOEXEC) < 0)
        break;
      if(send_lookup_cmd(ctrl_port->ctl_fd, 0, 0) < 0)
        break;
      pthread_mutex_unlock(&ctrl_port_init_lock);
    }
  }
  QMI_CCI_LOGD("%s: closing control port thread\n", __func__);
  return NULL;
}

/*===========================================================================
    FUNCTION  data_msg_reader_thread
 ===========================================================================*/
/*!
 * @brief
 *
 *   This function reads all the data messages for a specific client.
 *
 *   @return
 *     Transport handle or NULL incase of error.
 *
 */
/*=========================================================================*/
static void *data_msg_reader_thread(void *arg)
{
  struct xport_handle *xp = (struct xport_handle *)arg;
  unsigned char ch, *buf;
  int i;
  ssize_t rx_len;
  struct pollfd pbits[2];
  struct xport_ipc_router_server_addr src_addr;
  struct sockaddr_qrtr addr = {0};

  while(1)
  {
    pbits[0].fd = xp->rdr_tdata.wakeup_pipe[0];
    pbits[0].events = POLLIN;
    pbits[1].fd = xp->fd;
    pbits[1].events = POLLIN;

    i = poll(pbits, 2, -1);
    if(i < 0)
    {
      if (errno == EINTR)
        QMI_CCI_LOGD("%s: poll error (%d)\n", __func__, errno);
      else
        QMI_FW_LOGE("%s: poll error (%d)\n", __func__, errno);
      continue;
    }

    if((pbits[1].revents & POLLIN))
    {
      socklen_t addr_size = sizeof(struct sockaddr_qrtr);

      buf = (unsigned char *)calloc(xp->max_rx_len, 1);
      if(!buf)
      {
        QMI_FW_LOGE("%s: Unable to allocate read buffer for %p of size %d\n",
                     __func__, xp, xp->max_rx_len);
        break;
      }
      addr_size = sizeof(struct sockaddr_qrtr);
      rx_len = recvfrom(xp->fd, buf, xp->max_rx_len, MSG_DONTWAIT, (struct sockaddr *)&addr, &addr_size);
      if (rx_len < 0)
      {
        QMI_FW_LOGE("%s: Error recvfrom %p - rc : %d\n", __func__, xp, errno);
        free(buf);
        break;
      }
      else if (rx_len == 0)
      {
        if (addr_size == sizeof(struct sockaddr_qrtr))
        {
          QMI_CCI_LOGD("%s: QCCI Received Resume_Tx on FD %d from port %08x:%08x\n",
                        __func__, xp->fd, addr.sq_node, addr.sq_port);
          qmi_cci_xport_resume(xp->clnt);
        }
        else
        {
          QMI_FW_LOGE("%s: No data read from %d\n", __func__, xp->fd);
        }
        free(buf);
        continue;
      }
      else if (addr.sq_port == QRTR_PORT_CTRL)
      {
	/* NOT expected to receive data from control port */
        QMI_FW_LOGE("%s: DATA from control port len[%d]\n", __func__, rx_len);
        free(buf);
        continue;
      }

      QMI_CCI_LOGD("%s: Received %d bytes from %d\n", __func__, rx_len, xp->fd);
      src_addr.service = 0;
      src_addr.instance = 0;
      src_addr.node_id = addr.sq_node;
      src_addr.port_id = addr.sq_port;
      qmi_cci_xport_recv(xp->clnt, (void *)&src_addr, buf, (uint32_t)rx_len);
      free(buf);
    }
    if (pbits[0].revents & POLLIN)
    {
      read(xp->rdr_tdata.wakeup_pipe[0], &ch, 1);
      QMI_CCI_LOGD("%s: wakeup_pipe[0]=%x ch=%c\n", __func__, pbits[0].revents, ch);
      if(ch == 'd')
      {
        close(xp->rdr_tdata.wakeup_pipe[0]);
        close(xp->rdr_tdata.wakeup_pipe[1]);
        QMI_CCI_LOGD("Close[%d]\n", xp->fd);
        close(xp->fd);
        pthread_attr_destroy(&xp->rdr_tdata.reader_tattr);
        release_xp(xp);
        break;
      }
      else if (ch == 'r')
      {
        if (xp->srv_conn_reset)
          qmi_cci_xport_event_remove_server(xp->clnt, &xp->svc_addr);
      }
    }
    if (pbits[1].revents & POLLERR)
    {
      int sk_size;
      int flags;
      int err;

      rx_len = recvfrom(xp->fd, (void *)&err, sizeof(err), MSG_DONTWAIT, NULL, NULL);
      if (errno != ENETRESET)
        continue;

      QMI_FW_LOGE("%s: data thread received ENETRESET %d\n", __func__, errno);
      qmi_cci_xport_event_remove_server(xp->clnt, &xp->svc_addr);
      close(xp->fd);
      xp->fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
      if(xp->fd < 0)
        break;
      if(fcntl(xp->fd, F_SETFD, FD_CLOEXEC) < 0)
        break;
      flags = fcntl(xp->fd, F_GETFL, 0);
      fcntl(xp->fd, F_SETFL, flags | O_NONBLOCK);
      sk_size = INT_MAX;
      setsockopt(xp->fd, SOL_SOCKET, SO_RCVBUF, (char *)&sk_size, sizeof(sk_size));
    }
  }
  QMI_CCI_LOGD("%s data thread exiting\n", __func__);
  return NULL;
}

/*===========================================================================
    FUNCTION  reader_thread_data_init
 ===========================================================================*/
/*!
 * @brief
 *
 *   This function initilizes the reader threads and the pipes asociated with
 *   it.
 *
 *   @return
 *     0 on Success or -1 otherwise.
 *
 */
/*=========================================================================*/
static int reader_thread_data_init(struct reader_tdata *tdata, void *targs,
                                      void *(*rdr_thread)(void *arg))
{
  if (pipe(tdata->wakeup_pipe) == -1)
  {
    QMI_FW_LOGE("%s: failed to create pipe\n", __func__);
    return -1;
  }

  if (pthread_attr_init(&tdata->reader_tattr))
  {
    QMI_FW_LOGE("%s: Pthread reader thread attribute init failed\n", __func__);
    goto thread_init_close_pipe;
  }
  if (pthread_attr_setdetachstate(&tdata->reader_tattr, PTHREAD_CREATE_DETACHED))
  {
    QMI_FW_LOGE("%s: Pthread Set Detach State failed\n", __func__);
    goto thread_init_close_pipe;
  }
  /* create reader thread */
  if(pthread_create(&tdata->reader_tid, &tdata->reader_tattr, rdr_thread, targs))
  {
    QMI_FW_LOGE("%s: Reader thread creation failed\n", __func__);
    goto thread_init_close_pipe;
  }
  return 0;

thread_init_close_pipe:
  close(tdata->wakeup_pipe[0]);
  close(tdata->wakeup_pipe[1]);
  return -1;
}

/*===========================================================================
    FUNCTION  qmi_cci_xprt_ctrl_port_deinit
 ===========================================================================*/
/*!
 * @brief
 *
 *   Deinitilize the control port.
 *
 *   @return
 *     None
 *
 *     @note
 *     This function should be only called when the qcci library is unloaded
 *     as a result of process exit.
 */
/*=========================================================================*/
static void qmi_cci_xport_ctrl_port_deinit(void)
{
  pthread_mutex_lock(&ctrl_port_init_lock);
  if (!ctrl_port)
  {
    QMI_CCI_LOGD("%s: Control port is not initilized yet\n", __func__);
    pthread_mutex_unlock(&ctrl_port_init_lock);
    return;
  }
  if (write(ctrl_port->rdr_tdata.wakeup_pipe[1], "d", 1) < 0)
    QMI_FW_LOGE("%s: Error writing to pipe\n", __func__);
  pthread_mutex_unlock(&ctrl_port_init_lock);
}

/*===========================================================================
    FUNCTION  qmi_cci_xport_ctrl_port_init
 ===========================================================================*/
/*!
 * @brief
 *
 *   Initilize the control port only for the first time in the process context.
 *
 *   @return
 *     0 on success, -1 in case of any error.
 */
/*=========================================================================*/
static int qmi_cci_xport_ctrl_port_init(void)
{
  if (ctrl_port)
    return 0;

  ctrl_port = calloc(sizeof(struct xport_ctrl_port), 1);
  if (!ctrl_port)
  {
    QMI_FW_LOGE("%s: Control port calloc failed\n", __func__);
    return -1;
  }
  ctrl_port->ctl_fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
  if(ctrl_port->ctl_fd < 0)
  {
    QMI_FW_LOGE("%s: control socket creation failed - %d\n", __func__, errno);
    goto init_free_ctrl_port;
  }

  if(fcntl(ctrl_port->ctl_fd, F_SETFD, FD_CLOEXEC) < 0)
  {
    QMI_FW_LOGE("%s: Close on exec can't be set on ctl_fd - %d\n",
    __func__, errno);
    goto init_close_ctrl_fd;
  }

  if(send_lookup_cmd(ctrl_port->ctl_fd, 0, 0) < 0)//register for all services
  {
    QMI_FW_LOGE("%s: failed to register as control port\n", __func__);
    goto init_close_ctrl_fd;
  }
  LOCK_INIT(&ctrl_port->xport_list_lock);
  LIST_INIT(ctrl_port->xport);
  if (reader_thread_data_init(&ctrl_port->rdr_tdata,(void *)ctrl_port, ctrl_msg_reader_thread) < 0)
    goto init_close_ctrl_fd;
  QMI_CCI_LOGD("Control Port opened[%d]\n", ctrl_port->ctl_fd);
  return 0;

init_close_ctrl_fd:
  close(ctrl_port->ctl_fd);
init_free_ctrl_port:
  FREE(ctrl_port);
  return -1;
}

static void *xport_open
(
 void *xport_data,
 qmi_cci_client_type *clnt,
 uint32_t service_id,
 uint32_t version,
 void *addr,
 uint32_t max_rx_len
 )
{
  struct xport_handle *xp = calloc(sizeof(struct xport_handle), 1);
  int sk_size = INT_MAX;
  int align_size = 0;
  int flags;

  if (!xp)
  {
    QMI_FW_LOGE("%s: xp calloc failed\n", __func__);
    return NULL;
  }

  xp->clnt = clnt;
  xp->srv_name.service = service_id;
  xp->srv_name.instance = version;
  xp->max_rx_len = (max_rx_len + QMI_HEADER_SIZE);
  align_size = ALIGN_SIZE(xp->max_rx_len);
  xp->max_rx_len += align_size;
  LINK_INIT(xp->link);

  pthread_mutex_lock(&ctrl_port_init_lock);
  if (qmi_cci_xport_ctrl_port_init() < 0) {
      pthread_mutex_unlock(&ctrl_port_init_lock);
      goto xport_open_free_xp;
  }
  pthread_mutex_unlock(&ctrl_port_init_lock);

  if (!addr)
    /* No need to create data port as this is a notifier port. */
    goto xport_open_success;

  xp->fd = socket(AF_QIPCRTR, SOCK_DGRAM, 0);
  if(xp->fd < 0)
  {
    QMI_FW_LOGE("%s: socket creation failed - %d\n", __func__, errno);
    goto xport_open_free_xp;
  }

  if(fcntl(xp->fd, F_SETFD, FD_CLOEXEC) < 0)
  {
    QMI_FW_LOGE("%s: Close on exec can't be set on fd - %d\n",
        __func__, errno);
    goto xport_open_close_fd;
  }
  setsockopt(xp->fd, SOL_SOCKET, SO_RCVBUF, (char *)&sk_size, sizeof(sk_size));

  if (reader_thread_data_init(&xp->rdr_tdata, (void *)xp,
      data_msg_reader_thread) < 0)
    goto xport_open_close_fd;
  memcpy(xp->svc_addr, addr, sizeof(struct xport_ipc_router_server_addr));
  xp->is_client = 1;
  flags = fcntl(xp->fd, F_GETFL, 0);
  fcntl(xp->fd, F_SETFL, flags | O_NONBLOCK);
  if(write(xp->rdr_tdata.wakeup_pipe[1], "a", 1) < 0)
    QMI_FW_LOGE("%s: Error writing to pipe\n", __func__);
  QMI_CCI_LOGD("xport_open[%d]: max_rx_len=%d\n", xp->fd, max_rx_len);

xport_open_success:
  LOCK(&ctrl_port->xport_list_lock);
  LIST_ADD(ctrl_port->xport, xp, link);
  UNLOCK(&ctrl_port->xport_list_lock);
  return xp;
xport_open_close_fd:
  close(xp->fd);
xport_open_free_xp:
  free(xp);
  return NULL;
}


static qmi_client_error_type xport_send
(
 void *handle,
 void *addr,
 uint8_t *buf,
 uint32_t len
 )
{
  struct xport_handle *xp = (struct xport_handle *)handle;
  struct sockaddr_qrtr dest_addr;
  struct xport_ipc_router_server_addr *s_addr = (struct xport_ipc_router_server_addr *)addr;
  int send_ret_val;

  if (!s_addr)
  {
    QMI_FW_LOGEC("%s: Invalid address parameter\n", __func__);
    return QMI_CLIENT_TRANSPORT_ERR;
  }

  dest_addr.sq_family = AF_QIPCRTR;
  dest_addr.sq_node = s_addr->node_id;
  dest_addr.sq_port = s_addr->port_id;
  send_ret_val = sendto(xp->fd, buf, len, MSG_DONTWAIT, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_qrtr));
  if ((send_ret_val < 0) && (errno == EAGAIN))
  {
    QMI_FW_LOGEC("%s: Remote port %08x:%08x is busy for FD - %d\n",
                 __func__, s_addr->node_id, s_addr->port_id, xp->fd);
    return QMI_XPORT_BUSY_ERR;
  }
  else if ((send_ret_val < 0) && (errno == ENODEV || errno == EHOSTUNREACH))
  {
    QMI_FW_LOGEC("%s: sendto failed errno = [%d]\n", __func__, errno);
    return QMI_SERVICE_ERR;
  }
  else if(send_ret_val < 0)
  {
    QMI_FW_LOGEC("%s: Sendto failed for port %d error %d \n", __func__, ntohs(s_addr->port_id), errno);
    return QMI_CLIENT_TRANSPORT_ERR;
  }
  QMI_CCI_LOGD("Sent[%d]: %d bytes to port %d\n", xp->fd, len, ntohs(s_addr->port_id));
  return QMI_NO_ERR;
}


static void xport_close(void *handle)
{
  struct xport_handle *xp = (struct xport_handle *)handle;

  if(!xp)
  {
    QMI_FW_LOGE("%s: Invalid Handle %p\n", __func__, xp);
    return;
  }
  if (xp->is_client)
  {
    if(write(xp->rdr_tdata.wakeup_pipe[1], "d", 1) < 0)
      QMI_FW_LOGE("%s: Error writing to pipe\n", __func__);
  }
  else
  {
    QMI_CCI_LOGD("%s: It is notifier port no need to exit the control thread\n", __func__);
    release_xp(xp);
  }
}

static uint32_t xport_lookup
(
 void *xport_data,
 uint8_t xport_num,
 uint32_t service_id,
 uint32_t version,
 uint32_t *num_entries,
 qmi_cci_service_info *service_info
)
{
	struct xport_ipc_router_server_addr addr;
	uint32_t num_entries_to_fill = 0;
	uint32_t num_entries_filled = 0;
	struct qrtr_ctrl_pkt pkt = {0};
	int i = 0;
	int len;

	QMI_CCI_LOGD("Lookup: type=%d instance=%d\n", service_id, version);
	if (num_entries) {
		num_entries_to_fill = *num_entries;
		*num_entries = 0;
	}

	if (open_lookup_sock_fd() < 0)
		return 0;

	if(send_lookup_cmd(lookup_sock_fd, service_id, 0) < 0)
		return 0;

	while ((len = recv(lookup_sock_fd, &pkt, sizeof(pkt), 0)) > 0) {
		unsigned int type = le32_to_cpu(pkt.cmd);

		if (len < sizeof(pkt) || type != QRTR_TYPE_NEW_SERVER) {
			QMI_FW_LOGEC("%s: invalid/short packet\n", __func__);
			continue;
		}

		if (!pkt.server.service && !pkt.server.instance &&
		    !pkt.server.node && !pkt.server.port)
			break;

		addr.service = le32_to_cpu(pkt.server.service);
		addr.instance = le32_to_cpu(pkt.server.instance);
		addr.node_id = le32_to_cpu(pkt.server.node);
		addr.port_id = le32_to_cpu(pkt.server.port);

		if (service_info && (i < num_entries_to_fill)) {
			service_info[i].xport = xport_num;
			service_info[i].version = GET_VERSION(pkt.server.instance);
			service_info[i].instance = GET_INSTANCE(pkt.server.instance);
			service_info[i].reserved = 0;
			memcpy(&service_info[i].addr, &addr, sizeof(struct xport_ipc_router_server_addr));
			num_entries_filled++;
		}

		i++;
	}

	if (len < 0) {
		QMI_FW_LOGEC("%s: No RX for lookup %d\n", __func__, len);
		return 0;
	}

	if (num_entries)
		  *num_entries = num_entries_filled;

	close_lookup_sock_fd();
	return i;
}

static uint32_t xport_addr_len(void)
{
  return sizeof(struct xport_ipc_router_server_addr);
}

qmi_cci_xport_ops_type qcci_qrtr_ops =
{
  xport_open,
  xport_send,
  xport_close,
  xport_lookup,
  xport_addr_len
};
