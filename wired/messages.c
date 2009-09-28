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

#include "banlist.h"
#include "boards.h"
#include "chats.h"
#include "files.h"
#include "main.h"
#include "messages.h"
#include "server.h"
#include "servers.h"
#include "settings.h"
#include "transfers.h"
#include "users.h"

typedef void						wd_message_func_t(wd_user_t *, wi_p7_message_t *);


static void							wd_message_client_info(wd_user_t *, wi_p7_message_t *);
static void							wd_message_send_ping(wd_user_t *, wi_p7_message_t *);
static void							wd_message_ping(wd_user_t *, wi_p7_message_t *);
static void							wd_message_send_login(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_set_icon(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_set_nick(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_set_status(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_set_idle(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_get_info(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_disconnect_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_ban_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_user_get_users(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_join_chat(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_leave_chat(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_set_topic(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_send_say_or_me(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_create_chat(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_invite_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_decline_invitation(wd_user_t *, wi_p7_message_t *);
static void							wd_message_chat_kick_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_message_send_message(wd_user_t *, wi_p7_message_t *);
static void							wd_message_message_send_broadcast(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_get_boards(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_get_posts(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_add_board(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_rename_board(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_move_board(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_delete_board(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_set_permissions(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_add_thread(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_move_thread(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_delete_thread(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_add_post(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_edit_post(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_delete_post(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_subscribe_boards(wd_user_t *, wi_p7_message_t *);
static void							wd_message_board_unsubscribe_boards(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_list_directory(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_get_info(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_move(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_link(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_set_type(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_set_comment(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_set_executable(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_set_permissions(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_set_label(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_delete(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_create_directory(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_search(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_preview_file(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_subscribe_directory(wd_user_t *, wi_p7_message_t *);
static void							wd_message_file_unsubscribe_directory(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_change_password(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_list_users(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_list_groups(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_read_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_read_group(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_create_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_create_group(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_edit_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_edit_group(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_delete_user(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_delete_group(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_subscribe_accounts(wd_user_t *, wi_p7_message_t *);
static void							wd_message_account_unsubscribe_accounts(wd_user_t *, wi_p7_message_t *);
static void							wd_message_transfer_download_file(wd_user_t *, wi_p7_message_t *);
static void							wd_message_transfer_upload_file(wd_user_t *, wi_p7_message_t *);
static void							wd_message_transfer_upload_directory(wd_user_t *, wi_p7_message_t *);
static void							wd_message_log_get_log(wd_user_t *, wi_p7_message_t *);
static void							wd_message_log_subscribe(wd_user_t *, wi_p7_message_t *);
static void							wd_message_log_unsubscribe(wd_user_t *, wi_p7_message_t *);
static void							wd_message_settings_get_settings(wd_user_t *, wi_p7_message_t *);
static void							wd_message_settings_set_settings(wd_user_t *, wi_p7_message_t *);
static void							wd_message_banlist_get_bans(wd_user_t *, wi_p7_message_t *);
static void							wd_message_banlist_add_ban(wd_user_t *, wi_p7_message_t *);
static void							wd_message_banlist_delete_ban(wd_user_t *, wi_p7_message_t *);
static void							wd_message_tracker_get_categories(wd_user_t *, wi_p7_message_t *);
static void							wd_message_tracker_get_servers(wd_user_t *, wi_p7_message_t *);
static void							wd_message_tracker_send_register(wd_user_t *, wi_p7_message_t *);
static void							wd_message_tracker_send_update(wd_user_t *, wi_p7_message_t *);


static wi_mutable_dictionary_t		*wd_message_handlers;



#define WD_MESSAGE_HANDLER(message, handler) \
	wi_mutable_dictionary_set_data_for_key(wd_message_handlers, (handler), (message))
	
void wd_messages_init(void) {
	wd_message_handlers = wi_dictionary_init_with_capacity_and_callbacks(wi_mutable_dictionary_alloc(),
		0, wi_dictionary_default_key_callbacks, wi_dictionary_null_value_callbacks);
	
	WD_MESSAGE_HANDLER(WI_STR("wired.client_info"), wd_message_client_info);
	WD_MESSAGE_HANDLER(WI_STR("wired.send_ping"), wd_message_send_ping);
	WD_MESSAGE_HANDLER(WI_STR("wired.ping"), wd_message_ping);
	WD_MESSAGE_HANDLER(WI_STR("wired.send_login"), wd_message_send_login);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.set_icon"), wd_message_user_set_icon);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.set_nick"), wd_message_user_set_nick);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.set_status"), wd_message_user_set_status);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.set_idle"), wd_message_user_set_idle);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.get_info"), wd_message_user_get_info);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.disconnect_user"), wd_message_user_disconnect_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.ban_user"), wd_message_user_ban_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.user.get_users"), wd_message_user_get_users);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.join_chat"), wd_message_chat_join_chat);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.leave_chat"), wd_message_chat_leave_chat);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.set_topic"), wd_message_chat_set_topic);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.send_me"), wd_message_chat_send_say_or_me);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.send_say"), wd_message_chat_send_say_or_me);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.create_chat"), wd_message_chat_create_chat);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.invite_user"), wd_message_chat_invite_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.decline_invitation"), wd_message_chat_decline_invitation);
	WD_MESSAGE_HANDLER(WI_STR("wired.chat.kick_user"), wd_message_chat_kick_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.message.send_message"), wd_message_message_send_message);
	WD_MESSAGE_HANDLER(WI_STR("wired.message.send_broadcast"), wd_message_message_send_broadcast);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.get_boards"), wd_message_board_get_boards);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.get_posts"), wd_message_board_get_posts);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.add_board"), wd_message_board_add_board);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.rename_board"), wd_message_board_rename_board);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.move_board"), wd_message_board_move_board);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.delete_board"), wd_message_board_delete_board);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.set_permissions"), wd_message_board_set_permissions);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.add_thread"), wd_message_board_add_thread);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.move_thread"), wd_message_board_move_thread);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.delete_thread"), wd_message_board_delete_thread);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.add_post"), wd_message_board_add_post);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.edit_post"), wd_message_board_edit_post);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.delete_post"), wd_message_board_delete_post);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.subscribe_boards"), wd_message_board_subscribe_boards);
	WD_MESSAGE_HANDLER(WI_STR("wired.board.unsubscribe_boards"), wd_message_board_unsubscribe_boards);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.list_directory"), wd_message_file_list_directory);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.get_info"), wd_message_file_get_info);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.move"), wd_message_file_move);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.link"), wd_message_file_link);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.set_type"), wd_message_file_set_type);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.set_comment"), wd_message_file_set_comment);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.set_executable"), wd_message_file_set_executable);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.set_permissions"), wd_message_file_set_permissions);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.set_label"), wd_message_file_set_label);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.delete"), wd_message_file_delete);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.create_directory"), wd_message_file_create_directory);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.search"), wd_message_file_search);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.preview_file"), wd_message_file_preview_file);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.subscribe_directory"), wd_message_file_subscribe_directory);
	WD_MESSAGE_HANDLER(WI_STR("wired.file.unsubscribe_directory"), wd_message_file_unsubscribe_directory);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.change_password"), wd_message_account_change_password);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.list_users"), wd_message_account_list_users);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.list_groups"), wd_message_account_list_groups);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.read_user"), wd_message_account_read_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.read_group"), wd_message_account_read_group);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.create_user"), wd_message_account_create_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.create_group"), wd_message_account_create_group);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.edit_user"), wd_message_account_edit_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.edit_group"), wd_message_account_edit_group);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.delete_user"), wd_message_account_delete_user);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.delete_group"), wd_message_account_delete_group);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.subscribe_accounts"), wd_message_account_subscribe_accounts);
	WD_MESSAGE_HANDLER(WI_STR("wired.account.unsubscribe_accounts"), wd_message_account_unsubscribe_accounts);
	WD_MESSAGE_HANDLER(WI_STR("wired.transfer.download_file"), wd_message_transfer_download_file);
	WD_MESSAGE_HANDLER(WI_STR("wired.transfer.upload_file"), wd_message_transfer_upload_file);
	WD_MESSAGE_HANDLER(WI_STR("wired.transfer.upload_directory"), wd_message_transfer_upload_directory);
	WD_MESSAGE_HANDLER(WI_STR("wired.log.get_log"), wd_message_log_get_log);
	WD_MESSAGE_HANDLER(WI_STR("wired.log.subscribe"), wd_message_log_subscribe);
	WD_MESSAGE_HANDLER(WI_STR("wired.log.unsubscribe"), wd_message_log_unsubscribe);
	WD_MESSAGE_HANDLER(WI_STR("wired.settings.get_settings"), wd_message_settings_get_settings);
	WD_MESSAGE_HANDLER(WI_STR("wired.settings.set_settings"), wd_message_settings_set_settings);
	WD_MESSAGE_HANDLER(WI_STR("wired.banlist.get_bans"), wd_message_banlist_get_bans);
	WD_MESSAGE_HANDLER(WI_STR("wired.banlist.add_ban"), wd_message_banlist_add_ban);
	WD_MESSAGE_HANDLER(WI_STR("wired.banlist.delete_ban"), wd_message_banlist_delete_ban);
	WD_MESSAGE_HANDLER(WI_STR("wired.tracker.get_categories"), wd_message_tracker_get_categories);
	WD_MESSAGE_HANDLER(WI_STR("wired.tracker.get_servers"), wd_message_tracker_get_servers);
	WD_MESSAGE_HANDLER(WI_STR("wired.tracker.send_register"), wd_message_tracker_send_register);
	WD_MESSAGE_HANDLER(WI_STR("wired.tracker.send_update"), wd_message_tracker_send_update);
}



