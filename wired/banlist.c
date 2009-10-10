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

#include "config.h"

#include <wired/wired.h>

#include "banlist.h"
#include "server.h"
#include "settings.h"

struct _wd_ban {
	wi_runtime_base_t					base;
	
	wi_string_t							*ip;
	wi_date_t							*expiration_date;

	wi_timer_t							*timer;
};
typedef struct _wd_ban					wd_ban_t;


static wi_boolean_t						wd_banlist_file_contains_ip(wi_file_t *, wi_string_t *);
static wi_boolean_t						wd_banlist_delete_ban_from_file(wi_file_t *, wi_string_t *);

static wd_ban_t *						wd_ban_alloc(void);
static wd_ban_t *						wd_ban_init_with_ip(wd_ban_t *, wi_string_t *, wi_date_t *);
static void								wd_ban_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_ban_description(wi_runtime_instance_t *);

static void								wd_ban_expire_timer(wi_timer_t *);


static wi_rwlock_t						*wd_banlist_lock;
static wi_string_t						*wd_banlist_path;
static wi_mutable_dictionary_t			*wd_bans;

static wi_runtime_id_t					wd_ban_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_ban_runtime_class = {
	"wd_ban_t",
	wd_ban_dealloc,
	NULL,
	NULL,
	wd_ban_description,
	NULL
};



void wd_banlist_initialize(void) {
	wd_ban_runtime_id = wi_runtime_register_class(&wd_ban_runtime_class);

	wd_banlist_path = WI_STR("banlist");
	wd_banlist_lock = wi_rwlock_init(wi_rwlock_alloc());

	wd_bans = wi_dictionary_init(wi_mutable_dictionary_alloc());
}



#pragma mark -

wi_boolean_t wd_banlist_ip_is_banned(wi_string_t *ip, wi_date_t **expiration_date) {
	wi_file_t			*file;
	wd_ban_t			*ban;
	wi_boolean_t		banned = false;

	wi_dictionary_rdlock(wd_bans);
	ban = wi_autorelease(wi_retain(wi_dictionary_data_for_key(wd_bans, ip)));
	wi_dictionary_unlock(wd_bans);
	
	if(ban) {
		*expiration_date = ban->expiration_date;

		return true;
	}
	
	wi_rwlock_rdlock(wd_banlist_lock);

	file = wi_file_for_reading(wd_banlist_path);
	
	if(file)
		banned = wd_banlist_file_contains_ip(file, ip);
	else
		wi_log_err(WI_STR("Could not open \"%@\": %m"), wd_banlist_path);
	
	wi_rwlock_unlock(wd_banlist_lock);
	
	*expiration_date = NULL;

	return banned;
}



#pragma mark -

