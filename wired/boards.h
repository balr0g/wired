/* $Id$ */

/*
 *  Copyright (c) 2008 Axel Andersson
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

#ifndef WD_BOARD_H
#define WD_BOARD_H 1

#include "users.h"

enum _wd_board_permissions {
	WD_BOARD_OWNER_WRITE				= (2 << 6),
	WD_BOARD_OWNER_READ					= (4 << 6),
	WD_BOARD_GROUP_WRITE				= (2 << 3),
	WD_BOARD_GROUP_READ					= (4 << 3),
	WD_BOARD_EVERYONE_WRITE				= (2 << 0),
	WD_BOARD_EVERYONE_READ				= (4 << 0)
};
typedef enum _wd_board_permissions		wd_board_permissions_t;

typedef struct _wd_board_privileges		wd_board_privileges_t;


void									wd_boards_initialize(void);

void									wd_boards_rename_owner(wi_string_t *, wi_string_t *);
void									wd_boards_rename_group(wi_string_t *, wi_string_t *);
															  
void									wd_boards_reply_boards(wd_user_t *, wi_p7_message_t *);
void									wd_boards_reply_posts(wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_boards_add_board(wi_string_t *, wd_board_privileges_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_rename_board(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_move_board(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_set_board_privileges(wi_string_t *, wd_board_privileges_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_delete_board(wi_string_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_boards_board_is_valid(wi_string_t *);

wi_boolean_t							wd_boards_add_thread(wi_string_t *, wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_move_thread(wi_string_t *, wi_uuid_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_delete_thread(wi_string_t *, wi_uuid_t *, wd_user_t *, wi_p7_message_t *);

wi_boolean_t							wd_boards_add_post(wi_string_t *, wi_uuid_t *, wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_edit_post(wi_string_t *, wi_uuid_t *, wi_uuid_t *, wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);
wi_boolean_t							wd_boards_delete_post(wi_string_t *, wi_uuid_t *, wi_uuid_t *, wd_user_t *, wi_p7_message_t *);

wd_board_privileges_t *					wd_board_privileges_with_message(wi_p7_message_t *);

#endif /* WD_BOARD_H */