void wd_messages_loop_for_user(wd_user_t *user) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wi_p7_message_t			*message;
	wi_socket_state_t		state;
	wd_user_state_t			user_state;
	wi_time_interval_t		timeout;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	socket = wd_user_socket(user);
	p7_socket = wd_user_p7_socket(user);

	while(true) {
		timeout = wi_time_interval();
		
		do {
			state		= wi_socket_wait(socket, 0.1);
			user_state	= wd_user_state(user);
			
			if(state == WI_SOCKET_TIMEOUT) {
				if(wi_time_interval() - timeout >= 120.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && user_state <= WD_USER_LOGGED_IN);
		
		if(user_state == WD_USER_DISCONNECTED)
			break;
		
		if(state == WI_SOCKET_ERROR) {
			wi_log_err(WI_STR("Could not wait for message from %@: %m"),
				wd_user_identifier(user));

			break;
		}
		
		if(wi_time_interval() - timeout >= 120.0) {
			wi_log_err(WI_STR("Timed out waiting for message from %@"),
				wd_user_identifier(user));

			break;
		}

		wd_user_lock_socket(user);
		
		message = wi_p7_socket_read_message(p7_socket, 120.0);
		
		wd_user_unlock_socket(user);
		
		if(!message) {
			if(wi_error_domain() != WI_ERROR_DOMAIN_LIBWIRED && wi_error_code() != WI_ERROR_SOCKET_EOF) {
				wi_log_info(WI_STR("Could not read message from %@: %m"),
					wd_user_identifier(user));
			}
			
			break;
		}

		if(!wi_p7_socket_verify_message(p7_socket, message)) {
			wi_log_debug(WI_STR("Could not verify message: %m"));
			wd_user_reply_error(user, WI_STR("wired.error.invalid_message"), message);
			
			continue;
		}
		
		wd_messages_handle_message(message, user);
		
		wi_pool_set_context(pool, wi_p7_message_name(message));
		wi_pool_drain(pool);
	}
	
	if(wd_user_state(user) >= WD_USER_LOGGED_IN) {
		wi_lock_lock(wd_status_lock);
		wd_current_users--;
		wd_write_status(true);
		wi_lock_unlock(wd_status_lock);
	}

	wd_user_set_state(user, WD_USER_DISCONNECTED);

	if(wd_chat_contains_user(wd_public_chat, user))
		wd_chat_broadcast_user_leave(wd_public_chat, user);

	wi_log_info(WI_STR("Disconnect from %@"), wd_user_identifier(user));
	
	wi_p7_socket_close(p7_socket);
	wi_socket_close(socket);
	
	wd_users_remove_user(user);
	
	wi_release(pool);
}



void wd_messages_handle_message(wi_p7_message_t *message, wd_user_t *user) {
	wd_message_func_t		*handler;
	wi_string_t				*name;
	wd_user_state_t			user_state;
	
	name = wi_p7_message_name(message);
	handler = wi_dictionary_data_for_key(wd_message_handlers, name);
	
	if(!handler) {
		wi_log_warn(WI_STR("No handler for message %@"), name);
		wd_user_reply_error(user, WI_STR("wired.error.unrecognized_message"), message);
		
		return;
	}
	
	user_state = wd_user_state(user);

	if(user_state == WD_USER_CONNECTED) {
		if(handler != wd_message_client_info) {
			wi_log_warn(WI_STR("Could not process message %@: Out of sequence"), name);
			wd_user_reply_error(user, WI_STR("wired.error.message_out_of_sequence"), message);
			
			return;
		}
	}
	else if(user_state == WD_USER_GAVE_CLIENT_INFO) {
		if(handler != wd_message_send_ping &&
		   handler != wd_message_send_login &&
		   handler != wd_message_user_set_nick &&
		   handler != wd_message_user_set_status &&
		   handler != wd_message_user_set_icon) {
			wi_log_warn(WI_STR("Could not process message %@: Out of sequence"), name);
			wd_user_reply_error(user, WI_STR("wired.error.message_out_of_sequence"), message);
			
			return;
		}
	}
	
	(*handler)(user, message);
	
	if(handler != wd_message_send_ping &&
	   handler != wd_message_user_set_idle &&
	   handler != wd_message_user_get_users) {
		wd_user_set_idle_time(user, wi_date());
		
		if(wd_user_is_idle(user)) {
			wd_user_set_idle(user, false);
			
			wd_user_broadcast_status(user);
		}
	}
}



#pragma mark -

static void wd_message_client_info(wd_user_t *user, wi_p7_message_t *message) {
	wd_user_set_client_info(user, wd_client_info_with_message(message));
	wd_user_reply_message(user, wd_server_info_message(), message);
	wd_user_set_state(user, WD_USER_GAVE_CLIENT_INFO);
}



static void wd_message_send_ping(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	
	reply = wi_p7_message_with_name(WI_STR("wired.ping"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



static void wd_message_ping(wd_user_t *user, wi_p7_message_t *message) {
}



static void wd_message_send_login(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_string_t			*login, *password;
	wi_date_t			*expiration_date;
	wd_account_t		*account;
	
	login = wi_p7_message_string_for_name(message, WI_STR("wired.user.login"));
	
	wd_user_set_login(user, login);
	
	if(wd_banlist_ip_is_banned(wd_user_ip(user), &expiration_date)) {
		reply = wi_p7_message_with_name(WI_STR("wired.banned"), wd_p7_spec);
		
		if(expiration_date)
			wi_p7_message_set_date_for_name(reply, expiration_date, WI_STR("wired.banlist.expiration_date"));
		
		wd_user_reply_message(user, reply, message);
		
		wi_log_info(WI_STR("Login from %@ failed: Banned"),
					wd_user_identifier(user));
		
		return;
	}
	
	account = wd_accounts_read_user_and_group(login);
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.login_failed"), message);
		
		wi_log_info(WI_STR("Login from %@ failed: No such account"),
					wd_user_identifier(user));
		
		return;
	}
	
	password = wi_p7_message_string_for_name(message, WI_STR("wired.user.password"));
	
	if(!wi_is_equal(wd_account_password(account), password)) {
		wd_user_reply_error(user, WI_STR("wired.error.login_failed"), message);
		
		wi_log_info(WI_STR("Login from %@ failed: Wrong password"),
					wd_user_identifier(user));
		
		return;
	}
	
	wi_log_info(WI_STR("Login from %@ using %@ succeeded"),
				wd_user_identifier(user), wd_client_info_string(wd_user_client_info(user)));
	
	if(wd_account_user_cannot_set_nick(account) || !wd_user_nick(user))
		wd_user_set_nick(user, login);
	
	wd_user_set_account(user, account);
	
	wd_user_set_admin(user, (wd_account_user_disconnect_users(account) || wd_account_user_ban_users(account)));
	wd_user_set_state(user, WD_USER_LOGGED_IN);
	
	wi_lock_lock(wd_status_lock);
	wd_current_users++;
	wd_total_users++;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);
	
	reply = wi_p7_message_with_name(WI_STR("wired.login"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, wd_user_id(user), WI_STR("wired.user.id"));
	wd_user_reply_message(user, reply, message);
	
	reply = wd_account_privileges_message(account);
	wd_user_reply_message(user, reply, message);
	
	wd_accounts_update_login_time(account);
}



static void wd_message_user_set_nick(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*nick;
	wd_account_t	*account;
	
	account = wd_user_account(user);
	
	if(account && wd_account_user_cannot_set_nick(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	nick = wi_p7_message_string_for_name(message, WI_STR("wired.user.nick"));
	
	if(!wi_is_equal(nick, wd_user_nick(user))) {
		wd_user_set_nick(user, nick);
		
		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_status(user);
	}
	
	wd_user_reply_okay(user, message);
}



static void wd_message_user_set_status(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*status;
	
	status = wi_p7_message_string_for_name(message, WI_STR("wired.user.status"));
	
	if(!wi_is_equal(status, wd_user_status(user))) {
		wd_user_set_status(user, status);
		
		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_status(user);
	}
	
	wd_user_reply_okay(user, message);
}



static void wd_message_user_set_icon(wd_user_t *user, wi_p7_message_t *message) {
	wi_data_t		*icon;
	
	icon = wi_p7_message_data_for_name(message, WI_STR("wired.user.icon"));
	
	if(!wi_is_equal(icon, wd_user_icon(user))) {
		wd_user_set_icon(user, icon);
		
		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_icon(user);
	}
	
	wd_user_reply_okay(user, message);
}



static void wd_message_user_set_idle(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_boolean_t		idle;
	
	wi_p7_message_get_bool_for_name(message, &idle, WI_STR("wired.user.idle"));
	
	if(idle != wd_user_is_idle(user)) {
		wd_user_set_idle(user, idle);
		
		if(wd_user_state(user) == WD_USER_LOGGED_IN)
			wd_user_broadcast_status(user);
	}
	
	wd_user_reply_okay(user, message);
}



static void wd_message_user_get_info(wd_user_t *user, wi_p7_message_t *message) {
	wd_user_t		*peer;
	wi_p7_uint32_t	uid;
	
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));
	
	peer = wd_users_user_with_id(uid);
	
	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);
		
		return;
	}
	
	wd_user_reply_user_info(peer, user, message);
}



static void wd_message_user_disconnect_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*broadcast;
	wi_string_t			*disconnect_message;
	wd_user_t			*peer;
	wd_chat_t			*chat;
	wi_p7_uint32_t		uid;
	
	if(!wd_account_user_disconnect_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));
	
	peer = wd_users_user_with_id(uid);
	
	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);
		
		return;
	}
	
	if(wd_account_user_cannot_be_disconnected(wd_user_account(peer))) {
		wd_user_reply_error(user, WI_STR("wired.error.user_cannot_be_disconnected"), message);
		
		return;
	}
	
	wi_log_info(WI_STR("%@ disconnected %@"),
		wd_user_identifier(user),
		wd_user_identifier(peer));
	
	disconnect_message	= wi_p7_message_string_for_name(message, WI_STR("wired.user.disconnect_message"));
	enumerator			= wi_array_data_enumerator(wd_chats_chats_with_user(user));
	
	while((chat = wi_enumerator_next_data(enumerator))) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.chat.user_disconnect"), wd_p7_spec);
		wi_p7_message_set_uint32_for_name(broadcast, wd_chat_id(chat), WI_STR("wired.chat.id"));
		wi_p7_message_set_uint32_for_name(broadcast, uid, WI_STR("wired.user.disconnected_id"));
		wi_p7_message_set_string_for_name(broadcast, disconnect_message, WI_STR("wired.user.disconnect_message"));
		wd_chat_broadcast_message(chat, broadcast);
	}
	
	wd_user_set_state(peer, WD_USER_DISCONNECTED);
	wd_user_reply_okay(user, message);
}



