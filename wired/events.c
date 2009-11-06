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

#define WD_EVENTS_WRITE_EVENTS			100
#define WD_EVENTS_MAX_EVENTS			5000


static wi_mutable_array_t *				wd_events_array_for_archive(wi_date_t *);
static void								wd_events_archive_events(void);
static wi_p7_message_t *				wd_events_message_with_dictionary(wi_string_t *, wi_dictionary_t *);


static wi_mutable_array_t				*wd_events;

static wi_mutable_dictionary_t			*wd_events_last_events;

static wi_rwlock_t						*wd_events_lock;
static wi_string_t						*wd_events_path;
static wi_string_t						*wd_events_current_path;



void wd_events_initialize(void) {
	wi_mutable_array_t		*events;
	
	wd_events_path			= WI_STR("events");
	wd_events_current_path	= wi_retain(wi_string_by_appending_path_component(wd_events_path, WI_STR("current")));
	wd_events_lock			= wi_rwlock_init(wi_rwlock_alloc());
	
	events					= wd_events_array_for_archive(NULL);
	
	if(events)
		wd_events = wi_retain(events);
	else
		wd_events = wi_array_init(wi_mutable_array_alloc());
	
	wd_events_last_events = wi_dictionary_init(wi_mutable_dictionary_alloc());
}



void wd_events_flush_events(void) {
	if(!wi_plist_write_instance_to_file(wd_events, wd_events_current_path))
		wi_log_error(WI_STR("Could not write events to \"%@\": %m"), wd_events_current_path);
}



#pragma mark -

void wd_events_reply_archives(wd_user_t *user, wi_p7_message_t *message) {
	wi_fsenumerator_t			*fsenumerator;
	wi_string_t					*path;
	wi_date_t					*archive;
	wi_p7_message_t				*reply;
	wi_fsenumerator_status_t	status;
	
	fsenumerator = wi_fs_enumerator_at_path(wd_events_path);
	
	if(!fsenumerator) {
		wi_log_error(WI_STR("Could not open \"%@\": %m"), wd_events_path);
		wd_user_reply_internal_error(user, wi_error_string(), message);
		
		return;
	}
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &path)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_error(WI_STR("Could not read event archive \"%@\": %m"), path);
			
			continue;
		}
		
		archive = wi_date_with_rfc3339_string(wi_string_last_path_component(path));
		
		if(archive) {
			reply = wi_p7_message_with_name(WI_STR("wired.event.archive_list"), wd_p7_spec);
			wi_p7_message_set_date_for_name(reply, archive, WI_STR("wired.event.archive"));
			wd_user_reply_message(user, reply, message);
		}
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.event.archive_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



wi_boolean_t wd_events_reply_events(wi_date_t *archive, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_dictionary_t		*dictionary;
	wi_array_t			*events;
	wi_p7_message_t		*reply;
	wi_boolean_t		result = false;
	
	if(archive) {
		events = wd_events_array_for_archive(archive);
		
		if(events) {
			enumerator = wi_array_data_enumerator(events);
			
			while((dictionary = wi_enumerator_next_data(enumerator))) {
				reply = wd_events_message_with_dictionary(WI_STR("wired.event.event_list"), dictionary);
				wi_p7_message_set_date_for_name(reply, archive, WI_STR("wired.event.archive"));
				wd_user_reply_message(user, reply, message);
			}
			
			reply = wi_p7_message_with_name(WI_STR("wired.event.event_list.done"), wd_p7_spec);
			wd_user_reply_message(user, reply, message);
			
			result = true;
		} else {
			wd_user_reply_internal_error(user, NULL, message);
		}
	} else {
		wi_array_rdlock(wd_events);
		
		enumerator = wi_array_data_enumerator(wd_events);
		
		while((dictionary = wi_enumerator_next_data(enumerator))) {
			reply = wd_events_message_with_dictionary(WI_STR("wired.event.event_list"), dictionary);
			wd_user_reply_message(user, reply, message);
		}
		
		wi_array_unlock(wd_events);
		
		reply = wi_p7_message_with_name(WI_STR("wired.event.event_list.done"), wd_p7_spec);
		wd_user_reply_message(user, reply, message);
		
		result = true;
	}
	
	return false;
}



#pragma mark -

