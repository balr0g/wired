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

#include "config.h"

#include <wired/wired.h>

#include "accounts.h"
#include "boards.h"
#include "server.h"
#include "settings.h"

#define WD_BOARDS_META_PATH						".wired"
#define WD_BOARDS_META_PERMISSIONS_PATH			".wired/permissions"

#define WD_BOARDS_POST_EXTENSION				"WiredPost"
#define WD_BOARDS_THREAD_EXTENSION				"WiredThread"

#define WD_BOARDS_PERMISSIONS_FIELD_SEPARATOR	"\34"

#define WD_BOARDS_NEWS_FIELD_SEPARATOR			"\34"
#define WD_BOARDS_NEWS_POST_SEPARATOR			"\35"


struct _wd_board_privileges {
	wi_runtime_base_t							base;
	
	wi_string_t									*owner;
	wi_string_t									*group;
	wi_uinteger_t								mode;
};


static void										wd_boards_broadcast_message(wi_p7_message_t *, wd_board_privileges_t *, wi_boolean_t);
static void										wd_boards_send_thread_added(wi_string_t *, wi_uuid_t *, wd_user_t *);

static wi_boolean_t								wd_boards_get_boards_and_privileges(wi_array_t **, wi_array_t **);
static void										wd_boards_rename_account(wi_boolean_t, wi_string_t *, wi_string_t *);
static wi_dictionary_t *						wd_boards_dictionary_with_post(wd_user_t *, wi_string_t *, wi_string_t *);
static wi_p7_message_t *						wd_boards_message_with_post_for_user(wi_string_t *, wi_string_t *, wi_uuid_t *, wi_uuid_t *, wi_dictionary_t *, wd_user_t *);
static wi_string_t *							wd_boards_board_path(wi_string_t *);
static wi_string_t *							wd_boards_thread_path(wi_string_t *, wi_uuid_t *);
static wi_string_t *							wd_boards_post_path(wi_string_t *, wi_uuid_t *, wi_uuid_t *);

static wi_boolean_t								wd_boards_set_privileges(wi_string_t *, wd_board_privileges_t *);
static wd_board_privileges_t *					wd_boards_privileges(wi_string_t *);
static wi_boolean_t								wd_boards_convert_news_to_boards(wi_string_t *, wi_string_t *);
static wi_boolean_t								wd_boards_rename_board_path(wi_string_t *, wi_string_t *, wd_user_t *, wi_p7_message_t *);

static wd_board_privileges_t *					wd_board_privileges_alloc(void);
static void										wd_board_privileges_dealloc(wi_runtime_instance_t *);

static wd_board_privileges_t *					wd_board_privileges_with_owner(wi_string_t *, wi_string_t *, wi_uinteger_t);
static wd_board_privileges_t *					wd_board_privileges_with_string(wi_string_t *);

static wi_string_t *							wd_board_privileges_string(wd_board_privileges_t *);
static wi_boolean_t								wd_board_privileges_is_readable_by_user(wd_board_privileges_t *, wd_user_t *);
static wi_boolean_t								wd_board_privileges_is_writable_by_user(wd_board_privileges_t *, wd_user_t *);
static wi_boolean_t								wd_board_privileges_is_readable_and_writable_by_user(wd_board_privileges_t *, wd_user_t *);


static wi_string_t								*wd_boards_path;
static wi_rwlock_t								*wd_boards_lock;

static wi_runtime_id_t							wd_board_privileges_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t						wd_board_privileges_runtime_class = {
	"wd_board_privileges_t",
	wd_board_privileges_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};



void wd_boards_initialize(void) {
	wd_boards_path = WI_STR("boards");
	wd_boards_lock = wi_rwlock_init(wi_rwlock_alloc());

	wd_board_privileges_runtime_id = wi_runtime_register_class(&wd_board_privileges_runtime_class);
	
	if(wi_fs_path_exists(WI_STR("board"), NULL))
		wd_boards_path = WI_STR("board");
	
	if(wi_fs_path_exists(WI_STR("news"), NULL)) {
		if(wd_boards_convert_news_to_boards(WI_STR("news"), WI_STR("News"))) {
			wi_log_info(WI_STR("Migrated news to board \"News\""));
			wi_fs_delete_path(WI_STR("news"));
		}
	}
}



#pragma mark -

void wd_boards_rename_owner(wi_string_t *name, wi_string_t *new_name) {
	wd_boards_rename_account(true, name, new_name);
}



void wd_boards_rename_group(wi_string_t *name, wi_string_t *new_name) {
	wd_boards_rename_account(false, name, new_name);
}



#pragma mark -

