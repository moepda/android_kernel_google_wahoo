/*
 * Copyright (c) 2012-2018 The Linux Foundation. All rights reserved.
 *
 * Previously licensed under the ISC license by Qualcomm Atheros, Inc.
 *
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * This file was originally distributed by Qualcomm Atheros, Inc.
 * under proprietary terms before Copyright ownership was assigned
 * to the Linux Foundation.
 */

/******************************************************************************
* wlan_ptt_sock_svc.c
*
******************************************************************************/
#ifdef PTT_SOCK_SVC_ENABLE
#include <wlan_nlink_srv.h>
#include <qdf_types.h>
#include <qdf_status.h>
#include <qdf_trace.h>
#include <wlan_nlink_common.h>
#include <wlan_ptt_sock_svc.h>
#include <qdf_types.h>
#include <qdf_trace.h>
#include "wlan_hdd_main.h"

#ifdef CNSS_GENL
#include <net/cnss_nl.h>
#else

/** ptt Process ID */
static int32_t ptt_pid = INVALID_PID;
#endif

#define PTT_SOCK_DEBUG
#ifdef PTT_SOCK_DEBUG
#define PTT_TRACE(level, args ...) QDF_TRACE(QDF_MODULE_ID_QDF, level, ## args)
#else
#define PTT_TRACE(level, args ...)
#endif

#ifdef PTT_SOCK_DEBUG_VERBOSE
/* Utility function to perform a hex dump */
static void ptt_sock_dump_buf(const unsigned char *pbuf, int cnt)
{
	int i;

	for (i = 0; i < cnt; i++) {
		if ((i % 16) == 0)
			QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_INFO,
				  "\n%pK:", pbuf);
		QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_INFO, " %02X",
			  *pbuf);
		pbuf++;
	}
	QDF_TRACE(QDF_MODULE_ID_QDF, QDF_TRACE_LEVEL_INFO, "\n");
}
#endif

/**
 * nl_srv_ucast_ptt() - Wrapper function to send ucast msgs to PTT
 * @skb: sk buffer pointer
 * @dst_pid: Destination PID
 * @flag: flags
 *
 * Sends the ucast message to PTT with generic nl socket if CNSS_GENL
 * is enabled. Else, use the legacy netlink socket to send.
 *
 * Return: zero on success, error code otherwise
 */
static int nl_srv_ucast_ptt(struct sk_buff *skb, int dst_pid, int flag)
{
#ifdef CNSS_GENL
	return nl_srv_ucast(skb, dst_pid, flag, ANI_NL_MSG_PUMAC,
				CLD80211_MCGRP_DIAG_EVENTS);
#else
	return nl_srv_ucast(skb, dst_pid, flag);
#endif
}

/**
 * nl_srv_bcast_ptt() - Wrapper function to send bcast msgs to DIAG mcast group
 * @skb: sk buffer pointer
 *
 * Sends the bcast message to DIAG multicast group with generic nl socket
 * if CNSS_GENL is enabled. Else, use the legacy netlink socket to send.
 *
 * Return: zero on success, error code otherwise
 */
static int nl_srv_bcast_ptt(struct sk_buff *skb)
{
#ifdef CNSS_GENL
	return nl_srv_bcast(skb, CLD80211_MCGRP_DIAG_EVENTS, ANI_NL_MSG_PUMAC);
#else
	return nl_srv_bcast(skb);
#endif
}

/**
 * ptt_sock_send_msg_to_app() - Send nl message to user space
 * wmsg: Message header
 * radio: Unit number of the radio
 * src_mod: Message type
 * pid: Process ID to which message will be unicast. Message
 * will be broadcast when PID is INVALID_PID
 *
 * Utility function to send a netlink message to an application in user space
 *
 * Return: 0 on success and negative value on failure
 */