void wd_events_add_event(wi_string_t *event, wd_user_t *user, ...) {
	wi_enumerator_t				*enumerator;
	wi_mutable_dictionary_t		*dictionary;
	wi_mutable_array_t			*lastevents;
	wi_array_t					*parameters;
	wi_string_t					*nick, *login, *ip;
	wi_p7_message_t				*message;
	wd_user_t					*peer;
	va_list						ap;
	
	wi_dictionary_rdlock(wd_events_last_events);
	
	lastevents = wi_dictionary_data_for_key(wd_events_last_events, wi_number_with_integer(wd_user_id(user)));
	
	wi_dictionary_unlock(wd_events_last_events);
	
	if(lastevents && wi_array_contains_data(lastevents, event)) {
		if(wi_is_equal(event, WI_STR("wired.event.user.got_users")) || wi_is_equal(event, WI_STR("wired.event.user.got_info")))
			return;
	}
	
	va_start(ap, user);
	
	parameters		= wi_array_with_arguments(ap);
	
	va_end(ap);
	
	nick			= wd_user_nick(user);
	login			= wd_user_login(user);
	ip				= wd_user_ip(user);
	dictionary		= wi_mutable_dictionary_with_data_and_keys(
		event,							WI_STR("wired.event.event"),
		wi_date(),						WI_STR("wired.event.time"),
		nick ? nick : WI_STR(""),		WI_STR("wired.user.nick"),
		login ? login : WI_STR(""),		WI_STR("wired.user.login"),
		ip ? ip : WI_STR(""),			WI_STR("wired.user.ip"),
		NULL);
	
	if(wi_array_count(parameters) > 0)
		wi_mutable_dictionary_set_data_for_key(dictionary, parameters, WI_STR("wired.event.parameters"));
	
	wi_rwlock_wrlock(wd_events_lock);
	wi_array_wrlock(wd_events);
	
	wi_mutable_array_add_data(wd_events, dictionary);
	
	if(wi_array_count(wd_events) > 0 && wi_array_count(wd_events) % WD_EVENTS_WRITE_EVENTS == 0)
		wd_events_flush_events();
	
	wi_array_unlock(wd_events);
	wi_rwlock_unlock(wd_events_lock);
	
	wd_events_archive_events();
	
	wi_dictionary_rdlock(wd_users);
	
	message			= wd_events_message_with_dictionary(WI_STR("wired.event.event"), dictionary);
	enumerator		= wi_dictionary_data_enumerator(wd_users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN && wd_user_is_subscribed_events(peer))
			wd_user_send_message(peer, message);
	}
	
	wi_dictionary_unlock(wd_users);

	wi_dictionary_wrlock(wd_events_last_events);
	
	if(!lastevents)
		lastevents = wi_mutable_array();
	
	wi_mutable_array_add_data(lastevents, event);
		
	if(wi_array_count(lastevents) > 5)
		wi_mutable_array_remove_data_at_index(lastevents, 0);
	
	wi_mutable_dictionary_set_data_for_key(wd_events_last_events, lastevents, wi_number_with_integer(wd_user_id(user)));
	
	wi_dictionary_unlock(wd_events_last_events);
}



#pragma mark -

void wd_events_remove_user(wd_user_t *user) {
	wi_dictionary_wrlock(wd_events_last_events);
	
	wi_mutable_dictionary_remove_data_for_key(wd_events_last_events, wi_number_with_integer(wd_user_id(user)));
	
	wi_dictionary_unlock(wd_events_last_events);
}



#pragma mark -

static wi_mutable_array_t * wd_events_array_for_archive(wi_date_t *archive) {
	wi_runtime_instance_t		*instance;
	wi_string_t					*path;
	
	if(archive)
		path = wi_string_by_appending_path_component(wd_events_path, wi_date_rfc3339_string(archive));
	else
		path = wd_events_current_path;
	
	instance = wi_plist_read_instance_from_file(path);
	
	if(instance) {
		if(wi_runtime_id(instance) != wi_array_runtime_id()) {
			wi_log_error(WI_STR("Could not read events from \"%@\": Invalid format"), path);
			
			instance = NULL;
		}
	} else {
		wi_log_error(WI_STR("Could not read events from \"%@\": %m"), path);
	}
	
	return instance;
}



static void wd_events_archive_events(void) {
	wi_enumerator_t		*enumerator;
	wi_string_t			*path;
	wi_date_t			*archive;
	wi_p7_message_t		*message;
	wd_user_t			*user;
	
	wi_rwlock_wrlock(wd_events_lock);
	wi_array_wrlock(wd_events);
	
	if(wi_array_count(wd_events) >= WD_EVENTS_MAX_EVENTS) {
		wi_mutable_array_remove_all_data(wd_events);
		
		archive		= wi_date();
		path		= wi_string_by_appending_path_component(wd_events_path, wi_date_rfc3339_string(archive));
		
		if(wi_fs_rename_path(wd_events_current_path, path)) {
			if(wi_plist_write_instance_to_file(wd_events, wd_events_current_path)) {
				wi_dictionary_rdlock(wd_users);
				
				message = wi_p7_message_with_name(WI_STR("wired.event.archive"), wd_p7_spec);
				wi_p7_message_set_date_for_name(message, archive, WI_STR("wired.event.archive"));
				
				enumerator = wi_dictionary_data_enumerator(wd_users);
				
				while((user = wi_enumerator_next_data(enumerator))) {
					if(wd_user_state(user) == WD_USER_LOGGED_IN && wd_user_is_subscribed_events(user))
						wd_user_send_message(user, message);
				}
				
				wi_dictionary_unlock(wd_users);
			} else {
				wi_log_error(WI_STR("Could not write events to \"%@\": %m"), wd_events_current_path);
			}
		} else {
			wi_log_error(WI_STR("Could not back up events to \"%@\": %m"), path);
		}
	}

	wi_array_unlock(wd_events);
	wi_rwlock_unlock(wd_events_lock);
}



static wi_p7_message_t * wd_events_message_with_dictionary(wi_string_t *name, wi_dictionary_t *dictionary) {
	wi_array_t			*parameters;
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(name, wd_p7_spec);
	
	wi_p7_message_set_enum_name_for_name(message,
										 wi_dictionary_data_for_key(dictionary, WI_STR("wired.event.event")),
										 WI_STR("wired.event.event"));
	wi_p7_message_set_date_for_name(message,
									wi_dictionary_data_for_key(dictionary, WI_STR("wired.event.time")),
									WI_STR("wired.event.time"));
	
	parameters = wi_dictionary_data_for_key(dictionary, WI_STR("wired.event.parameters"));
	
	if(parameters)
		wi_p7_message_set_list_for_name(message, parameters, WI_STR("wired.event.parameters"));
	
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
