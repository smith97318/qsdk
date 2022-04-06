/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
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


/**
 * @defgroup
 * @{
 */
#ifndef _APPE_GENEVE_H_
#define _APPE_GENEVE_H_

sw_error_t
appe_tpr_geneve_cfg_get(
                a_uint32_t dev_id,
                union tpr_geneve_cfg_u *value);

sw_error_t
appe_tpr_geneve_cfg_set(
                a_uint32_t dev_id,
                union tpr_geneve_cfg_u *value);

sw_error_t
appe_tpr_geneve_cfg_udp_port_map_get(
                a_uint32_t dev_id,
                unsigned int *value);

sw_error_t
appe_tpr_geneve_cfg_udp_port_map_set(
                a_uint32_t dev_id,
                unsigned int value);

#endif
