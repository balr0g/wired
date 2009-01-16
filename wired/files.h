/* $Id$ */

/*
 *  Copyright (c) 2003-2007 Axel Andersson
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

#ifndef WD_FILES_H
#define WD_FILES_H 1

#include <wired/wired.h>

#include "users.h"

enum _wd_file_type {
	WD_FILE_TYPE_FILE					= 0,
	WD_FILE_TYPE_DIR,
	WD_FILE_TYPE_UPLOADS,
	WD_FILE_TYPE_DROPBOX
};
typedef enum _wd_file_type				wd_file_type_t;

enum _wd_file_permissions {
	WD_FILE_OWNER_WRITE					= (2 << 6),
	WD_FILE_OWNER_READ					= (4 << 6),
	WD_FILE_GROUP_WRITE					= (2 << 3),
	WD_FILE_GROUP_READ					= (4 << 3),
	WD_FILE_EVERYONE_WRITE				= (2 << 0),
	WD_FILE_EVERYONE_READ				= (4 << 0)
};
typedef enum _wd_file_permissions		wd_file_permissions_t;


void									wd_files_init(void);
void									wd_files_apply_settings(wi_set_t *);
void									wd_files_schedule(void);

void									wd_files_reply_list(wi_string_t *, wi_boolean_t, wd_user_t *, wi_p7_message_t *);
void									wd_files_reply_info(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_create_path(wi_string_t *, wd_file_type_t, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_delete_path(wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_move_path(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_files_link_path(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);

void									wd_files_search(wi_string_t *, wd_user_t *, wi_p7_message_t *);

void									wd_files_index(wi_boolean_t);

wd_file_type_t							wd_files_type(wi_string_t *);
void									wd_files_set_type(wi_string_t *, wd_file_type_t, wd_user_t *, wi_p7_message_t *);

void									wd_files_set_executable(wi_string_t *, wi_boolean_t, wd_user_t *, wi_p7_message_t *);

wi_string_t *							wd_files_comment(wi_string_t *, wd_user_t *);
void									wd_files_set_comment(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
void									wd_files_move_comment(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_files_get_permissions(wi_string_t *, wi_string_t **, wi_string_t **, wi_uinteger_t *);
void									wd_files_set_permissions(wi_string_t *, wi_string_t *, wi_string_t *, wi_uinteger_t, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_files_path_is_valid(wi_string_t *);
wi_boolean_t							wd_files_drop_box_path_is_writable(wi_string_t *, wd_user_t *);
wi_boolean_t							wd_files_drop_box_path_is_readable(wi_string_t *, wd_user_t *);
wi_string_t *							wd_files_virtual_path(wi_string_t *, wd_user_t *);
wi_string_t *							wd_files_real_path(wi_string_t *, wd_user_t *);

wi_string_t *							wd_files_string_for_bytes(wi_file_offset_t);


extern wi_fsevents_t					*wd_files_fsevents;

extern wi_uinteger_t					wd_files_count;
extern wi_uinteger_t					wd_folders_count;
extern wi_file_offset_t					wd_files_size;

#endif /* WD_FILES_H */
