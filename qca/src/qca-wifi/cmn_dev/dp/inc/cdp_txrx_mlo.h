/*
 * Copyright (c) 2021 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _CDP_TXRX_MLO_H_
#define _CDP_TXRX_MLO_H_
#include "cdp_txrx_ops.h"

struct cdp_mlo_ctxt;

/**
 * cdp_ctrl_mlo_mgr - opaque handle for mlo manager context
 */
struct cdp_ctrl_mlo_mgr;

struct
cdp_mlo_ctxt *dp_mlo_ctxt_attach_wifi3(struct cdp_ctrl_mlo_mgr *ctrl_ctxt);
void dp_mlo_ctxt_detach_wifi3(struct cdp_mlo_ctxt *ml_ctxt);

static inline
struct cdp_mlo_ctxt *cdp_mlo_ctxt_attach(struct cdp_ctrl_mlo_mgr *ctrl_ctxt)
{
	return dp_mlo_ctxt_attach_wifi3(ctrl_ctxt);
}

static inline
void cdp_mlo_ctxt_detach(struct cdp_mlo_ctxt *ml_ctxt)
{
	dp_mlo_ctxt_detach_wifi3(ml_ctxt);
}

static inline void cdp_soc_mlo_soc_setup(ol_txrx_soc_handle soc,
					 struct cdp_mlo_ctxt *mlo_ctx)
{
	if (!soc || !soc->ops) {
		QDF_BUG(0);
		return;
	}

	if (!soc->ops->mlo_ops ||
	    !soc->ops->mlo_ops->mlo_soc_setup)
		return;

	soc->ops->mlo_ops->mlo_soc_setup(soc, mlo_ctx);
}

static inline void cdp_soc_mlo_soc_teardown(ol_txrx_soc_handle soc,
					    struct cdp_mlo_ctxt *mlo_ctx)
{
	if (!soc || !soc->ops) {
		QDF_BUG(0);
		return;
	}

	if (!soc->ops->mlo_ops ||
	    !soc->ops->mlo_ops->mlo_soc_teardown)
		return;

	soc->ops->mlo_ops->mlo_soc_teardown(soc, mlo_ctx);
}

/*
 * cdp_update_mlo_ptnr_list - Add vdev to MLO partner list
 * @soc: soc handle
 * @vdev_ids: list of partner vdevs
 * @num_vdevs: number of items in list
 * @vdev_id: caller's vdev id
 *
 * return: QDF_STATUS
 */
static inline QDF_STATUS
cdp_update_mlo_ptnr_list(ol_txrx_soc_handle soc, int8_t vdev_ids[],
			 uint8_t num_vdevs, uint8_t vdev_id)
{
	if (!soc || !soc->ops || !soc->ops->mlo_ops)
		return QDF_STATUS_E_INVAL;

	if (soc->ops->mlo_ops->update_mlo_ptnr_list)
		return soc->ops->mlo_ops->update_mlo_ptnr_list(soc, vdev_ids,
						num_vdevs, vdev_id);

	return QDF_STATUS_SUCCESS;
}

static inline void cdp_mlo_setup_complete(ol_txrx_soc_handle soc,
					  struct cdp_mlo_ctxt *mlo_ctx)
{
	if (!soc || !soc->ops) {
		QDF_BUG(0);
		return;
	}

	if (!soc->ops->mlo_ops ||
	    !soc->ops->mlo_ops->mlo_setup_complete)
		return;

	soc->ops->mlo_ops->mlo_setup_complete(mlo_ctx);
}
#endif /*_CDP_TXRX_MLO_H_*/
