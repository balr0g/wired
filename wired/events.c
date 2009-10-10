/* $Id$ */

/*
 *  Copyright (c) 2009 Axel Andersson
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

#include "events.h"
#include "main.h"
#include "server.h"
#include "users.h"

#define WD_EVENTS_MAX_EVENTS			1000


static wi_mutable_array_t *				wd_events_array(void);
static wi_p7_message_t *				wd_events_message_with_dictionary(wi_string_t *, wi_dictionary_t *);


static wi_mutable_array_t				*wd_events;

static wi_rwlock_t						*wd_events_lock;
static wi_string_t						*wd_events_path;



void wd_events_initialize(void) {
	wi_mutable_array_t		*events;
	
	wd_events_path = WI_STR("events");
	wd_events_lock = wi_rwlock_init(wi_rwlock_alloc());
	
	events = wd_events_array();
	
	if(events)
		wd_events = wi_retain(events);
	else
		wd_events = wi_array_init(wi_mutable_array_alloc());
}



#pragma mark -

void wd_events_reply_events(wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_dictionary_t		*dictionary;
	wi_p7_message_t		*reply;
	
	wi_array_rdlock(wd_events);
	
	enumerator = wi_array_data_enumerator(wd_events);
	
	while((dictionary = wi_enumerator_next_data(enumerator))) {
		reply = wd_events_message_with_dictionary(WI_STR("wired.events.list"), dictionary);
		wd_user_reply_message(user, reply, message);
	}
	
	wi_array_unlock(wd_events);
	
	reply = wi_p7_message_with_name(WI_STR("wired.events.list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



#pragma mark -

void wd_events_add_event(wd_event_t event, wd_user_t *user, wi_string_t *string) {
	wi_enumerator_t		*enumerator;
	wi_dictionary_t		*dictionary;
	wi_string_t			*path;
	wi_p7_message_t		*message;
	wd_user_t			*peer;
	
	dictionary = wi_dictionary_with_data_and_keys(
		WI_INT32(event),				WI_STR("wired.events.event"),
		wi_date(),						WI_STR("wired.events.time"),
		string,							WI_STR("wired.events.string"),
		wd_user_nick(user),				WI_STR("wired.user.nick"),
		wd_user_login(user),			WI_STR("wired.user.login"),
		wd_user_ip(user),				WI_STR("wired.user.ip"),
		NULL);
	
	wi_rwlock_wrlock(wd_events_lock);
	wi_array_wrlock(wd_events);
	
	wi_mutable_array_add_data(wd_events, dictionary);
	
	if(!wi_plist_write_instance_to_file(wd_events, wd_events_path))
		wi_log_err(WI_STR("Could not write events to \"%@\": %m"), wd_events_path);
		
	if(wi_array_count(wd_events) >= WD_EVENTS_MAX_EVENTS) {
		wi_mutable_array_remove_all_data(wd_events);
		
		path = wi_string_by_appending_string(wd_events_path, wi_date_string_with_format(wi_date(), WI_STR("_%Y-%m-%d_%H_%M_%S")));
		
		if(wi_fs_rename_path(wd_events_path, path)) {
			if(!wi_plist_write_instance_to_file(wd_events, wd_events_path))
				wi_log_err(WI_STR("Could not write events to \"%@\": %m"), wd_events_path);
		} else {
			wi_log_err(WI_STR("Could not back up events to \"%@\": %m"), path);
		}
	}
	
	wi_array_unlock(wd_events);
	wi_rwlock_unlock(wd_events_lock);
	
	wi_dictionary_rdlock(wd_users);
	
	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_log(peer)) {
			message = wd_events_message_with_dictionary(WI_STR("wired.events.event"), dictionary);
			wd_user_send_message(user, message);
		}
	}
	
	wi_dictionary_unlock(wd_users);
}



#pragma mark -

static wi_mutable_array_t * wd_events_array(void) {
	wi_runtime_instance_t		*instance;
	
	instance = wi_plist_read_instance_from_file(wd_events_path);
	
	if(instance) {
		if(wi_runtime_id(instance) != wi_array_runtime_id()) {
			wi_log_err(WI_STR("Could not read events from \"%@\": Invalid format"), wd_events_path);
			
			instance = NULL;
		}
	} else {
		wi_log_err(WI_STR("Could not read events from \"%@\": %m"), wd_events_path);
	}
	
	return instance;
}



static wi_p7_message_t * wd_events_message_with_dictionary(wi_string_t *name, wi_dictionary_t *dictionary) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(name, wd_p7_spec);
	
	wi_p7_message_set_enum_for_name(message,
									wi_number_int32(wi_dictionary_data_for_key(dictionary, WI_STR("wired.events.event"))),
									WI_STR("wired.events.event"));
	wi_p7_message_set_date_for_name(message,
									wi_dictionary_data_for_key(dictionary, WI_STR("wired.events.time")),
									WI_STR("wired.events.time"));
	wi_p7_message_set_string_for_name(message,
									  wi_dictionary_data_for_key(dictionary, WI_STR("wired.events.string")),
									  WI_STR("wired.events.string"));
	wi_p7_message_set_string_for_name(message,
									  wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")),
									  WI_STR("wired.user.nick"));
	wi_p7_message_set_string_for_name(message,
									  wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.login")),
									  WI_STR("wired.user.login"));
	wi_p7_message_set_string_for_name(message,
									  wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.ip")),
									  WI_STR("wired.user.ip"));
	
	return message;
}