static void wd_message_user_ban_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*broadcast;
	wi_string_t			*disconnect_message;
	wi_date_t			*expiration_date;
	wd_user_t			*peer;
	wd_chat_t			*chat;
	wi_p7_uint32_t		uid;
	
	if(!wd_account_user_ban_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));
	
	peer = wd_users_user_with_id(uid);
	
	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);
		
		return;
	}
	
	if(wd_account_user_cannot_be_disconnected(wd_user_account(peer))) {
		wd_user_reply_error(user, WI_STR("wired.error.user_cannot_be_disconnected"), message);
		
		return;
	}
	
	disconnect_message	= wi_p7_message_string_for_name(message, WI_STR("wired.user.disconnect_message"));
	expiration_date		= wi_p7_message_date_for_name(message, WI_STR("wired.banlist.expiration_date"));
	enumerator			= wi_array_data_enumerator(wd_chats_chats_with_user(user));

	while((chat = wi_enumerator_next_data(enumerator))) {
		broadcast = wi_p7_message_with_name(WI_STR("wired.chat.user_ban"), wd_p7_spec);
		wi_p7_message_set_uint32_for_name(broadcast, wd_chat_id(chat), WI_STR("wired.chat.id"));
		wi_p7_message_set_uint32_for_name(broadcast, uid, WI_STR("wired.user.disconnected_id"));
		wi_p7_message_set_string_for_name(broadcast, disconnect_message, WI_STR("wired.user.disconnect_message"));
		wd_chat_broadcast_message(wd_public_chat, broadcast);
	}
	
	if(wd_banlist_add_ban(wd_user_ip(peer), expiration_date, user, message)) {
		wi_log_info(WI_STR("%@ banned %@"),
			wd_user_identifier(user),
			wd_user_identifier(peer));
		
		wd_user_set_state(peer, WD_USER_DISCONNECTED);
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_user_get_users(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_user_get_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wd_users_reply_users(user, message);
}



static void wd_message_chat_join_chat(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wd_chat_t			*chat;
	wi_p7_uint32_t		cid;
	
	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));
	
	chat = wd_chats_chat_with_id(cid);
	
	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);
		
		return;
	}
	
	if(wd_chat_contains_user(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.already_on_chat"), message);
		
		return;
	}
	
	if(chat != wd_public_chat && !wd_chat_is_user_invited(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_invited_to_chat"), message);
		
		return;
	}

	wd_chat_add_user_and_broadcast(chat, user);
	wd_chat_remove_invitation_for_user(chat, user);
	wd_chat_reply_user_list(chat, user, message);

	reply = wd_chat_topic_message(chat);
	
	if(reply)
		wd_user_reply_message(user, reply, message);
}



static void wd_message_chat_leave_chat(wd_user_t *user, wi_p7_message_t *message) {
	wd_chat_t			*chat;
	wi_p7_uint32_t		cid;

	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));

	chat = wd_chats_chat_with_id(cid);

	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);
		
		return;
	}
	
	if(!wd_chat_contains_user(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_on_chat"), message);
		
		return;
	}

	wd_chat_remove_user(chat, user);
	wd_chat_broadcast_user_leave(chat, user);

	wd_user_reply_okay(user, message);
}



