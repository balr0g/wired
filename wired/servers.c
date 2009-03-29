/* $Id$ */

/*
 *  Copyright (c) 2004-2009 Axel Andersson
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

#include <stdlib.h>
#include <string.h>
#include <wired/wired.h>

#include "main.h"
#include "server.h"
#include "servers.h"
#include "settings.h"
#include "users.h"

#define WD_SERVERS_UPDATE_INTERVAL		60.0
#define WD_SERVERS_MIN_UPDATE_INTERVAL	300.0


struct _wd_server {
	wi_runtime_base_t					base;
	
	wi_uuid_t							*token;
	wi_time_interval_t					register_time;
	wi_time_interval_t					update_time;
	
	wi_boolean_t						tracker;
	wi_string_t							*category;
	wi_mutable_string_t					*url;
	wi_string_t							*ip;
	wi_p7_uint32_t						port;
	wi_string_t							*name;
	wi_string_t							*description;
	wi_p7_uint32_t						users;
	wi_p7_uint64_t						files_count;
	wi_p7_uint64_t						files_size;
};


static void								wd_servers_update_servers(wi_timer_t *);

static void								wd_servers_add_server(wd_server_t *);
static void								wd_servers_remove_server(wd_server_t *);
static wd_server_t *					wd_servers_server_equal_to_server(wd_server_t *);
static wd_server_t *					wd_servers_server_with_token(wi_uuid_t *);
static void								wd_servers_add_stats_for_server(wd_server_t *);
static void								wd_servers_remove_stats_for_server(wd_server_t *);

static wd_server_t *					wd_server_alloc(void);
static wd_server_t *					wd_server_init_with_message(wd_server_t *, wi_p7_message_t *);
static void								wd_server_dealloc(wi_runtime_instance_t *);


static wi_lock_t						*wd_servers_lock;
static wi_timer_t						*wd_servers_timer;

static wi_mutable_dictionary_t			*wd_servers;

static wi_runtime_id_t					wd_server_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_server_runtime_class = {
	"wd_server_t",
	wd_server_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};



void wd_servers_init(void) {
	wd_server_runtime_id = wi_runtime_register_class(&wd_server_runtime_class);

	wd_servers = wi_dictionary_init(wi_mutable_dictionary_alloc());
	
	wd_servers_lock = wi_lock_init(wi_lock_alloc());

	wd_servers_timer = wi_timer_init_with_function(wi_timer_alloc(),
												   wd_servers_update_servers,
												   WD_SERVERS_UPDATE_INTERVAL,
												   true);
}



void wd_servers_apply_settings(void) {
}



void wd_servers_schedule(void) {
	wi_timer_schedule(wd_servers_timer);
}



static void wd_servers_update_servers(wi_timer_t *timer) {
	wi_enumerator_t		*enumerator;
	wi_string_t			*token;
	wd_server_t			*server;
	wi_time_interval_t	interval, update;
	wi_boolean_t		changed = false;

	wi_dictionary_wrlock(wd_servers);
		
	if(wi_dictionary_count(wd_servers) > 0) {
		interval = wi_time_interval();

		enumerator = wi_array_data_enumerator(wi_dictionary_all_keys(wd_servers));
		
		while((token = wi_enumerator_next_data(enumerator))) {
			server = wi_dictionary_data_for_key(wd_servers, token);
			update = server->update_time > 0.0 ? server->update_time : server->register_time;
			
			if(interval - update > WD_SERVERS_MIN_UPDATE_INTERVAL) {
				if(server->update_time > 0.0) {
					wi_log_warn(WI_STR("Removing server \"%@\": Last update was %.0f seconds ago"),
						server->name, interval - update);
				} else {
					wi_log_warn(WI_STR("Removing server \"%@\": Never received received an update"),
						server->name);
				}

				wd_servers_remove_stats_for_server(server);
				wi_mutable_dictionary_remove_data_for_key(wd_servers, token);
				
				changed = true;
			}
		}
	}

	wi_dictionary_unlock(wd_servers);

	if(changed) {
		wi_lock_lock(wd_status_lock);
		wd_write_status(true);
		wi_lock_unlock(wd_status_lock);
	}
}



#pragma mark -

static void wd_servers_add_server(wd_server_t *server) {
	wi_dictionary_wrlock(wd_servers);
	wi_mutable_dictionary_set_data_for_key(wd_servers, server, server->token);
	wi_dictionary_unlock(wd_servers);
}



static void wd_servers_remove_server(wd_server_t *server) {
	wi_dictionary_wrlock(wd_servers);
	wi_mutable_dictionary_remove_data_for_key(wd_servers, server->token);
	wi_dictionary_unlock(wd_servers);
}



static wd_server_t * wd_servers_server_equal_to_server(wd_server_t *server) {
	wi_enumerator_t	*enumerator;
	wd_server_t		*peer, *value = NULL;

	wi_dictionary_rdlock(wd_servers);
	
	enumerator = wi_dictionary_data_enumerator(wd_servers);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wi_is_equal(server->ip, peer->ip) && server->port == peer->port &&
		   wi_is_equal(server->category, peer->category)) {
			value = wi_autorelease(wi_retain(peer));

			break;
		}
	}

	wi_dictionary_unlock(wd_servers);

	return value;
}



static wd_server_t * wd_servers_server_with_token(wi_uuid_t *token) {
	wd_server_t		*server;
	
	wi_dictionary_rdlock(wd_servers);
	server = wi_autorelease(wi_retain(wi_dictionary_data_for_key(wd_servers, token)));
	wi_dictionary_unlock(wd_servers);
	
	return server;
}



void wd_servers_add_stats_for_server(wd_server_t *server) {
	wi_lock_lock(wd_status_lock);
	wd_tracker_current_servers++;
	wd_tracker_current_users += server->users;
	wd_tracker_current_files += server->files_count;
	wd_tracker_current_size += server->files_size;
	wd_write_status(false);
	wi_lock_unlock(wd_status_lock);
}



void wd_servers_remove_stats_for_server(wd_server_t *server) {
	wi_lock_lock(wd_status_lock);
	wd_tracker_current_servers--;
	wd_tracker_current_users -= server->users;
	wd_tracker_current_files -= server->files_count;
	wd_tracker_current_size -= server->files_size;
	wi_lock_unlock(wd_status_lock);
}



#pragma mark -

void wd_servers_register_server(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_address_t		*address;
	wi_array_t			*categories;
	wd_server_t			*server, *existing_server;
	
	address					= wi_socket_address(wd_user_socket(user));
	server					= wi_autorelease(wd_server_init_with_message(wd_server_alloc(), message));
	server->url				= wi_string_init_with_format(wi_mutable_string_alloc(), WI_STR("wired://"));
	server->register_time	= wi_time_interval();
	
	if(!server->ip)
		server->ip = wi_retain(wi_address_string(address));
	
	if(wi_address_family(address) == WI_ADDRESS_IPV6)
		wi_mutable_string_append_format(server->url, WI_STR("[%@]"), server->ip);
	else
		wi_mutable_string_append_string(server->url,server->ip);
	
	if(server->port != WD_SERVER_PORT)
		wi_mutable_string_append_format(server->url, WI_STR(":%u/"), server->port);
	else
		wi_mutable_string_append_string(server->url, WI_STR("/"));
	
	categories = wi_config_stringlist_for_name(wd_config, WI_STR("category"));
	
	if(wi_string_length(server->category) > 0 && !wi_array_contains_data(categories, server->category)) {
		wi_release(server->category);
		server->category = wi_retain(WI_STR(""));
	}
	
	existing_server = wd_servers_server_equal_to_server(server);
	
	if(existing_server) {
		wd_servers_remove_server(existing_server);
		wd_servers_remove_stats_for_server(existing_server);
	}
	
	wd_servers_add_server(server);
	wd_servers_add_stats_for_server(server);
	
	wi_log_info(WI_STR("Registered server \"%@\""), server->name);
	
	reply = wi_p7_message_with_name(WI_STR("wired.tracker.register"), wd_p7_spec);
	wi_p7_message_set_uuid_for_name(reply, server->token, WI_STR("wired.tracker.token"));
	wd_user_reply_message(user, reply, message);

	wi_lock_lock(wd_status_lock);
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);
}



void wd_servers_update_server(wi_p7_message_t *message) {
	wi_uuid_t			*token;
	wd_server_t			*server;

	token = wi_p7_message_uuid_for_name(message, WI_STR("wired.tracker.token"));
	server = wd_servers_server_with_token(token);
	
	if(server) {
		wd_servers_remove_stats_for_server(server);

		server->update_time = wi_time_interval();

		wi_p7_message_get_uint32_for_name(message, &server->users, WI_STR("wired.tracker.users"));
		wi_p7_message_get_uint64_for_name(message, &server->files_count, WI_STR("wired.info.files.count"));
		wi_p7_message_get_uint64_for_name(message, &server->files_size, WI_STR("wired.info.files.size"));

		wd_servers_add_stats_for_server(server);

		wi_lock_lock(wd_status_lock);
		wd_write_status(true);
		wi_lock_unlock(wd_status_lock);
	}
}



void wd_servers_reply_categories(wd_user_t *user, wi_p7_message_t *message) {
	wi_array_t			*categories;
	wi_p7_message_t		*reply;
	
	categories = wi_config_stringlist_for_name(wd_config, WI_STR("category"));
	
	reply = wi_p7_message_with_name(WI_STR("wired.tracker.categories"), wd_p7_spec);
	wi_p7_message_set_list_for_name(reply, categories, WI_STR("wired.tracker.categories"));
	wd_user_reply_message(user, reply, message);
}



void wd_servers_reply_server_list(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*reply;
	wd_server_t			*server;
	
	wi_dictionary_rdlock(wd_servers);
	
	enumerator = wi_dictionary_data_enumerator(wd_servers);
	
	while((server = wi_enumerator_next_data(enumerator))) {
		reply = wi_p7_message_with_name(WI_STR("wired.tracker.server_list"), wd_p7_spec);
		wi_p7_message_set_bool_for_name(reply, server->tracker, WI_STR("wired.tracker.tracker"));
		wi_p7_message_set_string_for_name(reply, server->category, WI_STR("wired.tracker.category"));
		wi_p7_message_set_string_for_name(reply, server->url, WI_STR("wired.tracker.url"));
		wi_p7_message_set_uint32_for_name(reply, server->users, WI_STR("wired.tracker.users"));
		wi_p7_message_set_string_for_name(reply, server->name, WI_STR("wired.info.name"));
		wi_p7_message_set_string_for_name(reply, server->description, WI_STR("wired.info.description"));
		wi_p7_message_set_uint64_for_name(reply, server->files_count, WI_STR("wired.info.files.count"));
		wi_p7_message_set_uint64_for_name(reply, server->files_size, WI_STR("wired.info.files.size"));
		wd_user_reply_message(user, reply, message);
	}
	
	wi_dictionary_unlock(wd_servers);

	reply = wi_p7_message_with_name(WI_STR("wired.tracker.server_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



#pragma mark -

static wd_server_t * wd_server_alloc(void) {
	return wi_runtime_create_instance(wd_server_runtime_id, sizeof(wd_server_t));
}



static wd_server_t * wd_server_init_with_message(wd_server_t *server, wi_p7_message_t *message) {
	server->token			= wi_uuid_init(wi_uuid_alloc());
	server->ip				= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.tracker.ip")));
	server->category		= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.tracker.category")));
	server->name			= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.name")));
	server->description		= wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.info.description")));

	wi_p7_message_get_bool_for_name(message, &server->tracker, WI_STR("wired.tracker.tracker"));
	wi_p7_message_get_uint32_for_name(message, &server->port, WI_STR("wired.tracker.port"));
	wi_p7_message_get_uint32_for_name(message, &server->users, WI_STR("wired.tracker.users"));
	wi_p7_message_get_uint64_for_name(message, &server->files_count, WI_STR("wired.info.files.count"));
	wi_p7_message_get_uint64_for_name(message, &server->files_size, WI_STR("wired.info.files.size"));
	
	return server;
}



static void wd_server_dealloc(wi_runtime_instance_t *instance) {
	wd_server_t		*server = instance;
	
	wi_release(server->token);
	wi_release(server->category);
	wi_release(server->ip);
	wi_release(server->url);
	wi_release(server->name);
	wi_release(server->description);
}
