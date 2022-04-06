/*
 **************************************************************************
 * Copyright (c) 2014-2015,2019, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/*
 * nss_nlgre_redir_family.h
 *	NSS Netlink gre_redir API definitions
 */
#ifndef __NSS_NLGRE_REDIR_FAMILY_H
#define __NSS_NLGRE_REDIR_FAMILY_H

/*
 * nss_nlgre_redir_family_init()
 * 	To initialize the gre_redir module
 */
bool nss_nlgre_redir_family_init(void);

/*
 * nss_nlgre_redir_family_exit()
 * 	Exit the gre_redir module
 */
bool nss_nlgre_redir_family_exit(void);

#if defined(CONFIG_NSS_NLGRE_REDIR_FAMILY)
#define NSS_NLGRE_REDIR_FAMILY_INIT nss_nlgre_redir_family_init
#define NSS_NLGRE_REDIR_FAMILY_EXIT nss_nlgre_redir_family_exit
#else
#define NSS_NLGRE_REDIR_FAMILY_INIT 0
#define NSS_NLGRE_REDIR_FAMILY_EXIT 0
#endif /* !CONFIG_NSS_NLGRE_REDIR_FAMILY */

#endif /* __NSS_NLGRE_REDIR_FAMILY_H */