void wd_boards_reply_boards(wd_user_t *user, wi_p7_message_t *message) {
	wi_array_t					*boards, *privileges;
	wi_string_t					*board;
	wi_p7_message_t				*reply;
	wd_board_privileges_t		*privs;
	wi_uinteger_t				i, count;
	
	if(wd_boards_get_boards_and_privileges(&boards, &privileges)) {
		count = wi_array_count(boards);
		
		for(i = 0; i < count; i++) {
			board = WI_ARRAY(boards, i);
			privs = WI_ARRAY(privileges, i);
			
			if(wd_board_privileges_is_readable_and_writable_by_user(privs, user)) {
				reply = wi_p7_message_with_name(WI_STR("wired.board.board_list"), wd_p7_spec);
				wi_p7_message_set_string_for_name(reply, board, WI_STR("wired.board.board"));
				wi_p7_message_set_string_for_name(reply, privs->owner, WI_STR("wired.board.owner"));
				wi_p7_message_set_bool_for_name(reply, (privs->mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
				wi_p7_message_set_bool_for_name(reply, (privs->mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
				wi_p7_message_set_string_for_name(reply, privs->group, WI_STR("wired.board.group"));
				wi_p7_message_set_bool_for_name(reply, (privs->mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
				wi_p7_message_set_bool_for_name(reply, (privs->mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
				wi_p7_message_set_bool_for_name(reply, (privs->mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
				wi_p7_message_set_bool_for_name(reply, (privs->mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
				wd_user_reply_message(user, reply, message);
			}
		}
		
		reply = wi_p7_message_with_name(WI_STR("wired.board.board_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
	} else {
		wd_user_reply_internal_error(user, wi_error_string(), message);
	}
}



void wd_boards_reply_posts(wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t		*instance;
	wi_fsenumerator_t			*fsenumerator;
	wi_p7_message_t				*reply;
	wi_array_t					*components;
	wi_uuid_t					*thread, *post;
	wi_string_t					*path, *board;
	wd_board_privileges_t		*privileges;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				pathlength, count;
	
	pathlength = wi_string_length(wd_boards_path);
	
	wi_rwlock_rdlock(wd_boards_lock);
	
	fsenumerator = wi_fs_enumerator_at_path(wd_boards_path);
	
	if(fsenumerator) {
		while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
			if(status == WI_FSENUMERATOR_ERROR) {
				wi_log_err(WI_STR("Could not read board \"%@\": %m"), path);
				
				continue;
			}
			
			if(!wi_is_equal(wi_string_path_extension(path), WI_STR(WD_BOARDS_POST_EXTENSION)))
				continue;
			
			instance = wi_plist_read_instance_from_file(path);
			
			if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
				components	= wi_string_path_components(wi_string_substring_from_index(path, pathlength + 1));
				count		= wi_array_count(components);
				
				post		= wi_uuid_with_string(wi_string_by_deleting_path_extension(WI_ARRAY(components, count - 1)));
				thread		= wi_uuid_with_string(wi_string_by_deleting_path_extension(WI_ARRAY(components, count - 2)));
				board		= wi_array_components_joined_by_string(wi_array_subarray_with_range(components, wi_make_range(0, count - 2)), WI_STR("/"));
				privileges	= wd_boards_privileges(board);
				
				if(privileges && wd_board_privileges_is_readable_by_user(privileges, user)) {
					reply = wd_boards_message_with_post_for_user(WI_STR("wired.board.post_list"), board, thread, post, instance, user);
					
					wd_user_reply_message(user, reply, message);
				}
			}
		}

		reply = wi_p7_message_with_name(WI_STR("wired.board.post_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
	} else {
		wi_log_err(WI_STR("Could not open \"%@\": %m"), wd_boards_path);
		wd_user_reply_internal_error(user, wi_error_string(), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
}



#pragma mark -

wi_boolean_t wd_boards_add_board(wi_string_t *board, wd_board_privileges_t *privileges, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*broadcast;
	wi_string_t				*path;
	wi_boolean_t			added = false;
	
	path = wd_boards_board_path(board);
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	if(!wi_fs_path_exists(path, NULL)) {
		if(wi_fs_create_directory(path, 0755)) {
			if(wd_boards_set_privileges(board, privileges)) {
				broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_added"), wd_p7_spec);
				wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
				wi_p7_message_set_string_for_name(broadcast, privileges->owner, WI_STR("wired.board.owner"));
				wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
				wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
				wi_p7_message_set_string_for_name(broadcast, privileges->group, WI_STR("wired.board.group"));
				wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
				wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
				wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
				wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
				wd_boards_broadcast_message(broadcast, privileges, false);
				
				added = true;
			} else {
				wd_user_reply_file_errno(user, message);
			}
		} else {
			wi_log_err(WI_STR("Could not create \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_exists"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return added;
}



wi_boolean_t wd_boards_rename_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*broadcast;
	wd_board_privileges_t	*privileges;
	
	if(wd_boards_rename_board_path(oldboard, newboard, user, message)) {
		privileges = wd_boards_privileges(newboard);
		
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_renamed"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
		wd_boards_broadcast_message(broadcast, privileges, false);
		
		return true;
	}
	
	return false;
}



wi_boolean_t wd_boards_move_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*broadcast;
	wd_board_privileges_t	*privileges;
	
	if(wd_boards_rename_board_path(oldboard, newboard, user, message)) {
		privileges = wd_boards_privileges(newboard);
		
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_moved"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
		wd_boards_broadcast_message(broadcast, privileges, false);
		
		return true;
	}
	
	return false;
}



wi_boolean_t wd_boards_set_board_privileges(wi_string_t *board, wd_board_privileges_t *privileges, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*broadcast;
	
	if(wd_boards_set_privileges(board, privileges)) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.permissions_changed"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, privileges->owner, WI_STR("wired.board.owner"));
		wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
		wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
		wi_p7_message_set_string_for_name(broadcast, privileges->group, WI_STR("wired.board.group"));
		wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
		wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
		wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
		wi_p7_message_set_bool_for_name(broadcast, (privileges->mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
		wd_boards_broadcast_message(broadcast, privileges, false);
		
		return true;
	} else {
		wd_user_reply_file_errno(user, message);
	}
	
	return false;
}



wi_boolean_t wd_boards_delete_board(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*broadcast;
	wi_string_t				*path;
	wd_board_privileges_t	*privileges;
	wi_boolean_t			deleted = false;
	
	path = wd_boards_board_path(board);
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	if(wi_fs_path_exists(path, NULL)) {
		privileges = wd_boards_privileges(board);
		
		if(wi_fs_delete_path(path)) {
			broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_deleted"), wd_p7_spec);
			wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
			wd_boards_broadcast_message(broadcast, privileges, false);
			
			deleted = true;
		} else {
			wi_log_err(WI_STR("Could not delete \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return deleted;
}



#pragma mark -

wi_boolean_t wd_boards_board_is_valid(wi_string_t *board) {
	if(wi_string_length(board) == 0)
		return false;
	
	if(wi_string_contains_string(board, WI_STR(".."), 0))
		return false;
	
	if(wi_string_has_prefix(board, WI_STR("/")))
		return false;
	
	return true;
}



#pragma mark -

static void wd_boards_broadcast_message(wi_p7_message_t *message, wd_board_privileges_t *privileges, wi_boolean_t readable) {
	wi_enumerator_t		*enumerator;
	wd_user_t			*user;
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(user)) {
			if((readable && wd_board_privileges_is_readable_by_user(privileges, user)) ||
			   (!readable && wd_board_privileges_is_readable_and_writable_by_user(privileges, user)))
				wd_user_send_message(user, message);
		}
	}
	
	wi_dictionary_unlock(wd_users);
}



static void wd_boards_send_thread_added(wi_string_t *board, wi_uuid_t *thread, wd_user_t *user) {
	wi_runtime_instance_t		*instance;
	wi_fsenumerator_t			*fsenumerator;
	wi_p7_message_t				*message;
	wi_uuid_t					*post;
	wi_string_t					*path;
	wi_fsenumerator_status_t	status;
	
	path = wd_boards_thread_path(board, thread);
	
	fsenumerator = wi_fs_enumerator_at_path(path);
	
	if(!fsenumerator) {
		wi_log_err(WI_STR("Could not open \"%@\": %m"), path);
		
		return;
	}
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_err(WI_STR("Could not read board \"%@\": %m"), path);
			
			continue;
		}
		
		if(!wi_is_equal(wi_string_path_extension(path), WI_STR(WD_BOARDS_POST_EXTENSION)))
			continue;
		
		instance = wi_plist_read_instance_from_file(path);
		
		if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
			post = wi_uuid_with_string(wi_string_by_deleting_path_extension(wi_string_last_path_component(path)));

			message = wd_boards_message_with_post_for_user(WI_STR("wired.board.post_added"), board, thread, post, instance, user);
			
			wd_user_send_message(user, message);
		}
	}
}



#pragma mark -

static wi_boolean_t wd_boards_get_boards_and_privileges(wi_array_t **boards_out, wi_array_t **privileges_out) {
	wi_fsenumerator_t			*fsenumerator;
	wi_mutable_array_t			*boards, *privileges;
	wi_string_t					*path, *board, *extension;
	wd_board_privileges_t		*privs;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				pathlength;
	
	pathlength	= wi_string_length(wd_boards_path);
	boards		= wi_mutable_array();
	privileges	= wi_mutable_array();
	
	wi_rwlock_rdlock(wd_boards_lock);
	
	fsenumerator = wi_fs_enumerator_at_path(wd_boards_path);
	
	if(fsenumerator) {
		while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
			if(status == WI_FSENUMERATOR_ERROR) {
				wi_log_err(WI_STR("Could not read board \"%@\": %m"), path);
				
				continue;
			}
			
			extension = wi_string_path_extension(path);
			
			if(wi_is_equal(extension, WI_STR(WD_BOARDS_POST_EXTENSION)) || wi_is_equal(extension, WI_STR(WD_BOARDS_THREAD_EXTENSION))) {
				wi_fsenumerator_skip_descendents(fsenumerator);
				
				continue;
			}
			
			board = wi_string_substring_from_index(path, pathlength + 1);
			privs = wd_boards_privileges(board);
			
			if(privs) {
				wi_mutable_array_add_data(boards, board);
				wi_mutable_array_add_data(privileges, privs);
			}
		}
	} else {
		wi_log_err(WI_STR("Could not open \"%@\": %m"), wd_boards_path);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	*boards_out			= boards;
	*privileges_out		= privileges;
		
	return true;
}



static void wd_boards_rename_account(wi_boolean_t user, wi_string_t *name, wi_string_t *new_name) {
	wi_array_t					*boards, *privileges;
	wi_string_t					*board;
	wi_p7_message_t				*broadcast;
	wd_board_privileges_t		*privs;
	wi_uinteger_t				i, count;
	
	if(wd_boards_get_boards_and_privileges(&boards, &privileges)) {
		count = wi_array_count(boards);
		
		for(i = 0; i < count; i++) {
			board = WI_ARRAY(boards, i);
			privs = WI_ARRAY(privileges, i);
			
			if((user && wi_is_equal(privs->owner, name)) || (!user && wi_is_equal(privs->group, name))) {
				if(user) {
					wi_release(privs->owner);
					privs->owner = wi_retain(new_name);
				} else {
					wi_release(privs->group);
					privs->group = wi_retain(new_name);
				}
				
				if(wd_boards_set_privileges(board, privs)) {
					broadcast = wi_p7_message_with_name(WI_STR("wired.board.permissions_changed"), wd_p7_spec);
					wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
					wi_p7_message_set_string_for_name(broadcast, privs->owner, WI_STR("wired.board.owner"));
					wi_p7_message_set_bool_for_name(broadcast, (privs->mode & WD_BOARD_OWNER_READ), WI_STR("wired.board.owner.read"));
					wi_p7_message_set_bool_for_name(broadcast, (privs->mode & WD_BOARD_OWNER_WRITE), WI_STR("wired.board.owner.write"));
					wi_p7_message_set_string_for_name(broadcast, privs->group, WI_STR("wired.board.group"));
					wi_p7_message_set_bool_for_name(broadcast, (privs->mode & WD_BOARD_GROUP_READ), WI_STR("wired.board.group.read"));
					wi_p7_message_set_bool_for_name(broadcast, (privs->mode & WD_BOARD_GROUP_WRITE), WI_STR("wired.board.group.write"));
					wi_p7_message_set_bool_for_name(broadcast, (privs->mode & WD_BOARD_EVERYONE_READ), WI_STR("wired.board.everyone.read"));
					wi_p7_message_set_bool_for_name(broadcast, (privs->mode & WD_BOARD_EVERYONE_WRITE), WI_STR("wired.board.everyone.write"));
					wd_boards_broadcast_message(broadcast, privs, false);
				}
			}
		}
	}
}



static wi_dictionary_t * wd_boards_dictionary_with_post(wd_user_t *user, wi_string_t *subject, wi_string_t *text) {
	wi_mutable_dictionary_t		*dictionary;
	wi_data_t					*icon;
	
	dictionary = wi_mutable_dictionary();
	wi_mutable_dictionary_set_data_for_key(dictionary, wd_user_nick(user), WI_STR("wired.user.nick"));
	wi_mutable_dictionary_set_data_for_key(dictionary, wd_user_login(user), WI_STR("wired.user.login"));
	wi_mutable_dictionary_set_data_for_key(dictionary, wi_date(), WI_STR("wired.board.post_date"));
	wi_mutable_dictionary_set_data_for_key(dictionary, subject, WI_STR("wired.board.subject"));
	wi_mutable_dictionary_set_data_for_key(dictionary, text, WI_STR("wired.board.text"));
	
	icon = wd_user_icon(user);
	
	if(icon)
		wi_mutable_dictionary_set_data_for_key(dictionary, icon, WI_STR("wired.user.icon"));
	
	return dictionary;
}



static wi_p7_message_t * wd_boards_message_with_post_for_user(wi_string_t *name, wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wi_dictionary_t *dictionary, wd_user_t *user) {
	wi_p7_message_t		*message;
	wi_date_t			*edit_date;
	wi_string_t			*login;
	
	message = wi_p7_message_with_name(name, wd_p7_spec);
	wi_p7_message_set_string_for_name(message, board, WI_STR("wired.board.board"));
	wi_p7_message_set_uuid_for_name(message, thread, WI_STR("wired.board.thread"));
	wi_p7_message_set_uuid_for_name(message, post, WI_STR("wired.board.post"));
	wi_p7_message_set_date_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post_date")), WI_STR("wired.board.post_date"));
	
	edit_date = wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"));
	
	if(edit_date)
		wi_p7_message_set_date_for_name(message, edit_date, WI_STR("wired.board.edit_date"));
	
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")), WI_STR("wired.user.nick"));
	
	login = wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.login"));

	wi_p7_message_set_bool_for_name(message, wi_is_equal(login, wd_user_login(user)), WI_STR("wired.board.own_post"));
	wi_p7_message_set_data_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.icon")), WI_STR("wired.user.icon"));
	
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.subject")), WI_STR("wired.board.subject"));
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.text")), WI_STR("wired.board.text"));

	return message;
}



static wi_string_t * wd_boards_board_path(wi_string_t *board) {
	return wi_string_by_appending_path_component(wd_boards_path, board);
}



static wi_string_t * wd_boards_thread_path(wi_string_t *board, wi_uuid_t *thread) {
	wi_string_t		*path;
	
	path = wi_string_by_appending_path_component(wd_boards_board_path(board), wi_uuid_string(thread));
	
	return wi_string_by_appending_path_extension(path, WI_STR(WD_BOARDS_THREAD_EXTENSION));
}



static wi_string_t * wd_boards_post_path(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post) {
	wi_string_t		*path;
	
	path = wi_string_by_appending_path_component(wd_boards_thread_path(board, thread), wi_uuid_string(post));

	return wi_string_by_appending_path_extension(path, WI_STR(WD_BOARDS_POST_EXTENSION));
}



#pragma mark -

static wi_boolean_t wd_boards_set_privileges(wi_string_t *board, wd_board_privileges_t *privileges) {
	wi_string_t		*path, *metapath, *permissionspath;
	wi_string_t		*string;
	
	path				= wd_boards_board_path(board);
	metapath			= wi_string_by_appending_path_component(path, WI_STR(WD_BOARDS_META_PATH));
	permissionspath		= wi_string_by_appending_path_component(path, WI_STR(WD_BOARDS_META_PERMISSIONS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_warn(WI_STR("Could not create \"%@\": %m"), metapath);
			
			return false;
		}
	}
	
	string = wi_string_by_appending_string(wd_board_privileges_string(privileges), WI_STR("\n"));
	
	if(!wi_string_write_to_file(string, permissionspath)) {
		wi_log_warn(WI_STR("Could not write to \"%@\": %m"), permissionspath);
		
		return false;
	}
	
	return true;
}



static wd_board_privileges_t * wd_boards_privileges(wi_string_t *board) {
	wi_string_t				*path, *permissionspath, *string;
	wd_board_privileges_t	*privileges;
	wi_fs_stat_t			sb;
	
	path				= wd_boards_board_path(board);
	permissionspath		= wi_string_by_appending_path_component(path, WI_STR(WD_BOARDS_META_PERMISSIONS_PATH));
	
	if(!wi_fs_stat_path(permissionspath, &sb)) {
		wi_log_warn(WI_STR("Could not open \"%@\": %m"), permissionspath);
		
		return NULL;
	}
	
	if(sb.size > 128) {
		wi_log_warn(WI_STR("Could not read %@: Size is too large (%u"), permissionspath, sb.size);
		
		return NULL;
	}
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), permissionspath));
	
	if(!string) {
		wi_log_warn(WI_STR("Could not read \"%@\": %m"), permissionspath);
		
		return NULL;
	}
	
	privileges = wd_board_privileges_with_string(string);
	
	if(!privileges) {
		wi_log_warn(WI_STR("Could not read \"%@\": Contents is malformed (\"%@\")"), permissionspath, string);
		
		return NULL;
	}
	
	return privileges;
}



static wi_boolean_t wd_boards_convert_news_to_boards(wi_string_t *news_path, wi_string_t *board) {
	wi_array_t				*array;
	wi_file_t				*file;
	wi_dictionary_t			*dictionary;
	wi_uuid_t				*thread, *post;
	wi_string_t				*string, *subject, *board_path, *thread_path, *post_path;
	wd_board_privileges_t	*privileges;
	
	board_path = wd_boards_board_path(board);
	
	if(wi_fs_path_exists(board_path, NULL)) {
		wi_log_warn(WI_STR("Could not convert news to \"%@\": Board already exists"), board);

		return false;
	}
	
	if(!wi_fs_create_directory(board_path, 0755)) {
		wi_log_warn(WI_STR("Could not open \"%@\": %m"), news_path);
		
		return false;
	}
	
	privileges = wd_board_privileges_with_owner(WI_STR("admin"), WI_STR(""),
		WD_BOARD_OWNER_WRITE | WD_BOARD_OWNER_READ | WD_BOARD_GROUP_WRITE | WD_BOARD_GROUP_READ | WD_BOARD_EVERYONE_WRITE | WD_BOARD_EVERYONE_READ);
	
	if(!wd_boards_set_privileges(board, privileges))
		return false;

	file = wi_file_for_reading(news_path);
	
	if(!file) {
		wi_log_warn(WI_STR("Could not open \"%@\": %m"), news_path);
		
		return false;
	}
	
	while((string = wi_file_read_to_string(file, WI_STR(WD_BOARDS_NEWS_POST_SEPARATOR)))) {
		array = wi_string_components_separated_by_string(string, WI_STR(WD_BOARDS_NEWS_FIELD_SEPARATOR));
		
		if(wi_array_count(array) == 3) {
			thread		= wi_uuid();
			post		= wi_uuid();
			thread_path	= wd_boards_thread_path(board, thread);
			post_path	= wd_boards_post_path(board, thread, post);
			subject		= WI_ARRAY(array, 2);
			
			if(!wi_fs_create_directory(thread_path, 0755)) {
				wi_log_warn(WI_STR("Could not create \"%@\": %m"), post_path);
				
				continue;
			}
			
			if(wi_string_length(subject) > 32)
				subject = wi_string_with_format(WI_STR("%@..."), wi_string_substring_to_index(subject, 31));
			
			dictionary = wi_dictionary_with_data_and_keys(
				WI_ARRAY(array, 0),									WI_STR("wired.user.nick"),
				WI_STR("admin"),									WI_STR("wired.user.login"),
				wi_date_with_rfc3339_string(WI_ARRAY(array, 1)),	WI_STR("wired.board.post_date"),
				subject,											WI_STR("wired.board.subject"),
				WI_ARRAY(array, 2),									WI_STR("wired.board.text"),
				NULL);
			
			if(!wi_plist_write_instance_to_file(dictionary, post_path))
				wi_log_warn(WI_STR("Could not create \"%@\": %m"), post_path);
		}
	}
	
	return true;
}



static wi_boolean_t wd_boards_rename_board_path(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*path, *oldpath, *newpath;
	wi_boolean_t		renamed = false;
	
	oldpath = wd_boards_board_path(oldboard);
	newpath = wd_boards_board_path(newboard);

	wi_rwlock_wrlock(wd_boards_lock);
	
	if(wi_fs_path_exists(oldpath, NULL)) {
		if(wi_string_case_insensitive_compare(oldpath, newpath) == 0) {
			path = wi_fs_temporary_path_with_template(
				wi_string_with_format(WI_STR("%@/.%@.XXXXXXXX"),
					  wi_string_by_deleting_last_path_component(oldpath),
					  wi_string_last_path_component(newpath)));
			
			if(path) {
				if(wi_fs_rename_path(oldpath, path)) {
					if(wi_fs_rename_path(path, newpath)) {
						renamed = true;
					} else {
						wi_log_err(WI_STR("Could not rename %@ to \"%@\": %m"), path, newpath);
						wd_user_reply_internal_error(user, wi_error_string(), message);
					}
				} else {
					wi_log_err(WI_STR("Could not rename \"%@\" to \"%@\": %m"), oldpath, path);
					wd_user_reply_internal_error(user, wi_error_string(), message);
				}
			}
		} else {
			if(!wi_fs_path_exists(newpath, NULL)) {
				if(wi_fs_rename_path(oldpath, newpath)) {
					renamed = true;
				} else {
					wi_log_err(WI_STR("Could not rename \"%@\" to \"%@\": %m"), oldpath, newpath);
					wd_user_reply_internal_error(user, wi_error_string(), message);
				}
			} else {
				wd_user_reply_error(user, WI_STR("wired.error.board_exists"), message);
			}
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return renamed;
}



#pragma mark -

wi_boolean_t wd_boards_add_thread(wi_string_t *board, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_uuid_t				*thread, *post;
	wi_array_t				*users;
	wi_string_t				*path;
	wd_board_privileges_t	*privileges;
	wd_user_t				*peer;
	wi_boolean_t			result = false;
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	thread		= wi_uuid();
	path		= wd_boards_thread_path(board, thread);
	privileges	= wd_boards_privileges(board);
	
	if(privileges && wd_board_privileges_is_writable_by_user(privileges, user)) {
		if(wi_fs_create_directory(path, 0755)) {
			dictionary	= wd_boards_dictionary_with_post(user, subject, text);
			post		= wi_uuid();
			path		= wd_boards_post_path(board, thread, post);
			
			if(wi_plist_write_instance_to_file(dictionary, path)) {
				users = wd_chat_users(wd_public_chat);
				
				wi_array_rdlock(users);
				
				enumerator = wi_array_data_enumerator(users);
				
				while((peer = wi_enumerator_next_data(enumerator))) {
					if(wd_user_state(peer) == WD_USER_LOGGED_IN &&
					   wd_board_privileges_is_readable_by_user(privileges, peer) &&
					   wd_user_is_subscribed_boards(peer)) {
						reply = wd_boards_message_with_post_for_user(WI_STR("wired.board.post_added"), board, thread, post, dictionary, peer);
						
						wd_user_send_message(peer, reply);
					}
				}
				
				wi_array_unlock(users);
				
				result = true;
			} else {
				wi_log_err(WI_STR("Could not create \"%@\": %m"), path);
				wd_user_reply_internal_error(user, wi_error_string(), message);
			}
		} else {
			wi_log_err(WI_STR("Could not create \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return result;
}



wi_boolean_t wd_boards_move_thread(wi_string_t *oldboard, wi_uuid_t *thread, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*movedmessage, *deletedmessage;
	wi_enumerator_t			*enumerator;
	wi_array_t				*users;
	wi_string_t				*oldpath, *newpath;
	wd_board_privileges_t	*oldprivileges, *newprivileges;
	wd_user_t				*peer;
	wi_boolean_t			oldreadable, newreadable;
	wi_boolean_t			result = false;
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	oldpath			= wd_boards_thread_path(oldboard, thread);
	newpath			= wd_boards_thread_path(newboard, thread);
	oldprivileges	= wd_boards_privileges(oldboard);
	newprivileges	= wd_boards_privileges(newboard);
	
	if(oldprivileges && wd_board_privileges_is_writable_by_user(oldprivileges, user) &&
	   newprivileges && wd_board_privileges_is_writable_by_user(newprivileges, user)) {
		if(wi_fs_rename_path(oldpath, newpath)) {
			movedmessage = wi_p7_message_with_name(WI_STR("wired.board.thread_moved"), wd_p7_spec);
			wi_p7_message_set_string_for_name(movedmessage, oldboard, WI_STR("wired.board.board"));
			wi_p7_message_set_string_for_name(movedmessage, newboard, WI_STR("wired.board.new_board"));
			wi_p7_message_set_uuid_for_name(movedmessage, thread, WI_STR("wired.board.thread"));
			
			deletedmessage = wi_p7_message_with_name(WI_STR("wired.board.thread_deleted"), wd_p7_spec);
			wi_p7_message_set_string_for_name(deletedmessage, oldboard, WI_STR("wired.board.board"));
			wi_p7_message_set_uuid_for_name(deletedmessage, thread, WI_STR("wired.board.thread"));
			
			users = wd_chat_users(wd_public_chat);
			
			wi_array_rdlock(users);
			
			enumerator = wi_array_data_enumerator(users);
			
			while((peer = wi_enumerator_next_data(enumerator))) {
				if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_boards(peer)) {
					oldreadable = wd_board_privileges_is_readable_by_user(oldprivileges, peer);
					newreadable = wd_board_privileges_is_readable_by_user(newprivileges, peer);
					
					if(oldreadable && newreadable)
						wd_user_send_message(peer, movedmessage);
					else if(oldreadable)
						wd_user_send_message(peer, deletedmessage);
					else if(newreadable)
						wd_boards_send_thread_added(newboard, thread, peer);
				}
			}
			
			wi_array_unlock(users);
			
			result = true;
		} else {
			wi_log_err(WI_STR("Could not move \"%@\" to \"%@\": %m"), oldpath, newpath);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return result;
}



wi_boolean_t wd_boards_delete_thread(wi_string_t *board, wi_uuid_t *thread, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*broadcast;
	wi_string_t				*path;
	wd_board_privileges_t	*privileges;
	wi_boolean_t			result = false;
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	privileges = wd_boards_privileges(board);
	
	if(privileges && wd_board_privileges_is_writable_by_user(privileges, user)) {
		path = wd_boards_thread_path(board, thread);

		if(wi_fs_delete_path(path)) {
			broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_deleted"), wd_p7_spec);
			wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
			wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
			wd_boards_broadcast_message(broadcast, privileges, true);
			
			result = true;
		} else {
			wi_log_err(WI_STR("Could not delete \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return result;
}



#pragma mark -

wi_boolean_t wd_boards_add_post(wi_string_t *board, wi_uuid_t *thread, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_enumerator_t			*enumerator;
	wi_dictionary_t			*dictionary;
	wi_string_t				*path;
	wi_uuid_t				*post;
	wi_array_t				*users;
	wd_board_privileges_t	*privileges;
	wd_user_t				*peer;
	wi_boolean_t			result = false;
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	privileges = wd_boards_privileges(board);

	if(privileges && wd_board_privileges_is_writable_by_user(privileges, user)) {
		post		= wi_uuid();
		path		= wd_boards_post_path(board, thread, post);
		dictionary	= wd_boards_dictionary_with_post(user, subject, text);
		
		if(wi_plist_write_instance_to_file(dictionary, path)) {
			users = wd_chat_users(wd_public_chat);
			
			wi_array_rdlock(users);
			
			enumerator = wi_array_data_enumerator(users);
			
			while((peer = wi_enumerator_next_data(enumerator))) {
				if(wd_user_state(peer) == WD_USER_LOGGED_IN &&
				   wd_board_privileges_is_readable_by_user(privileges, peer) &&
				   wd_user_is_subscribed_boards(peer)) {
					reply = wd_boards_message_with_post_for_user(WI_STR("wired.board.post_added"), board, thread, post, dictionary, peer);
					
					wd_user_send_message(peer, reply);
				}
			}
			
			wi_array_unlock(users);
			
			result = true;
		} else {
			wi_log_err(WI_STR("Could not create \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return result;
}



wi_boolean_t wd_boards_edit_post(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
	wi_p7_message_t			*broadcast;
	wi_date_t				*edit_date;
	wi_string_t				*path, *login;
	wd_account_t			*account;
	wd_board_privileges_t	*privileges;
	wi_boolean_t			edit = true, result = false;
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	privileges = wd_boards_privileges(board);
	
	if(privileges && wd_board_privileges_is_writable_by_user(privileges, user)) {
		path		= wd_boards_post_path(board, thread, post);
		instance	= wi_plist_read_instance_from_file(path);

		if(instance) {
			if(wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
				account = wd_user_account(user);
				
				if(!wd_account_board_edit_all_posts(account)) {
					login = wi_dictionary_data_for_key(instance, WI_STR("wired.user.login"));
					
					if(!wi_is_equal(login, wd_user_login(user))) {
						wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
						
						edit = false;
					}
				}
				
				if(edit) {
					edit_date = wi_date();
					
					wi_mutable_dictionary_set_data_for_key(instance, edit_date, WI_STR("wired.board.edit_date"));
					wi_mutable_dictionary_set_data_for_key(instance, subject, WI_STR("wired.board.subject"));
					wi_mutable_dictionary_set_data_for_key(instance, text, WI_STR("wired.board.text"));
					
					if(wi_plist_write_instance_to_file(instance, path)) {
						broadcast = wi_p7_message_with_name(WI_STR("wired.board.post_edited"), wd_p7_spec);
						wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
						wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
						wi_p7_message_set_uuid_for_name(broadcast, post, WI_STR("wired.board.post"));
						wi_p7_message_set_date_for_name(broadcast, edit_date, WI_STR("wired.board.edit_date"));
						wi_p7_message_set_string_for_name(broadcast, subject, WI_STR("wired.board.subject"));
						wi_p7_message_set_string_for_name(broadcast, text, WI_STR("wired.board.text"));
						wd_boards_broadcast_message(broadcast, privileges, true);
						
						result = true;
					} else {
						wi_log_err(WI_STR("Could not edit \"%@\": %m"), path);
						wd_user_reply_internal_error(user, wi_error_string(), message);
					}
				}
			} else {
				wi_log_err(WI_STR("Could not edit \"%@\": File is not a dictionary"), path);
				wd_user_reply_internal_error(user, wi_error_string(), message);
			}
		} else {
			wi_log_err(WI_STR("Could not open \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return result;
}



wi_boolean_t wd_boards_delete_post(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
	wi_p7_message_t			*broadcast;
	wi_string_t				*path, *login;
	wd_account_t			*account;
	wd_board_privileges_t	*privileges;
	wi_boolean_t			delete = true, result = false;
	
	wi_rwlock_wrlock(wd_boards_lock);
	
	privileges = wd_boards_privileges(board);
	
	if(privileges && wd_board_privileges_is_writable_by_user(privileges, user)) {
		path		= wd_boards_post_path(board, thread, post);
		instance	= wi_plist_read_instance_from_file(path);

		if(instance) {
			if(wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
				account = wd_user_account(user);
				
				if(!wd_account_board_delete_all_posts(account)) {
					login = wi_dictionary_data_for_key(instance, WI_STR("wired.user.login"));
					
					if(!wi_is_equal(login, wd_user_login(user))) {
						wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
						
						delete = false;
					}
				}
				
				if(delete) {
					if(wi_fs_delete_path(path)) {
						(void) rmdir(wi_string_cstring(wi_string_by_deleting_last_path_component(path)));

						broadcast = wi_p7_message_with_name(WI_STR("wired.board.post_deleted"), wd_p7_spec);
						wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
						wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
						wi_p7_message_set_uuid_for_name(broadcast, post, WI_STR("wired.board.post"));
						wd_boards_broadcast_message(broadcast, privileges, true);
						
						result = true;
					} else {
						wi_log_err(WI_STR("Could not delete \"%@\": %m"), path);
						wd_user_reply_internal_error(user, wi_error_string(), message);
					}
				}
			} else {
				wi_log_err(WI_STR("Could not delete \"%@\": File is not a dictionary"), path);
				wd_user_reply_internal_error(user, wi_error_string(), message);
			}
		} else {
			wi_log_err(WI_STR("Could not open \"%@\": %m"), path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	}
	
	wi_rwlock_unlock(wd_boards_lock);
	
	return result;
}



#pragma mark -

static wd_board_privileges_t * wd_board_privileges_alloc(void) {
	return wi_runtime_create_instance(wd_board_privileges_runtime_id, sizeof(wd_board_privileges_t));
}



static void wd_board_privileges_dealloc(wi_runtime_instance_t *instance) {
	wd_board_privileges_t		*privileges = instance;
	
	wi_release(privileges->owner);
	wi_release(privileges->group);
}



#pragma mark -

wd_board_privileges_t * wd_board_privileges_with_message(wi_p7_message_t *message) {
	wd_board_privileges_t		*privileges;
	wi_p7_boolean_t				value;
	
	privileges = wd_board_privileges_alloc();
	privileges->owner = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.board.owner")));

	if(!privileges->owner)
		privileges->owner = wi_retain(WI_STR(""));
	
	privileges->group = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.board.group")));
	
	if(!privileges->group)
		privileges->group = wi_retain(WI_STR(""));
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.owner.read")) && value)
		privileges->mode |= WD_BOARD_OWNER_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.owner.write")) && value)
		privileges->mode |= WD_BOARD_OWNER_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.group.read")) && value)
		privileges->mode |= WD_BOARD_GROUP_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.group.write")) && value)
		privileges->mode |= WD_BOARD_GROUP_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.everyone.read")) && value)
		privileges->mode |= WD_BOARD_EVERYONE_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.board.everyone.write")) && value)
		privileges->mode |= WD_BOARD_EVERYONE_WRITE;

	return wi_autorelease(privileges);
}



static wd_board_privileges_t * wd_board_privileges_with_owner(wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode) {
	wd_board_privileges_t		*privileges;
	
	privileges				= wd_board_privileges_alloc();
	privileges->owner		= wi_retain(owner);
	privileges->group		= wi_retain(group);
	privileges->mode		= mode;
	
	return wi_autorelease(privileges);
}



static wd_board_privileges_t * wd_board_privileges_with_string(wi_string_t *string) {
	wi_array_t				*array;
	
	string = wi_string_by_deleting_surrounding_whitespace(string);
	
	array = wi_string_components_separated_by_string(string, WI_STR(WD_BOARDS_PERMISSIONS_FIELD_SEPARATOR));
	
	if(wi_array_count(array) != 3)
		return NULL;
	
	return wd_board_privileges_with_owner(WI_ARRAY(array, 0), WI_ARRAY(array, 1), wi_string_uint32(WI_ARRAY(array, 2)));
}



#pragma mark -

static wi_string_t * wd_board_privileges_string(wd_board_privileges_t *privileges) {
	return wi_string_with_format(WI_STR("%#@%s%#@%s%u"),
	   privileges->owner,		WD_BOARDS_PERMISSIONS_FIELD_SEPARATOR,
	   privileges->group,		WD_BOARDS_PERMISSIONS_FIELD_SEPARATOR,
	   privileges->mode);
}



static wi_boolean_t wd_board_privileges_is_readable_by_user(wd_board_privileges_t *privileges, wd_user_t *user) {
	wd_account_t	*account;
	
	if(privileges->mode & WD_BOARD_EVERYONE_READ)
		return true;
	
	account = wd_user_account(user);
	
	if(privileges->mode & WD_BOARD_GROUP_READ && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_BOARD_OWNER_READ && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



static wi_boolean_t wd_board_privileges_is_writable_by_user(wd_board_privileges_t *privileges, wd_user_t *user) {
	wd_account_t	*account;
	
	if(privileges->mode & WD_BOARD_EVERYONE_WRITE)
		return true;
	
	account = wd_user_account(user);
	
	if(privileges->mode & WD_BOARD_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_BOARD_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



static wi_boolean_t wd_board_privileges_is_readable_and_writable_by_user(wd_board_privileges_t *privileges, wd_user_t *user) {
	wd_account_t	*account;
	
	if(privileges->mode & WD_BOARD_EVERYONE_READ && privileges->mode & WD_BOARD_EVERYONE_WRITE)
		return true;
	
	account = wd_user_account(user);
	
	if(privileges->mode & WD_BOARD_GROUP_READ && privileges->mode & WD_BOARD_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_BOARD_OWNER_READ && privileges->mode & WD_BOARD_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}