int ptt_sock_send_msg_to_app(tAniHdr *wmsg, int radio, int src_mod, int pid)
{
	int err = -1;
	int payload_len;
	int tot_msg_len;
	tAniNlHdr *wnl;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int wmsg_length = be16_to_cpu(wmsg->length);
	static int nlmsg_seq;

	if (radio < 0 || radio > ANI_MAX_RADIOS) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "%s: invalid radio id [%d]\n",
			  __func__, radio);
		return -EINVAL;
	}
	payload_len = wmsg_length + sizeof(wnl->radio);
	tot_msg_len = NLMSG_SPACE(payload_len);
	skb = dev_alloc_skb(tot_msg_len);
	if (skb  == NULL) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR,
			  "%s: dev_alloc_skb() failed for msg size[%d]\n",
			  __func__, tot_msg_len);
		return -ENOMEM;
	}
	nlh =
		nlmsg_put(skb, pid, nlmsg_seq++, src_mod, payload_len,
			  NLM_F_REQUEST);
	if (NULL == nlh) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR,
			  "%s: nlmsg_put() failed for msg size[%d]\n", __func__,
			  tot_msg_len);
		kfree_skb(skb);
		return -ENOMEM;
	}
	wnl = (tAniNlHdr *) nlh;
	wnl->radio = radio;
	memcpy(&wnl->wmsg, wmsg, wmsg_length);
#ifdef PTT_SOCK_DEBUG_VERBOSE
	ptt_sock_dump_buf((const unsigned char *)skb->data, skb->len);
#endif

	if (pid != INVALID_PID)
		err = nl_srv_ucast_ptt(skb, pid, MSG_DONTWAIT);
	else
		err = nl_srv_bcast_ptt(skb);

	if (err)
		PTT_TRACE(QDF_TRACE_LEVEL_INFO,
			  "%s:Failed sending Msg Type [0x%X] to pid[%d]\n",
			  __func__, be16_to_cpu(wmsg->type), pid);
	return err;
}

#ifndef CNSS_GENL
/*
 * Process tregisteration request and send registration response messages
 * to the PTT Socket App in user space
 */
static void ptt_sock_proc_reg_req(tAniHdr *wmsg, int radio)
{
	struct sAniAppRegReq *reg_req;
	struct sAniNlAppRegRsp rspmsg;

	reg_req = (struct sAniAppRegReq *) (wmsg + 1);
	memset((char *)&rspmsg, 0, sizeof(rspmsg));
	/* send reg response message to the application */
	rspmsg.ret = ANI_NL_MSG_OK;
	rspmsg.regReq.type = reg_req->type;
	/*Save the pid */
	ptt_pid = reg_req->pid;
	rspmsg.regReq.pid = reg_req->pid;
	rspmsg.wniHdr.type = cpu_to_be16(ANI_MSG_APP_REG_RSP);
	rspmsg.wniHdr.length = cpu_to_be16(sizeof(rspmsg));
	if (ptt_sock_send_msg_to_app((tAniHdr *) &rspmsg.wniHdr, radio,
				     ANI_NL_MSG_PUMAC, ptt_pid) < 0) {
		PTT_TRACE(QDF_TRACE_LEVEL_INFO,
			  "%s: Error sending ANI_MSG_APP_REG_RSP to pid[%d]\n",
			  __func__, ptt_pid);
	}
}

/*
 * Process all the messages from the PTT Socket App in user space
 */
static void ptt_proc_pumac_msg(struct sk_buff *skb, tAniHdr *wmsg, int radio)
{
	u16 ani_msg_type = be16_to_cpu(wmsg->type);

	switch (ani_msg_type) {
	case ANI_MSG_APP_REG_REQ:
		PTT_TRACE(QDF_TRACE_LEVEL_INFO,
			  "%s: Received ANI_MSG_APP_REG_REQ [0x%X]\n", __func__,
			  ani_msg_type);
		ptt_sock_proc_reg_req(wmsg, radio);
		break;
	default:
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR,
			  "%s: Received Unknown Msg Type[0x%X]\n", __func__,
			  ani_msg_type);
		break;
	}
}

/*
 * Process all the Netlink messages from PTT Socket app in user space
 */