void wd_banlist_reply_bans(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_enumerator_t		*enumerator;
	wi_file_t			*file;
	wi_string_t			*string;
	wd_ban_t			*ban;
	
	wi_rwlock_rdlock(wd_banlist_lock);
	
	file = wi_file_for_reading(wd_banlist_path);
	
	if(!file) {
		wi_log_err(WI_STR("Could not open \"%@\": %m"), wd_banlist_path);
	} else {
		while((string = wi_file_read_config_line(file))) {
			reply = wi_p7_message_with_name(WI_STR("wired.banlist.list"), wd_p7_spec);
			wi_p7_message_set_string_for_name(reply, string, WI_STR("wired.banlist.ip"));
			wd_user_reply_message(user, reply, message);
		}
	}
	
	wi_rwlock_unlock(wd_banlist_lock);
	
	wi_dictionary_rdlock(wd_bans);
	
	enumerator = wi_dictionary_data_enumerator(wd_bans);
	
	while((ban = wi_enumerator_next_data(enumerator))) {
		reply = wi_p7_message_with_name(WI_STR("wired.banlist.list"), wd_p7_spec);
		wi_p7_message_set_string_for_name(reply, ban->ip, WI_STR("wired.banlist.ip"));
		wi_p7_message_set_date_for_name(reply, ban->expiration_date, WI_STR("wired.banlist.expiration_date"));
		wd_user_reply_message(user, reply, message);
	}

	wi_dictionary_unlock(wd_bans);

	reply = wi_p7_message_with_name(WI_STR("wired.banlist.list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



wi_boolean_t wd_banlist_add_ban(wi_string_t *ip, wi_date_t *expiration_date, wd_user_t *user, wi_p7_message_t *message) {
	wi_file_t		*file;
	wd_ban_t		*ban;
	wi_boolean_t	result = false;
	
	if(expiration_date) {
		if(wi_date_time_interval(expiration_date) - wi_time_interval() > 1.0) {
			wi_dictionary_wrlock(wd_bans);
			
			if(!wi_dictionary_contains_key(wd_bans, ip)) {
				ban = wd_ban_init_with_ip(wd_ban_alloc(), ip, expiration_date);
				wi_timer_schedule(ban->timer);
				wi_mutable_dictionary_set_data_for_key(wd_bans, ban, ip);
				wi_release(ban);
				
				result = true;
			} else {
				wd_user_reply_error(user, WI_STR("wired.error.ban_exists"), message);
			}
			
			wi_dictionary_unlock(wd_bans);
		} else {
			wi_log_err(WI_STR("Ban has negative expiration date"), wd_banlist_path);
			wd_user_reply_internal_error(user, WI_STR("Ban has negative expiration date"), message);
		}
	} else {
		wi_rwlock_wrlock(wd_banlist_lock);
		
		file = wi_file_for_updating(wd_banlist_path);
		
		if(file) {
			if(!wd_banlist_file_contains_ip(file, ip)) {
				wi_file_write_format(file, WI_STR("%@\n"), ip);
				
				result = true;
			} else {
				wd_user_reply_error(user, WI_STR("wired.error.ban_exists"), message);
			}
		} else {
			wi_log_err(WI_STR("Could not open \"%@\": %m"), wd_banlist_path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
		
		wi_rwlock_unlock(wd_banlist_lock);
	}
	
	return result;
}



wi_boolean_t wd_banlist_delete_ban(wi_string_t *ip, wi_date_t *expiration_date, wd_user_t *user, wi_p7_message_t *message) {
	wi_file_t		*file;
	wi_boolean_t	result = false;
	
	if(expiration_date) {
		wi_dictionary_wrlock(wd_bans);
		
		if(wi_dictionary_contains_key(wd_bans, ip)) {
			wi_mutable_dictionary_remove_data_for_key(wd_bans, ip);
			
			result = true;
		} else {
			wd_user_reply_error(user, WI_STR("wired.error.ban_not_found"), message);
		}
		
		wi_dictionary_unlock(wd_bans);
	} else {
		wi_rwlock_wrlock(wd_banlist_lock);
		
		file = wi_file_for_updating(wd_banlist_path);
		
		if(file) {
			if(wd_banlist_delete_ban_from_file(file, ip))
				result = true;
			else
				wd_user_reply_error(user, WI_STR("wired.error.ban_not_found"), message);
		} else {
			wi_log_err(WI_STR("Could not open \"%@\": %m"), wd_banlist_path);
			wd_user_reply_internal_error(user, wi_error_string(), message);
		}
		
		wi_rwlock_unlock(wd_banlist_lock);
	}
	
	return result;
}



#pragma mark -

static wi_boolean_t wd_banlist_file_contains_ip(wi_file_t *file, wi_string_t *ip) {
	wi_string_t		*string;
	
	while((string = wi_file_read_config_line(file))) {
		if(wi_ip_matches_string(ip, string))
			return true;
	}
	
	return false;
}



static wi_boolean_t wd_banlist_delete_ban_from_file(wi_file_t *file, wi_string_t *ip) {
 	wi_file_t		*tmpfile;
	wi_string_t		*string;
	wi_boolean_t	result = false;
	
	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		return false;
	}
	
	while((string = wi_file_read_line(file))) {
		if(wi_string_length(string) == 0 || wi_string_has_prefix(string, WI_STR("#"))) {
			wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
		} else {
			if(wi_is_equal(string, ip))
				result = true;
			else
				wi_file_write_format(tmpfile, WI_STR("%@\n"), string);
		}
	}
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	while((string = wi_file_read(tmpfile, WI_FILE_BUFFER_SIZE)))
		wi_file_write_format(file, WI_STR("%@"), string);
	
	return result;
}



#pragma mark -

static wd_ban_t * wd_ban_alloc(void) {
	return wi_runtime_create_instance(wd_ban_runtime_id, sizeof(wd_ban_t));
}



static wd_ban_t * wd_ban_init_with_ip(wd_ban_t *ban, wi_string_t *ip, wi_date_t *expiration_date) {
	ban->ip					= wi_retain(ip);
	ban->expiration_date	= wi_retain(expiration_date);
	ban->timer				= wi_timer_init_with_function(wi_timer_alloc(),
														  wd_ban_expire_timer,
														  wi_date_time_interval(expiration_date) - wi_time_interval(),
														  false);

	wi_timer_set_data(ban->timer, ban);
	
	return ban;
}



static void wd_ban_dealloc(wi_runtime_instance_t *instance) {
	wd_ban_t		*ban = instance;
	
	wi_release(ban->ip);
	wi_release(ban->expiration_date);
	wi_release(ban->timer);
}



static wi_string_t * wd_ban_description(wi_runtime_instance_t *instance) {
	wd_ban_t		*ban = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{ip = %@}"),
		wi_runtime_class_name(ban),
		ban,
		ban->ip);
}



#pragma mark -

static void wd_ban_expire_timer(wi_timer_t *timer) {
	wd_ban_t		*ban;
	
	ban = wi_timer_data(timer);
	
	wi_dictionary_rdlock(wd_bans);
	wi_mutable_dictionary_remove_data_for_key(wd_bans, ban->ip);
	wi_dictionary_unlock(wd_bans);
}