static void wd_message_chat_kick_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*disconnect_message;
	wd_user_t			*peer;
	wd_chat_t			*chat;
	wi_p7_uint32_t		uid, cid;
	
	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));
	
	peer = wd_users_user_with_id(uid);
	
	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);
		
		return;
	}
	
	chat = wd_chats_chat_with_id(cid);
	
	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);
		
		return;
	}
	
	if(!wd_chat_contains_user(chat, user) || !wd_chat_contains_user(chat, peer)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_on_chat"), message);
		
		return;
	}
	
	if(chat == wd_public_chat && !wd_account_chat_kick_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	disconnect_message = wi_p7_message_string_for_name(message, WI_STR("wired.user.disconnect_message"));
	
	broadcast = wi_p7_message_with_name(WI_STR("wired.chat.user_kick"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(broadcast, cid, WI_STR("wired.chat.id"));
	wi_p7_message_set_uint32_for_name(broadcast, wd_user_id(user), WI_STR("wired.user.id"));
	wi_p7_message_set_uint32_for_name(broadcast, uid, WI_STR("wired.user.disconnected_id"));
	wi_p7_message_set_string_for_name(broadcast, disconnect_message, WI_STR("wired.user.disconnect_message"));
	wd_chat_broadcast_message(chat, broadcast);
	
	wd_chat_remove_user(chat, peer);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_chat_set_topic(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*topic;
	wd_chat_t			*chat;
	wi_p7_uint32_t		cid;
	
	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));
	
	chat = wd_chats_chat_with_id(cid);
	
	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);
		
		return;
	}
	
	if(!wd_chat_contains_user(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_on_chat"), message);
		
		return;
	}

	if(chat == wd_public_chat && !wd_account_chat_set_topic(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	topic = wi_p7_message_string_for_name(message, WI_STR("wired.chat.topic.topic"));
	
	wd_chat_set_topic(chat, wd_topic_with_user_and_string(user, topic));
	wd_chat_broadcast_message(chat, wd_chat_topic_message(chat));
	
	wd_user_reply_okay(user, message);
}



static void wd_message_chat_send_say_or_me(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_array_t			*array;
	wi_string_t			*name, *string, *line;
	wd_chat_t			*chat;
	wi_p7_uint32_t		cid;
	wi_uinteger_t		i, count;

	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));

	chat = wd_chats_chat_with_id(cid);
	
	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);
		
		return;
	}
	
	if(!wd_chat_contains_user(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_on_chat"), message);
		
		return;
	}
	
	if(wi_is_equal(wi_p7_message_name(message), WI_STR("wired.chat.send_say")))
		name = WI_STR("wired.chat.say");
	else
		name = WI_STR("wired.chat.me");

	string = wi_p7_message_string_for_name(message, name);
	array = wi_string_components_separated_by_string(string, WI_STR("\n\r"));
	count = wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		line = WI_ARRAY(array, i);
		
		if(wi_string_length(line) > 0) {
			broadcast = wi_p7_message_with_name(name, wd_p7_spec);
			wi_p7_message_set_string_for_name(broadcast, line, name);
			wi_p7_message_set_uint32_for_name(broadcast, cid, WI_STR("wired.chat.id"));
			wi_p7_message_set_uint32_for_name(broadcast, wd_user_id(user), WI_STR("wired.user.id"));
			wd_chat_broadcast_message(chat, broadcast);
		}
	}
	
	wd_user_reply_okay(user, message);
}



