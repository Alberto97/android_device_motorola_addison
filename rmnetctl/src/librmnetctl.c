/******************************************************************************

			L I B R M N E T C T L . C

Copyright (c) 2013-2015, 2017-2018 The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above
	  copyright notice, this list of conditions and the following
	  disclaimer in the documentation and/or other materials provided
	  with the distribution.
	* Neither the name of The Linux Foundation nor the names of its
	  contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/

/*!
* @file    librmnetctl.c
* @brief   rmnet control API's implementation file
*/

/*===========================================================================
			INCLUDE FILES
===========================================================================*/

#include <sys/socket.h>
#include <stdint.h>
#include <linux/netlink.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <linux/gen_stats.h>
#include <net/if.h>
#include <asm/types.h>
#include <linux/rmnet_data.h>
#include "librmnetctl_hndl.h"
#include "librmnetctl.h"

#ifdef USE_GLIB
#include <glib.h>
#define strlcpy g_strlcpy
#endif

#define RMNETCTL_SOCK_FLAG 0
#define ROOT_USER_ID 0
#define MIN_VALID_PROCESS_ID 0
#define MIN_VALID_SOCKET_FD 0
#define KERNEL_PROCESS_ID 0
#define UNICAST 0
#define MAX_BUF_SIZE sizeof(struct nlmsghdr) + sizeof(struct rmnet_nl_msg_s)
#define INGRESS_FLAGS_MASK   (RMNET_INGRESS_FIX_ETHERNET | \
			      RMNET_INGRESS_FORMAT_MAP | \
			      RMNET_INGRESS_FORMAT_DEAGGREGATION | \
			      RMNET_INGRESS_FORMAT_DEMUXING | \
			      RMNET_INGRESS_FORMAT_MAP_COMMANDS | \
			      RMNET_INGRESS_FORMAT_MAP_CKSUMV3 | \
			      RMNET_INGRESS_FORMAT_MAP_CKSUMV4)
