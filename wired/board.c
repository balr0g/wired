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
#include "board.h"
#include "server.h"
#include "settings.h"

static wi_dictionary_t *			_wd_board_dictionary_with_post(wd_user_t *, wi_string_t *, wi_string_t *);
static wi_p7_message_t *			_wd_board_message_with_post(wi_string_t *, wi_string_t *, wi_uuid_t *, wi_uuid_t *, wi_dictionary_t *);
static wi_string_t *				_wd_board_board_path(wi_string_t *);
static wi_string_t *				_wd_board_thread_path(wi_string_t *board, wi_uuid_t *);
static wi_string_t *				_wd_board_post_path(wi_string_t *, wi_uuid_t *, wi_uuid_t *);
	

static wi_string_t					*wd_board_path;
static wi_rwlock_t					*wd_board_lock;



void wd_board_init(void) {
	wd_board_path = WI_STR("board");
	wd_board_lock = wi_rwlock_init(wi_rwlock_alloc());
}



#pragma mark -

void wd_board_reply_boards(wd_user_t *user, wi_p7_message_t *message) {
	wi_fsenumerator_t			*fsenumerator;
	wi_p7_message_t				*reply;
	wi_string_t					*path, *board;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				pathlength;
	
	pathlength = wi_string_length(wd_board_path);

	wi_rwlock_rdlock(wd_board_lock);

	fsenumerator = wi_fs_enumerator_at_path(wd_board_path);
	
	if(fsenumerator) {
		while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
			if(status == WI_FSENUMERATOR_ERROR) {
				wi_log_err(WI_STR("Could not read board %@: %m"), path);

				continue;
			}
			
			if(wi_string_length(wi_string_path_extension(path)) > 0) {
				wi_fsenumerator_skip_descendents(fsenumerator);
				
				continue;
			}
			
			board = wi_string_substring_from_index(path, pathlength + 1);

			reply = wi_p7_message_with_name(WI_STR("wired.board.board_list"), wd_p7_spec);
			wi_p7_message_set_string_for_name(reply, board, WI_STR("wired.board.board"));
			wd_user_reply_message(user, reply, message);
		}

		reply = wi_p7_message_with_name(WI_STR("wired.board.board_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_board_path);
		wd_user_reply_internal_error(user, message);
	}

	wi_rwlock_unlock(wd_board_lock);
}



void wd_board_reply_posts(wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t		*instance;
	wi_fsenumerator_t			*fsenumerator;
	wi_p7_message_t				*reply;
	wi_array_t					*components;
	wi_uuid_t					*thread, *post;
	wi_string_t					*path, *board;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				pathlength, count;
	
	pathlength = wi_string_length(wd_board_path);
	
	wi_rwlock_rdlock(wd_board_lock);
	
	fsenumerator = wi_fs_enumerator_at_path(wd_board_path);
	
	if(fsenumerator) {
		while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
			if(status == WI_FSENUMERATOR_ERROR) {
				wi_log_err(WI_STR("Could not read board %@: %m"), path);
				
				continue;
			}
			
			if(!wi_is_equal(wi_string_path_extension(path), WI_STR("WiredPost")))
				continue;
			
			instance = wi_plist_read_instance_from_file(path);
			
			if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
				components	= wi_string_path_components(wi_string_substring_from_index(path, pathlength + 1));
				count		= wi_array_count(components);
				
				post		= wi_uuid_with_string(wi_string_by_deleting_path_extension(WI_ARRAY(components, count - 1)));
				thread		= wi_uuid_with_string(wi_string_by_deleting_path_extension(WI_ARRAY(components, count - 2)));
				board		= wi_array_components_joined_by_string(wi_array_subarray_with_range(components, wi_make_range(0, count - 2)), WI_STR("/"));
				
				reply = _wd_board_message_with_post(WI_STR("wired.board.post_list"), board, thread, post, instance);
				wd_user_reply_message(user, reply, message);
			}
		}

		reply = wi_p7_message_with_name(WI_STR("wired.board.post_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_board_path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
}



#pragma mark -

void wd_board_add_board(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path;
	wi_boolean_t		added = false;
	
	path = _wd_board_board_path(board);
	
	wi_rwlock_wrlock(wd_board_lock);
	
	if(!wi_fs_path_exists(path, NULL)) {
		if(wi_fs_create_directory(path, 0755)) {
			added = true;
		} else {
			wi_log_err(WI_STR("Could not create %@: %m"), path);
			wd_user_reply_internal_error(user, message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_exists"), message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
	
	if(added) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_added"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



void wd_board_rename_board(wi_string_t *oldboard, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*oldpath, *newpath;
	wi_boolean_t		renamed = false;
	
	oldpath = _wd_board_board_path(oldboard);
	newpath = _wd_board_board_path(newboard);
	
	wi_rwlock_wrlock(wd_board_lock);
	
	if(wi_fs_path_exists(oldpath, NULL)) {
		if(!wi_fs_path_exists(oldpath, NULL)) {
			if(wi_fs_rename_path(oldpath, newpath)) {
				renamed = true;
			} else {
				wi_log_err(WI_STR("Could not rename %@ to %@: %m"), oldpath, newpath);
				wd_user_reply_internal_error(user, message);
			}
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.board_exists"), message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
	
	if(renamed) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_renamed"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



void wd_board_delete_board(wi_string_t *board, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path;
	wi_boolean_t		deleted = false;
	
	path = _wd_board_board_path(board);
	
	wi_rwlock_wrlock(wd_board_lock);
	
	if(wi_fs_path_exists(path, NULL)) {
		if(wi_fs_delete_path(path)) {
			deleted = true;
		} else {
			wi_log_err(WI_STR("Could not delete %@: %m"), path);
			wd_user_reply_internal_error(user, message);
		}
	} else {
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
	
	if(deleted) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.board_deleted"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
}



#pragma mark -

static wi_dictionary_t * _wd_board_dictionary_with_post(wd_user_t *user, wi_string_t *subject, wi_string_t *text) {
	return wi_dictionary_with_data_and_keys(
		wi_date(),				WI_STR("wired.board.date"),
		subject,				WI_STR("wired.board.subject"),
		text,					WI_STR("wired.board.text"),
		NULL);

	return wi_dictionary_with_data_and_keys(
		wd_user_nick(user),		WI_STR("wired.user.nick"),
		wd_user_login(user),	WI_STR("wired.user.login"),
		wi_date(),				WI_STR("wired.board.post_date"),
		subject,				WI_STR("wired.board.subject"),
		text,					WI_STR("wired.board.text"),
		NULL);
}



static wi_p7_message_t * _wd_board_message_with_post(wi_string_t *name, wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wi_dictionary_t *dictionary) {
	wi_p7_message_t		*message;
	wi_date_t			*edit_date;
	
	message = wi_p7_message_with_name(WI_STR("wired.board.post_list"), wd_p7_spec);
	wi_p7_message_set_string_for_name(message, board, WI_STR("wired.board.board"));
	wi_p7_message_set_uuid_for_name(message, thread, WI_STR("wired.board.thread"));
	wi_p7_message_set_uuid_for_name(message, post, WI_STR("wired.board.post"));
	wi_p7_message_set_date_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.post_date")), WI_STR("wired.board.post_date"));
	
	edit_date = wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.edit_date"));
	
	if(edit_date)
		wi_p7_message_set_date_for_name(message, edit_date, WI_STR("wired.board.edit_date"));
	
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.nick")), WI_STR("wired.board.nick"));
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.subject")), WI_STR("wired.board.subject"));
	wi_p7_message_set_string_for_name(message, wi_dictionary_data_for_key(dictionary, WI_STR("wired.board.text")), WI_STR("wired.board.text"));

	return message;
}



static wi_string_t * _wd_board_board_path(wi_string_t *board) {
	return wi_string_by_appending_path_component(wd_board_path, board);
}



static wi_string_t * _wd_board_thread_path(wi_string_t *board, wi_uuid_t *thread) {
	wi_string_t		*path;
	
	path = _wd_board_board_path(board);
	
	wi_string_append_path_component(path, wi_uuid_string(thread));
	wi_string_append_path_extension(path, WI_STR("WiredThread"));
	
	return path;
}



static wi_string_t * _wd_board_post_path(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post) {
	wi_string_t		*path;
	
	path = _wd_board_board_path(board);
	
	wi_string_append_path_component(path, wi_uuid_string(thread));
	wi_string_append_path_extension(path, WI_STR("WiredThread"));
	wi_string_append_path_component(path, wi_uuid_string(post));
	wi_string_append_path_extension(path, WI_STR("WiredPost"));
	
	return path;
}



#pragma mark -

void wd_board_add_thread(wi_string_t *board, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_dictionary_t		*dictionary;
	wi_uuid_t			*thread, *post;
	wi_string_t			*path;
	
	thread	= wi_uuid();
	path	= _wd_board_thread_path(board, thread);
	
	wi_rwlock_wrlock(wd_board_lock);
	
	if(wi_fs_create_directory(path, 0755)) {
		dictionary	= _wd_board_dictionary_with_post(user, subject, text);
		post		= wi_uuid();
		path		= _wd_board_post_path(board, thread, post);
		
		if(wi_plist_write_instance_to_file(dictionary, path)) {
			broadcast = _wd_board_message_with_post(WI_STR("wired.board.post_added"), board, thread, post, dictionary);
			wd_chat_broadcast_message(wd_public_chat, broadcast);
		} else {
			wi_log_err(WI_STR("Could not create %@: %m"), path);
			wd_user_reply_internal_error(user, message);
		}
	} else {
		wi_log_err(WI_STR("Could not create %@: %m"), path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
}



void wd_board_move_thread(wi_string_t *oldboard, wi_uuid_t *thread, wi_string_t *newboard, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*oldpath, *newpath;
	
	oldpath = _wd_board_thread_path(oldboard, thread);
	newpath = _wd_board_thread_path(newboard, thread);

	wi_rwlock_wrlock(wd_board_lock);
	
	if(wi_fs_rename_path(oldpath, newpath)) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.board.thread_moved"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, oldboard, WI_STR("wired.board.board"));
		wi_p7_message_set_string_for_name(broadcast, newboard, WI_STR("wired.board.new_board"));
		wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	} else {
		wi_log_err(WI_STR("Could not move %@ to %@: %m"), oldpath, newpath);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
}



#pragma mark -

void wd_board_add_post(wi_string_t *board, wi_uuid_t *thread, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_dictionary_t		*dictionary;
	wi_uuid_t			*post;
	wi_string_t			*path;
	
	post		= wi_uuid();
	path		= _wd_board_post_path(board, thread, post);
	dictionary	= _wd_board_dictionary_with_post(user, subject, text);
		
	wi_rwlock_wrlock(wd_board_lock);
	
	if(wi_plist_write_instance_to_file(dictionary, path)) {
		broadcast = _wd_board_message_with_post(WI_STR("wired.board.post_added"), board, thread, post, dictionary);
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	} else {
		wi_log_err(WI_STR("Could not create %@: %m"), path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
}



void wd_board_edit_post(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wi_string_t *subject, wi_string_t *text, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
	wi_p7_message_t			*broadcast;
	wi_date_t				*edit_date;
	wi_string_t				*path, *login;
	wd_account_t			*account;
	wi_boolean_t			edit = true;
	
	path = _wd_board_post_path(board, thread, post);

	wi_rwlock_wrlock(wd_board_lock);
	
	instance = wi_plist_read_instance_from_file(path);

	if(instance) {
		if(wi_runtime_id(instance) == wi_dictionary_runtime_id()) {
			account = wd_user_account(user);
			
//			if(!account->board_edit_all_posts && account->board_edit_own_posts) {
				login = wi_dictionary_data_for_key(instance, WI_STR("wired.board.login"));
				
				if(!wi_is_equal(login, wd_user_login(user))) {
					wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
					
					edit = false;
				}
//			}
			
			if(edit) {
				edit_date = wi_date();
				
				wi_dictionary_set_data_for_key(instance, edit_date, WI_STR("wired.board.edit_date"));
				wi_dictionary_set_data_for_key(instance, subject, WI_STR("wired.board.subject"));
				wi_dictionary_set_data_for_key(instance, text, WI_STR("wired.board.text"));
				
				if(wi_plist_write_instance_to_file(instance, path)) {
					broadcast = wi_p7_message_with_name(WI_STR("wired.board.post_edited"), wd_p7_spec);
					wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
					wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
					wi_p7_message_set_uuid_for_name(broadcast, post, WI_STR("wired.board.post"));
					wi_p7_message_set_date_for_name(broadcast, edit_date, WI_STR("wired.board.edit_date"));
					wi_p7_message_set_string_for_name(broadcast, subject, WI_STR("wired.board.subject"));
					wi_p7_message_set_string_for_name(broadcast, text, WI_STR("wired.board.text"));
					wd_chat_broadcast_message(wd_public_chat, broadcast);
				} else {
					wi_log_err(WI_STR("Could not edit %@: %m"), path);
					wd_user_reply_internal_error(user, message);
				}
			}
		} else {
			wi_log_err(WI_STR("Could not edit %@: File is not a dictionary"), path);
			wd_user_reply_internal_error(user, message);
		}
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
}



void wd_board_delete_post(wi_string_t *board, wi_uuid_t *thread, wi_uuid_t *post, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*path;
	
	path = _wd_board_post_path(board, thread, post);
	
	wi_rwlock_wrlock(wd_board_lock);
	
	if(wi_fs_delete_path(path)) {
		(void) rmdir(wi_string_cstring(wi_string_by_deleting_last_path_component(path)));

		broadcast = wi_p7_message_with_name(WI_STR("wired.board.post_deleted"), wd_p7_spec);
		wi_p7_message_set_string_for_name(broadcast, board, WI_STR("wired.board.board"));
		wi_p7_message_set_uuid_for_name(broadcast, thread, WI_STR("wired.board.thread"));
		wi_p7_message_set_uuid_for_name(broadcast, post, WI_STR("wired.board.post"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	} else {
		wi_log_err(WI_STR("Could not delete %@: %m"), path);
		wd_user_reply_internal_error(user, message);
	}
	
	wi_rwlock_unlock(wd_board_lock);
}
