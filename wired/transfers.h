/* $Id$ */

/*
 *  Copyright (c) 2003-2009 Axel Andersson
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WD_TRANFERS_H
#define WD_TRANFERS_H 1

#include <wired/wired.h>

#include "files.h"
#include "main.h"

enum _wd_transfer_type {
	WD_TRANSFER_DOWNLOAD				= 0,
	WD_TRANSFER_UPLOAD
};
typedef enum _wd_transfer_type			wd_transfer_type_t;


enum _wd_transfer_state {
	WD_TRANSFER_QUEUED					= 0,
	WD_TRANSFER_WAITING,
	WD_TRANSFER_RUNNING,
	WD_TRANSFER_STOP,
	WD_TRANSFER_STOPPED
};
typedef enum _wd_transfer_state			wd_transfer_state_t;


struct _wd_transfer {
	wi_runtime_base_t					base;
	
	wd_user_t							*user;
	wi_p7_uint32_t						transaction;
	wi_string_t							*key;
	wi_boolean_t						disconnected;
	wi_boolean_t						failed;

	wi_string_t							*path;
	wi_string_t							*realdatapath, *realrsrcpath;
	int									datafd, rsrcfd;

	wi_condition_lock_t					*state_lock;
	wd_transfer_state_t					state;
	wd_transfer_type_t					type;
	wi_boolean_t						executable;

	uint32_t							queue;
	wi_time_interval_t					queue_time;
	wi_time_interval_t					waiting_time;

	wi_file_offset_t					dataoffset, rsrcoffset;
	wi_file_offset_t					datasize, rsrcsize;
	wi_file_offset_t					remainingdatasize, remainingrsrcsize;
	wi_file_offset_t					transferred, actualtransferred;
	uint32_t							speed;
	wi_boolean_t						finished;
	
	wi_data_t							*finderinfo;
};
typedef struct _wd_transfer				wd_transfer_t;


void									wd_transfers_init(void);
void									wd_transfers_apply_settings(wi_set_t *);
void									wd_transfers_schedule(void);

void									wd_transfers_queue_download(wi_string_t *, wi_file_offset_t, wi_file_offset_t, wd_user_t *, wi_p7_message_t *);
void									wd_transfers_queue_upload(wi_string_t *, wi_file_offset_t, wi_file_offset_t, wi_boolean_t, wd_user_t *, wi_p7_message_t *);
void									wd_transfers_remove_user(wd_user_t *, wi_boolean_t);
wd_transfer_t *							wd_transfers_transfer_with_path(wd_user_t *, wi_string_t *);

void									wd_transfer_start(wd_transfer_t *transfer);


extern wi_array_t						*wd_transfers;

#endif /* WD_TRANFERS_H */