#define EGRESS_FLAGS_MASK    (RMNET_EGRESS_FORMAT__RESERVED__ | \
			      RMNET_EGRESS_FORMAT_MAP | \
			      RMNET_EGRESS_FORMAT_AGGREGATION | \
			      RMNET_EGRESS_FORMAT_MUXING | \
			      RMNET_EGRESS_FORMAT_MAP_CKSUMV3 | \
			      RMNET_EGRESS_FORMAT_MAP_CKSUMV4)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define NLMSG_TAIL(nmsg) \
    ((struct rtattr *) (((char *)(nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

#define NLMSG_DATA_SIZE  500

#define CHECK_MEMSCPY(x) ({if (x < 0 ){return RMNETCTL_LIB_ERR;}})

struct nlmsg {
	struct nlmsghdr nl_addr;
	struct ifinfomsg ifmsg;
	char data[NLMSG_DATA_SIZE];
};

#define RMNETCTL_NUM_TX_QUEUES 10

/* This needs to be hardcoded here because some legacy linux systems
 * do not have this definition
 */
#define RMNET_IFLA_NUM_TX_QUEUES 31

/*===========================================================================
			LOCAL FUNCTION DEFINITIONS
===========================================================================*/
/* Helper functions
 * @brief helper function to implement a secure memcpy
 * @details take source and destination buffer size into
 *          considerations before copying
 * @param dst destination buffer
 * @param dst_size size of destination buffer
 * @param src source buffer
 * @param src_size size of source buffer
 * @return size of the smallest of two buffer
 */

static inline size_t memscpy(void *dst, size_t dst_size, const void *src,
			     size_t src_size) {
	size_t min_size = dst_size < src_size ? dst_size : src_size;
	memcpy(dst, src, min_size);
	return min_size;
}

/*
* @brief helper function to implement a secure memcpy
 * for a concatenating buffer where offset is kept
 * track of
 * @details take source and destination buffer size into
 *          considerations before copying
 * @param dst destination buffer
 * @param dst_size pointer used to decrement
 * @param src source buffer
 * @param src_size size of source buffer
 * @return size of the remaining buffer
 */


static inline int  memscpy_repeat(void* dst, size_t *dst_size,
	const void* src, size_t src_size)
{
	if( !dst_size || *dst_size <= src_size || !dst || !src)
		return RMNETCTL_LIB_ERR;
	else {
		*dst_size -= memscpy(dst, *dst_size, src, src_size);
	}
	return *dst_size;
}


/*!
* @brief Synchronous method to send and receive messages to and from the kernel
* using  netlink sockets
* @details Increments the transaction id for each message sent to the kernel.
* Sends the netlink message to the kernel and receives the response from the
* kernel.
* @param *hndl RmNet handle for this transaction
* @param request Message to be sent to the kernel
* @param response Message received from the kernel
* @return RMNETCTL_API_SUCCESS if successfully able to send and receive message
* from the kernel
* @return RMNETCTL_API_ERR_HNDL_INVALID if RmNet handle for the transaction was
* NULL
* @return RMNETCTL_API_ERR_REQUEST_NULL not enough memory to create buffer for
* sending the message
* @return RMNETCTL_API_ERR_MESSAGE_SEND if could not send the message to kernel
* @return RMNETCTL_API_ERR_MESSAGE_RECEIVE if could not receive message from the
* kernel
* @return RMNETCTL_API_ERR_MESSAGE_TYPE if the request and response type do not
* match
*/
static uint16_t rmnetctl_transact(rmnetctl_hndl_t *hndl,
			struct rmnet_nl_msg_s *request,
			struct rmnet_nl_msg_s *response) {
	uint8_t *request_buf, *response_buf;
	struct nlmsghdr *nlmsghdr_val;
	struct rmnet_nl_msg_s *rmnet_nl_msg_s_val;
	ssize_t bytes_read = -1, buffsize = MAX_BUF_SIZE - sizeof(struct nlmsghdr);
	uint16_t return_code = RMNETCTL_API_ERR_HNDL_INVALID;
	struct sockaddr_nl* __attribute__((__may_alias__)) saddr_ptr;
	request_buf = NULL;
	response_buf = NULL;
	nlmsghdr_val = NULL;
	rmnet_nl_msg_s_val = NULL;
	do {
	if (!hndl){
		break;
	}
	if (!request){
		return_code = RMNETCTL_API_ERR_REQUEST_NULL;
		break;
	}
	if (!response){
		return_code = RMNETCTL_API_ERR_RESPONSE_NULL;
		break;
	}
	request_buf = (uint8_t *)malloc(MAX_BUF_SIZE * sizeof(uint8_t));
	if (!request_buf){
		return_code = RMNETCTL_API_ERR_REQUEST_NULL;
		break;
	}

	response_buf = (uint8_t *)malloc(MAX_BUF_SIZE * sizeof(uint8_t));
	if (!response_buf) {
		return_code = RMNETCTL_API_ERR_RESPONSE_NULL;
		break;
	}

	nlmsghdr_val = (struct nlmsghdr *)request_buf;
	rmnet_nl_msg_s_val = (struct rmnet_nl_msg_s *)NLMSG_DATA(request_buf);

	memset(request_buf, 0, MAX_BUF_SIZE*sizeof(uint8_t));
	memset(response_buf, 0, MAX_BUF_SIZE*sizeof(uint8_t));

	nlmsghdr_val->nlmsg_seq = hndl->transaction_id;
	nlmsghdr_val->nlmsg_pid = hndl->pid;
	nlmsghdr_val->nlmsg_len = MAX_BUF_SIZE;

	memscpy((void *)NLMSG_DATA(request_buf), buffsize, request,
	sizeof(struct rmnet_nl_msg_s));

	rmnet_nl_msg_s_val->crd = RMNET_NETLINK_MSG_COMMAND;
	hndl->transaction_id++;

	saddr_ptr = &hndl->dest_addr;
	socklen_t addrlen = sizeof(struct sockaddr_nl);
	if (sendto(hndl->netlink_fd,
			request_buf,
			MAX_BUF_SIZE,
			RMNETCTL_SOCK_FLAG,
			(struct sockaddr*)saddr_ptr,
			sizeof(struct sockaddr_nl)) < 0) {
		return_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		break;
	}

	saddr_ptr = &hndl->src_addr;
	bytes_read = recvfrom(hndl->netlink_fd,
			response_buf,
			MAX_BUF_SIZE,
			RMNETCTL_SOCK_FLAG,
			(struct sockaddr*)saddr_ptr,
			&addrlen);
	if (bytes_read < 0) {
		return_code = RMNETCTL_API_ERR_MESSAGE_RECEIVE;
		break;
	}
	buffsize = MAX_BUF_SIZE - sizeof(struct nlmsghdr);
	memscpy(response, buffsize, (void *)NLMSG_DATA(response_buf),
	sizeof(struct rmnet_nl_msg_s));
	if (sizeof(*response) < sizeof(struct rmnet_nl_msg_s)) {
		return_code = RMNETCTL_API_ERR_RESPONSE_NULL;
		break;
	}

	if (request->message_type != response->message_type) {
		return_code = RMNETCTL_API_ERR_MESSAGE_TYPE;
		break;
	}
	return_code = RMNETCTL_SUCCESS;
	free(request_buf);
	free(response_buf);
	} while(0);
	return return_code;
}

/*!
* @brief Static function to check the dev name
* @details Checks if the name is not NULL and if the name is less than the
* RMNET_MAX_STR_LEN
* @param dev_name Name of the device
* @return RMNETCTL_SUCCESS if successful
* @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
*/
static inline int _rmnetctl_check_dev_name(const char *dev_name) {
	int return_code = RMNETCTL_INVALID_ARG;
	do {
	if (!dev_name)
		break;
	if (strlen(dev_name) >= RMNET_MAX_STR_LEN)
		break;
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

/*!
* @brief Static function to check the string length after a copy
* @details Checks if the string length is not lesser than zero and lesser than
* RMNET_MAX_STR_LEN
* @param str_len length of the string after a copy
* @param error_code Status code of this operation
* @return RMNETCTL_SUCCESS if successful
* @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
*/
static inline int _rmnetctl_check_len(size_t str_len, uint16_t *error_code) {
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if (str_len > RMNET_MAX_STR_LEN) {
		*error_code = RMNETCTL_API_ERR_STRING_TRUNCATION;
		break;
	}
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

/*!
* @brief Static function to check the response type
* @details Checks if the response type of this message was return code
* @param crd The crd field passed
* @param error_code Status code of this operation
* @return RMNETCTL_SUCCESS if successful
* @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
*/
static inline int _rmnetctl_check_code(int crd, uint16_t *error_code) {
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if (crd != RMNET_NETLINK_MSG_RETURNCODE) {
		*error_code = RMNETCTL_API_ERR_RETURN_TYPE;
		break;
	}
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

/*!
* @brief Static function to check the response type
* @details Checks if the response type of this message was data
* @param crd The crd field passed
* @param error_code Status code of this operation
* @return RMNETCTL_SUCCESS if successful
* @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
*/
static inline int _rmnetctl_check_data(int crd, uint16_t *error_code) {
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if (crd != RMNET_NETLINK_MSG_RETURNDATA) {
		*error_code = RMNETCTL_API_ERR_RETURN_TYPE;
		break;
	}
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

/*!
* @brief Static function to set the return value
* @details Checks if the error_code from the transaction is zero for a return
* code type message and sets the message type as RMNETCTL_SUCCESS
* @param crd The crd field passed
* @param error_code Status code of this operation
* @return RMNETCTL_SUCCESS if successful
* @return RMNETCTL_KERNEL_ERR if there was an error in the kernel.
* Check error_code
*/
static inline int _rmnetctl_set_codes(int error_val, uint16_t *error_code) {
	int return_code = RMNETCTL_KERNEL_ERR;
	if (error_val == RMNET_CONFIG_OK)
		return_code = RMNETCTL_SUCCESS;
	else
		*error_code = (uint16_t)error_val + RMNETCTL_KERNEL_FIRST_ERR;
	return return_code;
}

/*===========================================================================
				EXPOSED API
===========================================================================*/

int rmnetctl_init(rmnetctl_hndl_t **hndl, uint16_t *error_code)
{
	pid_t pid = 0;
	int netlink_fd = -1, return_code = RMNETCTL_LIB_ERR;
	struct sockaddr_nl* __attribute__((__may_alias__)) saddr_ptr;
	do {
	if ((!hndl) || (!error_code)){
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	*hndl = (rmnetctl_hndl_t *)malloc(sizeof(rmnetctl_hndl_t));
	if (!*hndl) {
		*error_code = RMNETCTL_API_ERR_HNDL_INVALID;
		break;
	}

	memset(*hndl, 0, sizeof(rmnetctl_hndl_t));

	pid = getpid();
	if (pid  < MIN_VALID_PROCESS_ID) {
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_PROCESS_ID;
		break;
	}
	(*hndl)->pid = (uint32_t)pid;
	netlink_fd = socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, RMNET_NETLINK_PROTO);
	if (netlink_fd < MIN_VALID_SOCKET_FD) {
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_NETLINK_FD;
		break;
	}

	(*hndl)->netlink_fd = netlink_fd;

	memset(&(*hndl)->src_addr, 0, sizeof(struct sockaddr_nl));

	(*hndl)->src_addr.nl_family = AF_NETLINK;
	(*hndl)->src_addr.nl_pid = (*hndl)->pid;

	saddr_ptr = &(*hndl)->src_addr;
	if (bind((*hndl)->netlink_fd,
		(struct sockaddr*)saddr_ptr,
		sizeof(struct sockaddr_nl)) < 0) {
		close((*hndl)->netlink_fd);
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_BIND;
		break;
	}

	memset(&(*hndl)->dest_addr, 0, sizeof(struct sockaddr_nl));

	(*hndl)->dest_addr.nl_family = AF_NETLINK;
	(*hndl)->dest_addr.nl_pid = KERNEL_PROCESS_ID;
	(*hndl)->dest_addr.nl_groups = UNICAST;

	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

void rmnetctl_cleanup(rmnetctl_hndl_t *hndl)
{
	if (!hndl)
		return;
	close(hndl->netlink_fd);
	free(hndl);
}

int rmnet_associate_network_device(rmnetctl_hndl_t *hndl,
				   const char *dev_name,
				   uint16_t *error_code,
				   uint8_t assoc_dev)
{
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!error_code) || _rmnetctl_check_dev_name(dev_name) ||
		((assoc_dev != RMNETCTL_DEVICE_ASSOCIATE) &&
		(assoc_dev != RMNETCTL_DEVICE_UNASSOCIATE))) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	if (assoc_dev == RMNETCTL_DEVICE_ASSOCIATE)
		request.message_type = RMNET_NETLINK_ASSOCIATE_NETWORK_DEVICE;
	else
		request.message_type = RMNET_NETLINK_UNASSOCIATE_NETWORK_DEVICE;

	request.arg_length = RMNET_MAX_STR_LEN;
	str_len = strlcpy((char *)(request.data), dev_name, (size_t)RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;
	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;
	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

int rmnet_get_network_device_associated(rmnetctl_hndl_t *hndl,
					const char *dev_name,
					int *register_status,
					uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int  return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!register_status) || (!error_code) ||
	_rmnetctl_check_dev_name(dev_name)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_GET_NETWORK_DEVICE_ASSOCIATED;

	request.arg_length = RMNET_MAX_STR_LEN;
	str_len = strlcpy((char *)(request.data), dev_name, RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_data(response.crd, error_code)
		!= RMNETCTL_SUCCESS) {
		if (_rmnetctl_check_code(response.crd, error_code)
			== RMNETCTL_SUCCESS)
			return_code = _rmnetctl_set_codes(response.return_code,
							  error_code);
		break;
	}

	*register_status = response.return_code;
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

int rmnet_set_link_egress_data_format(rmnetctl_hndl_t *hndl,
				      uint32_t egress_flags,
				      uint16_t agg_size,
				      uint16_t agg_count,
				      const char *dev_name,
				      uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int  return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!error_code) || _rmnetctl_check_dev_name(dev_name) ||
	    ((~EGRESS_FLAGS_MASK) & egress_flags)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_SET_LINK_EGRESS_DATA_FORMAT;

	request.arg_length = RMNET_MAX_STR_LEN +
			 sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
	str_len = strlcpy((char *)(request.data_format.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	request.data_format.flags = egress_flags;
	request.data_format.agg_size = agg_size;
	request.data_format.agg_count = agg_count;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;

	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

int rmnet_get_link_egress_data_format(rmnetctl_hndl_t *hndl,
				      const char *dev_name,
				      uint32_t *egress_flags,
				      uint16_t *agg_size,
				      uint16_t *agg_count,
				      uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int  return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!egress_flags) || (!agg_size) || (!agg_count) ||
	(!error_code) || _rmnetctl_check_dev_name(dev_name)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}
	request.message_type = RMNET_NETLINK_GET_LINK_EGRESS_DATA_FORMAT;

	request.arg_length = RMNET_MAX_STR_LEN;
	str_len = strlcpy((char *)(request.data_format.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_data(response.crd, error_code)
		!= RMNETCTL_SUCCESS) {
		if (_rmnetctl_check_code(response.crd, error_code)
			== RMNETCTL_SUCCESS)
			return_code = _rmnetctl_set_codes(response.return_code,
							  error_code);
		break;
	}

	*egress_flags = response.data_format.flags;
	*agg_size = response.data_format.agg_size;
	*agg_count = response.data_format.agg_count;
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

int rmnet_set_link_ingress_data_format_tailspace(rmnetctl_hndl_t *hndl,
						 uint32_t ingress_flags,
						 uint8_t  tail_spacing,
						 const char *dev_name,
						 uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int  return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!error_code) || _rmnetctl_check_dev_name(dev_name) ||
	    ((~INGRESS_FLAGS_MASK) & ingress_flags)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_SET_LINK_INGRESS_DATA_FORMAT;

	request.arg_length = RMNET_MAX_STR_LEN +
	sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t);
	str_len = strlcpy((char *)(request.data_format.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;
	request.data_format.flags = ingress_flags;
	request.data_format.tail_spacing = tail_spacing;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;

	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

int rmnet_get_link_ingress_data_format_tailspace(rmnetctl_hndl_t *hndl,
						 const char *dev_name,
						 uint32_t *ingress_flags,
						 uint8_t  *tail_spacing,
						 uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int  return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!error_code) ||
		_rmnetctl_check_dev_name(dev_name)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_GET_LINK_INGRESS_DATA_FORMAT;

	request.arg_length = RMNET_MAX_STR_LEN;
	str_len = strlcpy((char *)(request.data_format.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_data(response.crd, error_code)
		!= RMNETCTL_SUCCESS) {
		if (_rmnetctl_check_code(response.crd, error_code)
			== RMNETCTL_SUCCESS)
			return_code = _rmnetctl_set_codes(response.return_code,
							  error_code);
		break;
	}

	if (ingress_flags)
		*ingress_flags = response.data_format.flags;

	if (tail_spacing)
		*tail_spacing = response.data_format.tail_spacing;

	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

int rmnet_set_logical_ep_config(rmnetctl_hndl_t *hndl,
				int32_t ep_id,
				uint8_t operating_mode,
				const char *dev_name,
				const char *next_dev,
				uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || ((ep_id < -1) || (ep_id > 31)) || (!error_code) ||
		_rmnetctl_check_dev_name(dev_name) ||
		_rmnetctl_check_dev_name(next_dev) ||
		operating_mode >= RMNET_EPMODE_LENGTH) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_SET_LOGICAL_EP_CONFIG;

	request.arg_length = RMNET_MAX_STR_LEN +
	RMNET_MAX_STR_LEN + sizeof(int32_t) + sizeof(uint8_t);
	str_len = strlcpy((char *)(request.local_ep_config.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	str_len = strlcpy((char *)(request.local_ep_config.next_dev),
			  next_dev,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;
	request.local_ep_config.ep_id = ep_id;
	request.local_ep_config.operating_mode = operating_mode;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;
	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;

	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

int rmnet_unset_logical_ep_config(rmnetctl_hndl_t *hndl,
				  int32_t ep_id,
				  const char *dev_name,
				  uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int return_code = RMNETCTL_LIB_ERR;
	do {

	if ((!hndl) || ((ep_id < -1) || (ep_id > 31)) || (!error_code) ||
		_rmnetctl_check_dev_name(dev_name)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_UNSET_LOGICAL_EP_CONFIG;

	request.arg_length = RMNET_MAX_STR_LEN + sizeof(int32_t);
	str_len = strlcpy((char *)(request.local_ep_config.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);

	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	request.local_ep_config.ep_id = ep_id;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;
	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;

	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);

	return return_code;
}

int rmnet_get_logical_ep_config(rmnetctl_hndl_t *hndl,
				int32_t ep_id,
				const char *dev_name,
				uint8_t *operating_mode,
				char **next_dev,
				uint32_t next_dev_len,
				uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	size_t str_len = 0;
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!operating_mode) || (!error_code) || ((ep_id < -1) ||
	    (ep_id > 31)) || _rmnetctl_check_dev_name(dev_name) || (!next_dev)
	    || (0 == next_dev_len)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_GET_LOGICAL_EP_CONFIG;

	request.arg_length = RMNET_MAX_STR_LEN + sizeof(int32_t);
	str_len = strlcpy((char *)(request.local_ep_config.dev),
			  dev_name,
			  RMNET_MAX_STR_LEN);
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	request.local_ep_config.ep_id = ep_id;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_data(response.crd, error_code)
		!= RMNETCTL_SUCCESS) {
		if (_rmnetctl_check_code(response.crd, error_code)
			== RMNETCTL_SUCCESS)
			return_code = _rmnetctl_set_codes(response.return_code,
							  error_code);
		break;
	}

	str_len = strlcpy(*next_dev,
			  (char *)(response.local_ep_config.next_dev),
			  min(RMNET_MAX_STR_LEN, next_dev_len));
	if (_rmnetctl_check_len(str_len, error_code) != RMNETCTL_SUCCESS)
		break;

	*operating_mode = response.local_ep_config.operating_mode;
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

int rmnet_new_vnd_prefix(rmnetctl_hndl_t *hndl,
			 uint32_t id,
			 uint16_t *error_code,
			 uint8_t new_vnd,
			 const char *prefix)
{
	struct rmnet_nl_msg_s request, response;
	int return_code = RMNETCTL_LIB_ERR;
	size_t str_len = 0;
	do {
	if ((!hndl) || (!error_code) ||
	((new_vnd != RMNETCTL_NEW_VND) && (new_vnd != RMNETCTL_FREE_VND))) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	memset(request.vnd.vnd_name, 0, RMNET_MAX_STR_LEN);
	if (new_vnd ==  RMNETCTL_NEW_VND) {
		if (prefix) {
			request.message_type =RMNET_NETLINK_NEW_VND_WITH_PREFIX;
			str_len = strlcpy((char *)request.vnd.vnd_name,
					  prefix, RMNET_MAX_STR_LEN);
			if (_rmnetctl_check_len(str_len, error_code)
						!= RMNETCTL_SUCCESS)
				break;
		} else {
			request.message_type = RMNET_NETLINK_NEW_VND;
		}
	} else {
		request.message_type = RMNET_NETLINK_FREE_VND;
	}

	request.arg_length = sizeof(uint32_t);
	request.vnd.id = id;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;
	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;

	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

int rmnet_new_vnd_name(rmnetctl_hndl_t *hndl,
			 uint32_t id,
			 uint16_t *error_code,
			 const char *prefix)
{
	struct rmnet_nl_msg_s request, response;
	int return_code = RMNETCTL_LIB_ERR;
	size_t str_len = 0;
	do {
	if ((!hndl) || (!error_code)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	memset(request.vnd.vnd_name, 0, RMNET_MAX_STR_LEN);
		if (prefix) {
			request.message_type =RMNET_NETLINK_NEW_VND_WITH_NAME;
			str_len = strlcpy((char *)request.vnd.vnd_name,
					  prefix, RMNET_MAX_STR_LEN);
			if (_rmnetctl_check_len(str_len, error_code)
						!= RMNETCTL_SUCCESS)
				break;
		} else {
			request.message_type = RMNET_NETLINK_NEW_VND;
		}

	request.arg_length = sizeof(uint32_t);
	request.vnd.id = id;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;
	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;

	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

int rmnet_new_vnd(rmnetctl_hndl_t *hndl,
		  uint32_t id,
		  uint16_t *error_code,
		  uint8_t new_vnd)
{
	return rmnet_new_vnd_prefix(hndl, id, error_code, new_vnd, 0);
}

int rmnet_get_vnd_name(rmnetctl_hndl_t *hndl,
		       uint32_t id,
		       uint16_t *error_code,
		       char *buf,
		       uint32_t buflen)
{
	struct rmnet_nl_msg_s request, response;
	uint32_t str_len;
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!error_code) || (!buf) || (0 == buflen)) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}

	request.message_type = RMNET_NETLINK_GET_VND_NAME;
	request.arg_length = sizeof(uint32_t);
	request.vnd.id = id;


	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
		!= RMNETCTL_SUCCESS)
		break;

	if (_rmnetctl_check_data(response.crd, error_code)
		!= RMNETCTL_SUCCESS) {
		if (_rmnetctl_check_code(response.crd, error_code)
			== RMNETCTL_SUCCESS)
			return_code = _rmnetctl_set_codes(response.return_code,
							  error_code);
		break;
	}

	str_len = (uint32_t)strlcpy(buf,
			  (char *)(response.vnd.vnd_name),
			  buflen);
	if (str_len >= buflen) {
		*error_code = RMNETCTL_API_ERR_STRING_TRUNCATION;
		break;
	}

	return_code = RMNETCTL_SUCCESS;
	} while (0);
	return return_code;
}

int rmnet_add_del_vnd_tc_flow(rmnetctl_hndl_t *hndl,
			      uint32_t id,
			      uint32_t map_flow_id,
			      uint32_t tc_flow_id,
			      uint8_t set_flow,
			      uint16_t *error_code) {
	struct rmnet_nl_msg_s request, response;
	int return_code = RMNETCTL_LIB_ERR;
	do {
	if ((!hndl) || (!error_code) || ((set_flow != RMNETCTL_ADD_FLOW) &&
	    (set_flow != RMNETCTL_DEL_FLOW))) {
		return_code = RMNETCTL_INVALID_ARG;
		break;
	}
	if (set_flow ==  RMNETCTL_ADD_FLOW)
		request.message_type = RMNET_NETLINK_ADD_VND_TC_FLOW;
	else
		request.message_type = RMNET_NETLINK_DEL_VND_TC_FLOW;

	request.arg_length = (sizeof(uint32_t))*3;
	request.flow_control.id = id;
	request.flow_control.map_flow_id = map_flow_id;
	request.flow_control.tc_flow_id = tc_flow_id;

	if ((*error_code = rmnetctl_transact(hndl, &request, &response))
	!= RMNETCTL_SUCCESS)
		break;
	if (_rmnetctl_check_code(response.crd, error_code) != RMNETCTL_SUCCESS)
		break;
	return_code = _rmnetctl_set_codes(response.return_code, error_code);
	} while(0);
	return return_code;
}

/*
 *                       NEW DRIVER API
 */
/* @brief Synchronous method to receive messages to and from the kernel
 * using netlink sockets
 * @details Receives the ack response from the kernel.
 * @param *hndl RmNet handle for this transaction
 * @param *error_code Error code if transaction fails
 * @return RMNETCTL_API_SUCCESS if successfully able to send and receive message
 * from the kernel
 * @return RMNETCTL_API_ERR_HNDL_INVALID if RmNet handle for the transaction was
 * NULL
 * @return RMNETCTL_API_ERR_MESSAGE_RECEIVE if could not receive message from
 * the kernel
 * @return RMNETCTL_API_ERR_MESSAGE_TYPE if the response type does not
 * match
 */
static int rmnet_get_ack(rmnetctl_hndl_t *hndl, uint16_t *error_code)
{
	struct nlack {
		struct nlmsghdr ackheader;
		struct nlmsgerr ackdata;
		char   data[256];

	} ack;
	int i;

	if (!hndl || !error_code)
		return RMNETCTL_INVALID_ARG;

	if ((i = recv(hndl->netlink_fd, &ack, sizeof(ack), 0)) < 0) {
		*error_code = errno;
		return RMNETCTL_API_ERR_MESSAGE_RECEIVE;
	}

	/*Ack should always be NLMSG_ERROR type*/
	if (ack.ackheader.nlmsg_type == NLMSG_ERROR) {
		if (ack.ackdata.error == 0) {
			*error_code = RMNETCTL_API_SUCCESS;
			return RMNETCTL_SUCCESS;
		} else {
			*error_code = -ack.ackdata.error;
			return RMNETCTL_KERNEL_ERR;
		}
	}

	*error_code = RMNETCTL_API_ERR_RETURN_TYPE;
	return RMNETCTL_API_FIRST_ERR;
}

/*
 *                       EXPOSED NEW DRIVER API
 */
int rtrmnet_ctl_init(rmnetctl_hndl_t **hndl, uint16_t *error_code)
{
	struct sockaddr_nl __attribute__((__may_alias__)) *saddr_ptr;
	int netlink_fd = -1;
	pid_t pid = 0;

	if (!hndl || !error_code)
		return RMNETCTL_INVALID_ARG;

	*hndl = (rmnetctl_hndl_t *)malloc(sizeof(rmnetctl_hndl_t));
	if (!*hndl) {
		*error_code = RMNETCTL_API_ERR_HNDL_INVALID;
		return RMNETCTL_LIB_ERR;
	}

	memset(*hndl, 0, sizeof(rmnetctl_hndl_t));

	pid = getpid();
	if (pid  < MIN_VALID_PROCESS_ID) {
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_PROCESS_ID;
		return RMNETCTL_LIB_ERR;
	}
	(*hndl)->pid = KERNEL_PROCESS_ID;
	netlink_fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (netlink_fd < MIN_VALID_SOCKET_FD) {
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_NETLINK_FD;
		return RMNETCTL_LIB_ERR;
	}

	(*hndl)->netlink_fd = netlink_fd;

	memset(&(*hndl)->src_addr, 0, sizeof(struct sockaddr_nl));

	(*hndl)->src_addr.nl_family = AF_NETLINK;
	(*hndl)->src_addr.nl_pid = (*hndl)->pid;

	saddr_ptr = &(*hndl)->src_addr;
	if (bind((*hndl)->netlink_fd,
		(struct sockaddr *)saddr_ptr,
		sizeof(struct sockaddr_nl)) < 0) {
		close((*hndl)->netlink_fd);
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_BIND;
		return RMNETCTL_LIB_ERR;
	}

	memset(&(*hndl)->dest_addr, 0, sizeof(struct sockaddr_nl));

	(*hndl)->dest_addr.nl_family = AF_NETLINK;
	(*hndl)->dest_addr.nl_pid = KERNEL_PROCESS_ID;
	(*hndl)->dest_addr.nl_groups = UNICAST;

	return RMNETCTL_SUCCESS;
}

int rtrmnet_ctl_deinit(rmnetctl_hndl_t *hndl)
{
	if (!hndl)
		return RMNETCTL_SUCCESS;

	close(hndl->netlink_fd);
	free(hndl);

	return RMNETCTL_SUCCESS;
}

int rtrmnet_ctl_newvnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
		       uint16_t *error_code, uint8_t  index,
		       uint32_t flagconfig)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct ifla_vlan_flags flags;
	int devindex = 0, val = 0;
	char *kind = "rmnet";
	struct nlmsg req;
	short id;
	size_t reqsize;

	if (!hndl || !devname || !vndname || !error_code ||
	   _rmnetctl_check_dev_name(vndname) || _rmnetctl_check_dev_name(devname))
		return RMNETCTL_INVALID_ARG;

	memset(&req, 0, sizeof(req));
	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL |
				  NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type = RMNET_IFLA_NUM_TX_QUEUES;
	attrinfo->rta_len = RTA_LENGTH(4);
	*(int *)RTA_DATA(attrinfo) = RMNETCTL_NUM_TX_QUEUES;
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH((4)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	id = index;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_ID;
	attrinfo->rta_len = RTA_LENGTH(sizeof(id));
	*(short *)RTA_DATA(attrinfo) = id;
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(id)));

	if (flagconfig != 0) {
		flags.mask  = flagconfig;
		flags.flags = flagconfig;

		attrinfo = (struct rtattr *)(((char *)&req) +
					     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
		attrinfo->rta_type =  IFLA_VLAN_FLAGS;
		attrinfo->rta_len = RTA_LENGTH(sizeof(flags));
		CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flags,
			       sizeof(flags)));
		req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
					RTA_ALIGN(RTA_LENGTH(sizeof(flags)));
	}

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}

int rtrmnet_ctl_delvnd(rmnetctl_hndl_t *hndl, char *vndname,
		       uint16_t *error_code)
{
	int devindex = 0;
	struct nlmsg req;

	if (!hndl || !vndname || !error_code)
		return RMNETCTL_INVALID_ARG;

	memset(&req, 0, sizeof(req));
	req.nl_addr.nlmsg_type = RTM_DELLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of vndname*/
	devindex = if_nametoindex(vndname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup index attribute */
	req.ifmsg.ifi_index = devindex;
	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_ctl_changevnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
			  uint16_t *error_code, uint8_t  index,
			  uint32_t flagconfig)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct ifla_vlan_flags flags;
	char *kind = "rmnet";
	struct nlmsg req;
	int devindex = 0, val = 0;
	size_t reqsize;
	short id;

	memset(&req, 0, sizeof(req));

	if (!hndl || !devname || !vndname || !error_code ||
	    _rmnetctl_check_dev_name(vndname) || _rmnetctl_check_dev_name(devname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	id = index;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_ID;
	attrinfo->rta_len = RTA_LENGTH(sizeof(id));
	*(short *)RTA_DATA(attrinfo) = id;
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(id)));

	if (flagconfig != 0) {
		flags.mask  = flagconfig;
		flags.flags = flagconfig;

		attrinfo = (struct rtattr *)(((char *)&req) +
					     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
		attrinfo->rta_type =  IFLA_VLAN_FLAGS;
		attrinfo->rta_len = RTA_LENGTH(sizeof(flags));
		CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flags,
			       sizeof(flags)));
		req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
					RTA_ALIGN(RTA_LENGTH(sizeof(flags)));
	}

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}

int rtrmnet_ctl_bridgevnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
			  uint16_t *error_code)
{
	int devindex = 0, vndindex = 0;
	struct rtattr *masterinfo;
	struct nlmsg req;
	size_t reqsize;

	if (!hndl || !vndname || !devname || !error_code || _rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	memset(&req, 0, sizeof(req));
	reqsize = NLMSG_DATA_SIZE - sizeof(*masterinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of vndname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	vndindex = if_nametoindex(vndname);
	if (vndindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup index attribute */
	req.ifmsg.ifi_index = devindex;
	masterinfo = (struct rtattr *)(((char *)&req) +
				       NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	masterinfo->rta_type =  IFLA_MASTER;
	masterinfo->rta_len = RTA_LENGTH(sizeof(vndindex));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(masterinfo), &reqsize, &vndindex, sizeof(vndindex)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(vndindex)));

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_activate_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint32_t flow_id,
			  int ip_type,
			  uint32_t tcm_handle,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	int devindex = 0;
	int val = 0;
	size_t reqsize =0;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));


	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_handle = tcm_handle;
	flowinfo.tcm_family = 0x1;
	flowinfo.tcm__pad1 = bearer_id;
	flowinfo.tcm_ifindex = ip_type;
	flowinfo.tcm_parent = flow_id;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_delete_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint32_t flow_id,
			  int ip_type,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	int devindex = 0;
	int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_family = 0x2;
	flowinfo.tcm_ifindex = ip_type;
	flowinfo.tcm__pad1 = bearer_id;
	flowinfo.tcm_parent = flow_id;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));


	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}

int rtrmnet_control_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint16_t sequence,
			  uint32_t grantsize,
			  uint8_t ack,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	int devindex = 0;
	int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_family = 0x3;
	flowinfo.tcm__pad1 = bearer_id;
	flowinfo.tcm__pad2 = sequence;
	flowinfo.tcm_parent = ack;
	flowinfo.tcm_info = grantsize;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;
	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_flow_state_up(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint32_t instance,
			  uint32_t ep_type,
			  uint32_t ifaceid,
			  int flags,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	int devindex = 0;
	int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_handle = instance;
	flowinfo.tcm_family = 0x4;
	flowinfo.tcm_ifindex = flags;
	flowinfo.tcm_parent = ifaceid;
	flowinfo.tcm_info = ep_type;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;
	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_flow_state_down(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint32_t instance,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	int devindex = 0;
	int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(struct rtattr);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex < 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_handle = instance;
	flowinfo.tcm_family = 0x5;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));


	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;
	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}