static int ptt_sock_rx_nlink_msg(struct sk_buff *skb)
{
	tAniNlHdr *wnl;
	int radio;
	int type;

	wnl = (tAniNlHdr *) skb->data;
	radio = wnl->radio;
	type = wnl->nlh.nlmsg_type;
	switch (type) {
	case ANI_NL_MSG_PUMAC:  /* Message from the PTT socket APP */
		PTT_TRACE(QDF_TRACE_LEVEL_INFO,
			  "%s: Received ANI_NL_MSG_PUMAC Msg [0x%X]\n",
			  __func__, type);
		ptt_proc_pumac_msg(skb, &wnl->wmsg, radio);
		break;
	default:
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "%s: Unknown NL Msg [0x%X]\n",
			  __func__, type);
		break;
	}
	return 0;
}
#endif

#ifdef CNSS_GENL
/**
 * ptt_cmd_handler() - Handler function for PTT commands
 * @data: Data to be parsed
 * @data_len: Length of the data received
 * @ctx: Registered context reference
 * @pid: Process id of the user space application
 *
 * This function handles the command from PTT user space application
 *
 * Return: None
 */
static void ptt_cmd_handler(const void *data, int data_len, void *ctx, int pid)
{
	uint16_t length;
	struct sptt_app_reg_req *payload;
	struct nlattr *tb[CLD80211_ATTR_MAX + 1];

	/*
	 * audit note: it is ok to pass a NULL policy here since a
	 * length check on the data is added later already
	 */
	if (hdd_nla_parse(tb, CLD80211_ATTR_MAX, data, data_len, NULL)) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "Invalid ATTR");
		return;
	}

	if (!tb[CLD80211_ATTR_DATA]) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "attr ATTR_DATA failed");
		return;
	}

	if (nla_len(tb[CLD80211_ATTR_DATA]) < sizeof(struct sptt_app_reg_req)) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "%s:attr length check fails\n",
			__func__);
		return;
	}

	payload = (struct sptt_app_reg_req *)(nla_data(tb[CLD80211_ATTR_DATA]));
	length = be16_to_cpu(payload->wmsg.length);

	if (nla_len(tb[CLD80211_ATTR_DATA]) <  (length +
						sizeof(payload->radio))) {
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "ATTR_DATA len check failed");
		return;
	}

	switch (payload->wmsg.type) {
	case ANI_MSG_APP_REG_REQ:
		ptt_sock_send_msg_to_app(&payload->wmsg, payload->radio,
							ANI_NL_MSG_PUMAC, pid);
		break;
	default:
		PTT_TRACE(QDF_TRACE_LEVEL_ERROR, "Unknown msg type %d",
							payload->wmsg.type);
		break;
	}
}

/**
 * ptt_sock_activate_svc() - API to register PTT/PUMAC command handler
 *
 * API to register the PTT/PUMAC command handlers. Argument @pAdapter
 * is sent for prototype compatibility between new genl and legacy
 * implementation
 *
 * Return: 0
 */
int ptt_sock_activate_svc(void)
{
	register_cld_cmd_cb(ANI_NL_MSG_PUMAC, ptt_cmd_handler, NULL);
	register_cld_cmd_cb(ANI_NL_MSG_PTT, ptt_cmd_handler, NULL);
	return 0;
}

/**
 * ptt_sock_deactivate_svc() - Dummy API to deactivate PTT service
 *
 * Return: Void
 */
void ptt_sock_deactivate_svc(void)
{
}
#else

/**
 * ptt_sock_activate_svc() - activate PTT service
 *
 * Return: 0
 */
int ptt_sock_activate_svc(void)
{
	ptt_pid = INVALID_PID;
	nl_srv_register(ANI_NL_MSG_PUMAC, ptt_sock_rx_nlink_msg);
	nl_srv_register(ANI_NL_MSG_PTT, ptt_sock_rx_nlink_msg);
	return 0;
}

/**
 * ptt_sock_deactivate_svc() - deactivate PTT service
 *
 * Return: Void
 */
void ptt_sock_deactivate_svc(void)
{
	ptt_pid = INVALID_PID;
}
#endif
#endif /* PTT_SOCK_SVC_ENABLE */