static void wd_message_chat_create_chat(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wd_chat_t			*chat;

	if(!wd_account_chat_create_chats(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
		return;
	}

	chat = wd_chat_private_chat();
	
	wd_chat_add_invitation_for_user(chat, user);
	
	wd_chats_add_chat(chat);

	reply = wi_p7_message_with_name(WI_STR("wired.chat.chat_created"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, wd_chat_id(chat), WI_STR("wired.chat.id"));
	wd_user_reply_message(user, reply, message);
}



static void wd_message_chat_invite_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wd_user_t			*peer;
	wd_chat_t			*chat;
	wi_p7_uint32_t		uid, cid;
	
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));

	peer = wd_users_user_with_id(uid);

	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);

		return;
	}

	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));

	chat = wd_chats_chat_with_id(cid);
	
	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);
		
		return;
	}
	
	if(!wd_chat_contains_user(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_on_chat"), message);
		
		return;
	}

	if(wd_chat_contains_user(chat, peer)) {
		wd_user_reply_error(user, WI_STR("wired.error.already_on_chat"), message);
		
		return;
	}
	
	wd_chat_add_invitation_for_user(chat, user);

	reply = wi_p7_message_with_name(WI_STR("wired.chat.invitation"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, wd_user_id(user), WI_STR("wired.user.id"));
	wi_p7_message_set_uint32_for_name(reply, cid, WI_STR("wired.chat.id"));
	wd_user_send_message(peer, reply);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_chat_decline_invitation(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wd_user_t			*peer;
	wd_chat_t			*chat;
	wi_p7_uint32_t		uid, cid;
	
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));

	peer = wd_users_user_with_id(uid);

	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);

		return;
	}

	wi_p7_message_get_uint32_for_name(message, &cid, WI_STR("wired.chat.id"));

	chat = wd_chats_chat_with_id(cid);
	
	if(!chat) {
		wd_user_reply_error(user, WI_STR("wired.error.chat_not_found"), message);

		return;
	}
	
	if(chat != wd_public_chat && !wd_chat_is_user_invited(chat, user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_invited_to_chat"), message);
		
		return;
	}

	reply = wi_p7_message_with_name(WI_STR("wired.chat.user_decline_invitation"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, wd_user_id(user), WI_STR("wired.user.id"));
	wi_p7_message_set_uint32_for_name(reply, cid, WI_STR("wired.chat.id"));
	wd_user_send_message(peer, reply);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_message_send_message(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_string_t			*string;
	wd_user_t			*peer;
	wi_p7_uint32_t		uid;
	
	if(!wd_account_message_send_messages(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wi_p7_message_get_uint32_for_name(message, &uid, WI_STR("wired.user.id"));
	
	peer = wd_users_user_with_id(uid);
	
	if(!peer) {
		wd_user_reply_error(user, WI_STR("wired.error.user_not_found"), message);
		
		return;
	}
	
	string = wi_p7_message_string_for_name(message, WI_STR("wired.message.message"));
	reply = wi_p7_message_with_name(WI_STR("wired.message.message"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, wd_user_id(user), WI_STR("wired.user.id"));
	wi_p7_message_set_string_for_name(reply, string, WI_STR("wired.message.message"));
	wd_user_send_message(peer, reply);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_message_send_broadcast(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*broadcast;
	wi_string_t			*string;
	
	if(!wd_account_message_broadcast(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	string = wi_p7_message_string_for_name(message, WI_STR("wired.message.broadcast"));
	broadcast = wi_p7_message_with_name(WI_STR("wired.message.broadcast"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(broadcast, wd_user_id(user), WI_STR("wired.user.id"));
	wi_p7_message_set_string_for_name(broadcast, string, WI_STR("wired.message.broadcast"));
	wd_broadcast_message(broadcast);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_board_get_boards(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_board_read_boards(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when listing boards"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wd_boards_reply_boards(user, message);
}



static void wd_message_board_get_posts(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_board_read_boards(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when listing posts"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wd_boards_reply_posts(user, message);
}



static void wd_message_board_add_board(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*board;
	wd_board_privileges_t	*privileges;
	
	if(!wd_account_board_add_boards(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when creating a board"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when creating board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	privileges = wd_board_privileges_with_message(message);
	
	if(wd_boards_add_board(board, privileges, user, message)) {
		wi_log_info(WI_STR("%@ added board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_rename_board(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*oldboard, *newboard;
	
	if(!wd_account_board_rename_boards(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when renaming a board"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	oldboard = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	newboard = wi_p7_message_string_for_name(message, WI_STR("wired.board.new_board"));
	
	if(!wd_boards_board_is_valid(oldboard) || !wd_boards_board_is_valid(newboard)) {
		wi_log_warn(WI_STR("Board not found for %@ when renaming board \"%@\" to \"%@\""),
			wd_user_identifier(user), oldboard, newboard);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	if(!wi_is_equal(wi_string_by_deleting_last_path_component(oldboard), wi_string_by_deleting_last_path_component(newboard))) {
		wi_log_warn(WI_STR("Board not found for %@ when renaming board \"%@\" to \"%@\""),
			wd_user_identifier(user), oldboard, newboard);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	if(wd_boards_rename_board(oldboard, newboard, user, message)) {
		wi_log_info(WI_STR("%@ renamed board \"%@\" to \"%@\""),
			wd_user_identifier(user),
			oldboard,
			newboard);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_move_board(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*oldboard, *newboard;
	
	if(!wd_account_board_move_boards(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when moving a board"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	oldboard = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	newboard = wi_p7_message_string_for_name(message, WI_STR("wired.board.new_board"));
	
	if(!wd_boards_board_is_valid(oldboard) || !wd_boards_board_is_valid(newboard)) {
		wi_log_warn(WI_STR("Board not found for %@ when moving board \"%@\" to \"%@\""),
			wd_user_identifier(user), oldboard, newboard);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	if(wd_boards_move_board(oldboard, newboard, user, message)) {
		wi_log_info(WI_STR("%@ moved board \"%@\" to \"%@\""),
			wd_user_identifier(user),
			oldboard,
			newboard);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_delete_board(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*board;
	
	if(!wd_account_board_delete_boards(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when deleting a board"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when deleting board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	if(wd_boards_delete_board(board, user, message)) {
		wi_log_info(WI_STR("%@ deleted board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_set_permissions(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*board;
	wd_board_privileges_t	*privileges;
	
	if(!wd_account_board_set_permissions(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when setting board permissions"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when setting permissions for board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	privileges = wd_board_privileges_with_message(message);

	if(wd_boards_set_board_privileges(board, privileges, user, message)) {
		wi_log_info(WI_STR("%@ changed permissions for board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_add_thread(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*board, *subject, *text;
	
	if(!wd_account_board_add_threads(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when creating a thread"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when creating a thread in board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	subject		= wi_p7_message_string_for_name(message, WI_STR("wired.board.subject"));
	text		= wi_p7_message_string_for_name(message, WI_STR("wired.board.text"));

	if(wd_boards_add_thread(board, subject, text, user, message)) {
		wi_log_info(WI_STR("%@ created a thread in board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_move_thread(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*oldboard, *newboard;
	wi_uuid_t		*thread;
	
	if(!wd_account_board_move_threads(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when moving a thread"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	oldboard = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	newboard = wi_p7_message_string_for_name(message, WI_STR("wired.board.new_board"));
	
	if(!wd_boards_board_is_valid(oldboard) || !wd_boards_board_is_valid(newboard)) {
		wi_log_warn(WI_STR("Board not found for %@ when moving a thread from board \"%@\" to \"%@\""),
			wd_user_identifier(user), oldboard, newboard);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	thread = wi_p7_message_uuid_for_name(message, WI_STR("wired.board.thread"));
	
	if(wd_boards_move_thread(oldboard, thread, newboard, user, message)) {
		wi_log_info(WI_STR("%@ moved a thread from board \"%@\" to \"%@\""),
			wd_user_identifier(user),
			oldboard, newboard);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_delete_thread(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*board;
	wi_uuid_t		*thread;
	
	if(!wd_account_board_delete_threads(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when deleting a thread"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when deleting a thread in board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	thread = wi_p7_message_uuid_for_name(message, WI_STR("wired.board.thread"));
	
	if(wd_boards_delete_thread(board, thread, user, message)) {
		wi_log_info(WI_STR("%@ deleted a thread from board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_add_post(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*board, *subject, *text;
	wi_uuid_t		*thread;
	
	if(!wd_account_board_add_posts(wd_user_account(user))) {
		wi_log_warn(WI_STR("Permission denied for %@ when creating a post"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when creating a post in board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	thread		= wi_p7_message_uuid_for_name(message, WI_STR("wired.board.thread"));
	subject		= wi_p7_message_string_for_name(message, WI_STR("wired.board.subject"));
	text		= wi_p7_message_string_for_name(message, WI_STR("wired.board.text"));

	if(wd_boards_add_post(board, thread, subject, text, user, message)) {
		wi_log_info(WI_STR("%@ created a post in board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_edit_post(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*board, *subject, *text;
	wi_uuid_t		*thread, *post;
	wd_account_t	*account;
	
	account = wd_user_account(user);
	
	if(!wd_account_board_edit_own_posts(account) && !wd_account_board_edit_all_posts(account)) {
		wi_log_warn(WI_STR("Permission denied for %@ when editing a post"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when editing a post in board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	thread		= wi_p7_message_uuid_for_name(message, WI_STR("wired.board.thread"));
	post		= wi_p7_message_uuid_for_name(message, WI_STR("wired.board.post"));
	subject		= wi_p7_message_string_for_name(message, WI_STR("wired.board.subject"));
	text		= wi_p7_message_string_for_name(message, WI_STR("wired.board.text"));
	
	if(wd_boards_edit_post(board, thread, post, subject, text, user, message)) {
		wi_log_info(WI_STR("%@ edited a post in board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_delete_post(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*board;
	wi_uuid_t		*thread, *post;
	wd_account_t	*account;
	
	account = wd_user_account(user);

	if(!wd_account_board_edit_own_posts(account) && !wd_account_board_edit_all_posts(account)) {
		wi_log_warn(WI_STR("Permission denied for %@ when deleting a post"),
			wd_user_identifier(user));
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	board = wi_p7_message_string_for_name(message, WI_STR("wired.board.board"));
	
	if(!wd_boards_board_is_valid(board)) {
		wi_log_warn(WI_STR("Board not found for %@ when deleting a post in board \"%@\""),
			wd_user_identifier(user), board);
		wd_user_reply_error(user, WI_STR("wired.error.board_not_found"), message);
		
		return;
	}
	
	thread		= wi_p7_message_uuid_for_name(message, WI_STR("wired.board.thread"));
	post		= wi_p7_message_uuid_for_name(message, WI_STR("wired.board.post"));
	
	if(wd_boards_delete_post(board, thread, post, user, message)) {
		wi_log_info(WI_STR("%@ deleted a post in board \"%@\""),
			wd_user_identifier(user),
			board);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_board_subscribe_boards(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_board_read_boards(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(wd_user_is_subscribed_boards(user)) {
		wd_user_reply_error(user, WI_STR("wired.error.already_subscribed"), message);
		
		return;
	}
	
	wd_user_subscribe_boards(user);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_board_unsubscribe_boards(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_board_read_boards(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(!wd_user_is_subscribed_boards(user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_subscribed"), message);
		
		return;
	}
	
	wd_user_unsubscribe_boards(user);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_file_list_directory(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*path;
	wi_p7_boolean_t		recursive;
	
	if(!wd_account_file_list_files(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));
	
	if(!wi_p7_message_get_bool_for_name(message, &recursive, WI_STR("wired.file.recursive")))
		recursive = false;
	
	wd_files_reply_list(path, recursive, user, message);
}



static void wd_message_file_get_info(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	
	account = wd_user_account(user);
	
	if(!wd_account_file_get_info(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	privileges = wd_files_privileges(path, user);
	
	if(privileges && !wd_files_privileges_is_readable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	wd_files_reply_info(wi_string_by_normalizing_path(path), user, message);
}



static void wd_message_file_move(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*frompath, *topath;
	wi_string_t				*properfrompath, *propertopath;
	wi_string_t				*fromdirectory, *todirectory;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	
	frompath	= wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));
	topath		= wi_p7_message_string_for_name(message, WI_STR("wired.file.new_path"));

	if(!wd_files_path_is_valid(frompath) || !wd_files_path_is_valid(topath)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);
		
		return;
	}
	
	account = wd_user_account(user);
	
	if(!wd_account_file_move_files(account)) {
		fromdirectory	= wi_string_by_deleting_last_path_component(frompath);
		todirectory		= wi_string_by_deleting_last_path_component(topath);

		if(!wd_account_file_rename_files(account) || !wi_is_equal(fromdirectory, todirectory)) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
			return;
		}
	}

	properfrompath	= wi_string_by_normalizing_path(frompath);
	privileges		= wd_files_privileges(properfrompath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	propertopath	= wi_string_by_normalizing_path(topath);
	privileges		= wd_files_privileges(propertopath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(wd_files_move_path(properfrompath, propertopath, user, message)) {
		wd_files_index_delete_file(properfrompath);
		wd_files_index_add_file(propertopath);

		wi_log_info(WI_STR("%@ moved \"%@\" to \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properfrompath, user),
			wd_files_virtual_path(propertopath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_link(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*frompath, *topath;
	wi_string_t				*properfrompath, *propertopath;
	wd_files_privileges_t	*privileges;
	wd_account_t			*account;
	
	frompath	= wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));
	topath		= wi_p7_message_string_for_name(message, WI_STR("wired.file.new_path"));

	if(!wd_files_path_is_valid(frompath) || !wd_files_path_is_valid(topath)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);
		
		return;
	}
	
	account = wd_user_account(user);

	if(!wd_account_file_create_links(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
		return;
	}


	properfrompath	= wi_string_by_normalizing_path(frompath);
	privileges		= wd_files_privileges(properfrompath, user);
	
	if(privileges && !wd_files_privileges_is_readable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
		return;
	}

	propertopath	= wi_string_by_normalizing_path(topath);
	privileges		= wd_files_privileges(propertopath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
		return;
	}

	if(wd_files_link_path(properfrompath, propertopath, user, message)) {
		wi_log_info(WI_STR("%@ linked \"%@\" to \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properfrompath, user),
			wd_files_virtual_path(propertopath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_set_type(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wd_file_type_t			type;
	
	account = wd_user_account(user);

	if(!wd_account_file_set_type(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(properpath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	wi_p7_message_get_enum_for_name(message, &type, WI_STR("wired.file.type"));

	if(wd_files_set_type(properpath, type, user, message)) {
		wi_log_info(WI_STR("%@ changed type of \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_set_comment(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath, *comment;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wi_boolean_t			okay;
	
	account = wd_user_account(user);

	if(!wd_account_file_set_comment(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}

	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(properpath, user);

	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	comment = wi_p7_message_string_for_name(message, WI_STR("wired.file.comment"));

	if(wi_string_length(comment) > 0)
		okay = wd_files_set_comment(properpath, comment, user, message);
	else
		okay = wd_files_remove_comment(properpath, user, message);
	
	if(okay) {
		wi_log_info(WI_STR("%@ changed comment of \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_set_executable(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wi_p7_boolean_t			executable;
	
	account = wd_user_account(user);
	
	if(!wd_account_file_set_executable(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}

	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(properpath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}
	
	wi_p7_message_get_bool_for_name(message, &executable, WI_STR("wired.file.executable"));

	if(wd_files_set_executable(properpath, executable, user, message)) {
		wi_log_info(WI_STR("%@ changed executable mode of \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_set_permissions(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	
	account = wd_user_account(user);

	if(!wd_account_file_set_permissions(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(properpath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}
	
	privileges = wd_files_privileges_with_message(message);
	
	if(wd_files_set_privileges(properpath, privileges, user, message)) {
		wi_log_info(WI_STR("%@ changed permissions of \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_set_label(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wi_p7_enum_t			label;
	wi_boolean_t			okay;
	
	account = wd_user_account(user);
	
	if(!wd_account_file_set_label(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}

	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(properpath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}
	
	wi_p7_message_get_enum_for_name(message, &label, WI_STR("wired.file.label"));

	if(label != WD_FILE_LABEL_NONE)
		okay = wd_files_set_label(properpath, label, user, message);
	else
		okay = wd_files_remove_label(properpath, user, message);
	
	if(okay) {
		wi_log_info(WI_STR("%@ changed label of \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_delete(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;

	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}

	account			= wd_user_account(user);
	properpath		= wi_string_by_normalizing_path(path);
	privileges		= wd_files_privileges(properpath, user);
	
	if(privileges) {
		if(!wd_files_privileges_is_readable_and_writable_by_account(privileges, account)) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

			return;
		}
	} else {
		if(!wd_account_file_delete_files(account)) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

			return;
		}
	}
	
	if(wd_files_delete_path(properpath, user, message)) {
		wd_files_index_delete_file(properpath);
		
		wi_log_info(WI_STR("%@ deleted \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));

		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_create_directory(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wd_file_type_t			type;
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	account		= wd_user_account(user);
	properpath	= wi_string_by_normalizing_path(path);
	privileges	= wd_files_privileges(properpath, user);
	
	if(privileges) {
		if(!wd_files_privileges_is_writable_by_account(privileges, account)) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

			return;
		}
	} else {
		if(!wd_account_file_create_directories(account)) {
			wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
			
			return;
		}
	}
	
	if(!wi_p7_message_get_enum_for_name(message, &type, WI_STR("wired.file.type")))
		type = WD_FILE_TYPE_DIR;

	if(wd_files_create_path(properpath, type, user, message)) {
		if(type == WD_FILE_TYPE_DROPBOX && wd_account_file_set_permissions(account)) {
			privileges = wd_files_privileges_with_message(message);
			
			if(!wd_files_set_privileges(properpath, privileges, user, message)) {
				wi_fs_delete_path(properpath);
				
				return;
			}
		}
		
		wi_log_info(WI_STR("%@ created \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_file_search(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*query;
	
	if(!wd_account_file_search_files(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	query = wi_p7_message_string_for_name(message, WI_STR("wired.file.query"));
	
	wd_files_search(query, user, message);
}



static void wd_message_file_preview_file(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	
	account = wd_user_account(user);
	
	if(!wd_account_transfer_download_files(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	privileges = wd_files_privileges(path, user);
	
	if(privileges && !wd_files_privileges_is_readable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	wd_files_reply_preview(wi_string_by_normalizing_path(path), user, message);
}



static void wd_message_file_subscribe_directory(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*path, *realpath;
	
	if(!wd_account_file_list_files(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	path		= wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_stat_path(realpath, NULL)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);
		
		return;
	}
	
	wd_user_subscribe_path(user, realpath);
	wd_user_reply_okay(user, message);
}



static void wd_message_file_unsubscribe_directory(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*path, *realpath;
	
	if(!wd_account_file_list_files(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	path		= wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_stat_path(realpath, NULL)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);
		
		return;
	}

	if(!wi_set_contains_data(wd_user_subscribed_paths(user), realpath)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_subscribed"), message);
		
		return;
	}
	
	wd_user_unsubscribe_path(user, realpath);
	wd_user_reply_okay(user, message);
}



static void wd_message_account_change_password(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*password;
	wd_account_t	*account;
	
	account = wd_user_account(user);
	
	if(!wd_account_account_change_password(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	password = wi_p7_message_string_for_name(message, WI_STR("wired.account.password"));
	
	if(wd_accounts_change_password(account, password, user, message)) {
		wi_log_info(WI_STR("%@ changed password"),
			wd_user_identifier(user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_account_list_users(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_account_list_accounts(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wd_accounts_reply_user_list(user, message);
}



static void wd_message_account_list_groups(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_account_list_accounts(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wd_accounts_reply_group_list(user, message);
}



static void wd_message_account_read_user(wd_user_t *user, wi_p7_message_t *message) {
	wd_account_t		*account;

	if(!wd_account_account_read_accounts(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	account = wd_accounts_read_user(wi_p7_message_string_for_name(message, WI_STR("wired.account.name")));
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_not_found"), message);
		
		return;
	}
	
	wd_account_reply_user_account(account, user, message);
}



static void wd_message_account_read_group(wd_user_t *user, wi_p7_message_t *message) {
	wd_account_t		*account;

	if(!wd_account_account_read_accounts(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	account = wd_accounts_read_group(wi_p7_message_string_for_name(message, WI_STR("wired.account.name")));
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_not_found"), message);
		
		return;
	}
	
	wd_account_reply_group_account(account, user, message);
}



static void wd_message_account_create_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*error;
	wd_account_t		*account;

	if(!wd_account_account_create_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	account = wd_accounts_read_user(wi_p7_message_string_for_name(message, WI_STR("wired.account.name")));
	
	if(account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_exists"), message);
		
		return;
	}
	
	account = wi_autorelease(wd_account_init_with_message(wd_account_alloc(), message));
	
	if(wi_string_length(wd_account_name(account)) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.invalid_message"), message);
		
		return;
	}
	
	if(!wd_account_verify_privileges_for_user(account, user, &error)) {
		wi_log_warn(WI_STR("Permission denied for %@ when creating user \"%@\": %@"),
			wd_user_identifier(user), wd_account_name(account), error);
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	if(wd_accounts_create_user(account, user, message)) {
		wi_log_info(WI_STR("%@ created the user \"%@\""),
			wd_user_identifier(user),
			wd_account_name(account));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_account_create_group(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*error;
	wd_account_t		*account;

	if(!wd_account_account_create_groups(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	account = wd_accounts_read_group(wi_p7_message_string_for_name(message, WI_STR("wired.account.name")));
	
	if(account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_exists"), message);
		
		return;
	}
	
	account = wi_autorelease(wd_account_init_with_message(wd_account_alloc(), message));
	
	if(wi_string_length(wd_account_name(account)) == 0) {
		wd_user_reply_error(user, WI_STR("wired.error.invalid_message"), message);
		
		return;
	}
	
	if(!wd_account_verify_privileges_for_user(account, user, &error)) {
		wi_log_warn(WI_STR("Permission denied for %@ when creating group \"%@\": %@"),
			wd_user_identifier(user), wd_account_name(account), error);
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(wd_accounts_create_group(account, user, message)) {
		wi_log_info(WI_STR("%@ created the group \"%@\""),
			wd_user_identifier(user),
			wd_account_name(account));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_account_edit_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*name, *new_name, *error;
	wd_account_t	*account;

	if(!wd_account_account_edit_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	name		= wi_p7_message_string_for_name(message, WI_STR("wired.account.name"));
	new_name	= wi_p7_message_string_for_name(message, WI_STR("wired.account.new_name"));
	
	if(new_name && !wi_is_equal(name, new_name)) {
		account = wd_accounts_read_user(wi_p7_message_string_for_name(message, new_name));
		
		if(account) {
			wd_user_reply_error(user, WI_STR("wired.error.account_exists"), message);
			
			return;
		}
	}
	
	account = wd_accounts_read_user(name);
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_not_found"), message);
		
		return;
	}
	
	wd_account_update_from_message(account, message);
	
	if(!wd_account_verify_privileges_for_user(account, user, &error)) {
		wi_log_warn(WI_STR("Permission denied for %@ when editing user \"%@\": %@"),
			wd_user_identifier(user), wd_account_name(account), error);
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	if(wd_accounts_edit_user(account, user, message)) {
		wd_boards_rename_owner(name, new_name);
		
		wi_log_info(WI_STR("%@ modified the user \"%@\""),
			wd_user_identifier(user),
			wd_account_name(account));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_account_edit_group(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*name, *new_name, *error;
	wd_account_t	*account;

	if(!wd_account_account_edit_groups(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	name		= wi_p7_message_string_for_name(message, WI_STR("wired.account.name"));
	new_name	= wi_p7_message_string_for_name(message, WI_STR("wired.account.new_name"));
	
	if(new_name && !wi_is_equal(name, new_name)) {
		account = wd_accounts_read_group(wi_p7_message_string_for_name(message, new_name));
		
		if(account) {
			wd_user_reply_error(user, WI_STR("wired.error.account_exists"), message);
			
			return;
		}
	}
	
	account = wd_accounts_read_group(name);
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_not_found"), message);
		
		return;
	}
	
	wd_account_update_from_message(account, message);
	
	if(!wd_account_verify_privileges_for_user(account, user, &error)) {
		wi_log_warn(WI_STR("Permission denied for %@ when editing group \"%@\": %@"),
			wd_user_identifier(user), wd_account_name(account), error);
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	if(wd_accounts_edit_group(account, user, message)) {
		wd_boards_rename_group(name, new_name);
		
		wi_log_info(WI_STR("%@ modified the group \"%@\""),
			wd_user_identifier(user),
			wd_account_name(account));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_account_delete_user(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_array_t			*users;
	wi_string_t			*name;
	wd_account_t		*account;
	wd_user_t			*peer;
	wi_p7_boolean_t		disconnect_users;
	
	wi_p7_message_get_bool_for_name(message, &disconnect_users, WI_STR("wired.account.disconnect_users"));
	
	if(!wd_account_account_delete_users(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	name = wi_p7_message_string_for_name(message, WI_STR("wired.account.name"));
	account = wd_accounts_read_user(name);
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_not_found"), message);
		
		return;
	}
	
	users = wd_users_users_with_login(name);
	
	if(!disconnect_users && wi_array_count(users) > 0) {
		wd_user_reply_error(user, WI_STR("wired.error.account_in_use"), message);
		
		return;
	}

	if(wd_accounts_delete_user(name, user, message)) {
		wi_log_info(WI_STR("%@ deleted the user \"%@\""),
			wd_user_identifier(user),
			name);
	
		wd_user_reply_okay(user, message);
	
		enumerator = wi_array_data_enumerator(users);
	
		while((peer = wi_enumerator_next_data(enumerator)))
			wd_user_set_state(peer, WD_USER_DISCONNECTED);
	}
}



static void wd_message_account_delete_group(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*name;
	wd_account_t	*account;
	
	if(!wd_account_account_delete_groups(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	name = wi_p7_message_string_for_name(message, WI_STR("wired.account.name"));
	account = wd_accounts_read_group(name);
	
	if(!account) {
		wd_user_reply_error(user, WI_STR("wired.error.account_not_found"), message);
		
		return;
	}

	if(wd_accounts_delete_group(name, user, message)) {
		wi_log_info(WI_STR("%@ deleted the group \"%@\""),
			wd_user_identifier(user),
			name);
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_account_subscribe_accounts(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_account_list_accounts(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(wd_user_is_subscribed_accounts(user)) {
		wd_user_reply_error(user, WI_STR("wired.error.already_subscribed"), message);
		
		return;
	}

	wd_user_subscribe_accounts(user);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_account_unsubscribe_accounts(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_account_list_accounts(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(!wd_user_is_subscribed_accounts(user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_subscribed"), message);
		
		return;
	}
	
	wd_user_unsubscribe_accounts(user);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_transfer_download_file(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wd_transfer_t			*transfer;
	wi_p7_uint64_t			dataoffset, rsrcoffset;
	
	account = wd_user_account(user);
	
	if(!wd_account_transfer_download_files(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(path, user);
	
	if(privileges && !wd_files_privileges_is_readable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}

	wi_p7_message_get_uint64_for_name(message, &dataoffset, WI_STR("wired.transfer.data_offset"));
	wi_p7_message_get_uint64_for_name(message, &rsrcoffset, WI_STR("wired.transfer.rsrc_offset"));
	
	transfer = wd_transfer_download_transfer(properpath, dataoffset, rsrcoffset, user, message);
	
	if(transfer) {
		wd_user_set_transfer(user, transfer);
		
		if(!wd_transfers_run_transfer(transfer, user, message))
			wd_user_set_state(user, WD_USER_DISCONNECTED);
		
		wd_user_set_transfer(user, NULL);
	}
}



static void wd_message_transfer_upload_file(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *realpath, *realparentpath, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wd_transfer_t			*transfer;
	wi_file_offset_t		datasize, rsrcsize;
	wi_boolean_t			executable;
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}
	
	account		= wd_user_account(user);
	properpath	= wi_string_by_normalizing_path(path);
	privileges	= wd_files_privileges(properpath, user);
	
	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	realpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(properpath, user));
	realparentpath	= wi_string_by_deleting_last_path_component(realpath);

	switch(wd_files_type(realparentpath)) {
		case WD_FILE_TYPE_UPLOADS:
		case WD_FILE_TYPE_DROPBOX:
			if(!wd_account_transfer_upload_files(account)) {
				wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

				return;
			}
			break;

		default:
			if(!wd_account_transfer_upload_anywhere(account)) {
				wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

				return;
			}
			break;
	}
	
	wi_p7_message_get_uint64_for_name(message, &datasize, WI_STR("wired.transfer.data_size"));
	
	if(!wi_p7_message_get_uint64_for_name(message, &rsrcsize, WI_STR("wired.transfer.rsrc_size")))
		rsrcsize = 0;
	
	if(!wi_p7_message_get_bool_for_name(message, &executable, WI_STR("wired.file.executable")))
		executable = false;

	transfer = wd_transfer_upload_transfer(properpath, datasize, rsrcsize, executable, user, message);
	
	if(transfer) {
		wd_user_set_transfer(user, transfer);
		
		if(!wd_transfers_run_transfer(transfer, user, message))
			wd_user_set_state(user, WD_USER_DISCONNECTED);

		wd_user_set_transfer(user, NULL);
	}
}



static void wd_message_transfer_upload_directory(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t				*path, *realparentpath, *realpath, *properpath;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges;
	wd_file_type_t			parenttype;
	
	account = wd_user_account(user);
	
	if(!wd_account_transfer_upload_directories(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}
	
	path = wi_p7_message_string_for_name(message, WI_STR("wired.file.path"));

	if(!wd_files_path_is_valid(path)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_not_found"), message);

		return;
	}

	properpath = wi_string_by_normalizing_path(path);
	privileges = wd_files_privileges(properpath, user);

	if(privileges && !wd_files_privileges_is_writable_by_account(privileges, account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	realpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(properpath, user));
	realparentpath	= wi_string_by_deleting_last_path_component(realpath);
	parenttype		= wd_files_type(realparentpath);
	
	if(parenttype == WD_FILE_TYPE_DIR && !wd_account_transfer_upload_anywhere(account)) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);

		return;
	}

	if(wd_files_create_path(properpath, parenttype, user, message)) {
		wi_log_info(WI_STR("%@ uploaded \"%@\""),
			wd_user_identifier(user),
			wd_files_virtual_path(properpath, user));
		
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_log_get_log(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_log_view_log(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	wd_server_log_reply_log(user, message);
}



static void wd_message_log_subscribe(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_log_view_log(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	if(wd_user_is_subscribed_log(user)) {
		wd_user_reply_error(user, WI_STR("wired.error.already_subscribed"), message);
		
		return;
	}

	wd_user_subscribe_log(user);
	
	wd_user_reply_okay(user, message);
}



static void wd_message_log_unsubscribe(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_log_view_log(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	if(!wd_user_is_subscribed_log(user)) {
		wd_user_reply_error(user, WI_STR("wired.error.not_subscribed"), message);
		
		return;
	}

	wd_user_unsubscribe_log(user);

	wd_user_reply_okay(user, message);
}



static void wd_message_settings_get_settings(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_settings_get_settings(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	wd_settings_reply_settings(user, message);
}



static void wd_message_settings_set_settings(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_settings_set_settings(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	if(wd_settings_set_settings(user, message)) {
		wi_log_info(WI_STR("%@ changed server settings"), wd_user_identifier(user));

		wd_user_reply_okay(user, message);
	}
}



static void wd_message_banlist_get_bans(wd_user_t *user, wi_p7_message_t *message) {
	if(!wd_account_banlist_get_bans(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	wd_banlist_reply_bans(user, message);
}



static void wd_message_banlist_add_ban(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*ip;
	wi_date_t		*expiration_date;
	
	if(!wd_account_banlist_add_bans(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	ip					= wi_p7_message_string_for_name(message, WI_STR("wired.banlist.ip"));
	expiration_date		= wi_p7_message_date_for_name(message, WI_STR("wired.banlist.expiration_date"));
	
	if(wd_banlist_add_ban(ip, expiration_date, user, message)) {
		wi_log_info(WI_STR("%@ added ban for %@"), wd_user_identifier(user), ip);

		wd_user_reply_okay(user, message);
	}
}



static void wd_message_banlist_delete_ban(wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*ip;
	wi_date_t		*expiration_date;
	
	if(!wd_account_banlist_delete_bans(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
	 
		return;
	}

	ip					= wi_p7_message_string_for_name(message, WI_STR("wired.banlist.ip"));
	expiration_date		= wi_p7_message_date_for_name(message, WI_STR("wired.banlist.expiration_date"));
	
	if(wd_banlist_delete_ban(ip, expiration_date, user, message)) {
		wi_log_info(WI_STR("%@ deleted ban for %@"), wd_user_identifier(user), ip);
	
		wd_user_reply_okay(user, message);
	}
}



static void wd_message_tracker_get_categories(wd_user_t *user, wi_p7_message_t *message) {
	if(!wi_config_bool_for_name(wd_config, WI_STR("enable tracker"))) {
		wd_user_reply_error(user, WI_STR("wired.error.tracker_not_enabled"), message);
		
		return;
	}
	
	if(!wd_account_tracker_list_servers(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	wd_servers_reply_categories(user, message);
}



static void wd_message_tracker_get_servers(wd_user_t *user, wi_p7_message_t *message) {
	if(!wi_config_bool_for_name(wd_config, WI_STR("enable tracker"))) {
		wd_user_reply_error(user, WI_STR("wired.error.tracker_not_enabled"), message);
		
		return;
	}
	
	if(!wd_account_tracker_list_servers(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	wd_servers_reply_server_list(user, message);
}



static void wd_message_tracker_send_register(wd_user_t *user, wi_p7_message_t *message) {
	if(!wi_config_bool_for_name(wd_config, WI_STR("enable tracker"))) {
		wd_user_reply_error(user, WI_STR("wired.error.tracker_not_enabled"), message);
		
		return;
	}
	
	if(!wd_account_tracker_register_servers(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}

	wd_servers_register_server(user, message);
}



static void wd_message_tracker_send_update(wd_user_t *user, wi_p7_message_t *message) {
	if(!wi_config_bool_for_name(wd_config, WI_STR("enable tracker"))) {
		wd_user_reply_error(user, WI_STR("wired.error.tracker_not_enabled"), message);
		
		return;
	}
	
	if(!wd_account_tracker_register_servers(wd_user_account(user))) {
		wd_user_reply_error(user, WI_STR("wired.error.permission_denied"), message);
		
		return;
	}
	
	if(wd_servers_update_server(user, message))
		wd_user_reply_okay(user, message);
}
