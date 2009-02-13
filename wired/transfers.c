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

#include "config.h"

#include <sys/fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <wired/wired.h>

#include "files.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "transfers.h"

#define WD_TRANSFERS_PARTIAL_EXTENSION	".WiredTransfer"

#define WD_TRANSFERS_TIMER_INTERVAL		60.0
#define WD_TRANSFERS_WAITING_INTERVAL	20.0

#define WD_TRANSFER_BUFFER_SIZE			8192


static void								wd_transfers_update_waiting(wi_timer_t *);
static wi_string_t *					wd_transfers_transfer_key_for_user(wd_user_t *);
static void								wd_transfers_add_or_remove_transfer(wd_transfer_t *, wi_boolean_t);

static void								wd_transfers_update_queue(void);
static wi_integer_t						wd_transfers_compare_user(wi_runtime_instance_t *, wi_runtime_instance_t *);

static wd_transfer_t *					wd_transfer_alloc(void);
static wd_transfer_t *					wd_transfer_init_with_user(wd_transfer_t *, wd_user_t *);
static wd_transfer_t *					wd_transfer_init_download_with_user(wd_transfer_t *, wd_user_t *);
static wd_transfer_t *					wd_transfer_init_upload_with_user(wd_transfer_t *, wd_user_t *);
static void								wd_transfer_dealloc(wi_runtime_instance_t *);
static wi_string_t *					wd_transfer_description(wi_runtime_instance_t *);

static void								wd_transfer_set_state(wd_transfer_t *, wd_transfer_state_t);
static wd_transfer_state_t				wd_transfer_state(wd_transfer_t *);
static inline void						wd_transfer_limit_speed(wd_transfer_t *, wi_uinteger_t, wi_uinteger_t, ssize_t, wi_time_interval_t, wi_time_interval_t);

static void								wd_transfer_thread(wi_runtime_instance_t *);
static wi_boolean_t						wd_transfer_open(wd_transfer_t *);
static void								wd_transfer_close(wd_transfer_t *);
static void								wd_transfer_download(wd_transfer_t *);
static void								wd_transfer_upload(wd_transfer_t *);


wi_array_t								*wd_transfers;

static wi_timer_t						*wd_transfers_timer;

static wi_integer_t						wd_transfers_total_download_speed, wd_transfers_total_upload_speed;

static wi_lock_t						*wd_transfers_status_lock;
static wi_dictionary_t					*wd_transfers_user_downloads, *wd_transfers_user_uploads;
static wi_uinteger_t					wd_transfers_active_downloads, wd_transfers_active_uploads;

static wi_lock_t						*wd_transfers_update_queue_lock;

static wi_runtime_id_t					wd_transfer_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t				wd_transfer_runtime_class = {
	"wd_transfer_t",
	wd_transfer_dealloc,
	NULL,
	NULL,
	wd_transfer_description,
	NULL
};


void wd_transfers_init(void) {
	wd_transfer_runtime_id = wi_runtime_register_class(&wd_transfer_runtime_class);

	wd_transfers = wi_array_init(wi_array_alloc());

	wd_transfers_timer = wi_timer_init_with_function(wi_timer_alloc(),
													 wd_transfers_update_waiting,
													 WD_TRANSFERS_TIMER_INTERVAL,
													 true);
	
	wd_transfers_status_lock = wi_lock_init(wi_lock_alloc());
	wd_transfers_update_queue_lock = wi_lock_init(wi_lock_alloc());
	
	wd_transfers_user_downloads = wi_dictionary_init(wi_dictionary_alloc());
	wd_transfers_user_uploads = wi_dictionary_init(wi_dictionary_alloc());
}



void wd_transfers_apply_settings(wi_set_t *changes) {
	wd_transfers_total_download_speed = wi_config_integer_for_name(wd_config, WI_STR("total download speed"));
	wd_transfers_total_upload_speed = wi_config_integer_for_name(wd_config, WI_STR("total upload speed"));
}



void wd_transfers_schedule(void) {
	wi_timer_schedule(wd_transfers_timer);
}



static void wd_transfers_update_waiting(wi_timer_t *timer) {
	wd_transfer_t		*transfer;
	wi_time_interval_t	interval;
	wi_boolean_t		update = false;
	wi_uinteger_t		i, count;
	
	wi_array_wrlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);

	if(count > 0) {
		interval = wi_time_interval();

		for(i = 0; i < count; i++) {
			transfer = wi_retain(WI_ARRAY(wd_transfers, i));
			
			wi_condition_lock_lock(transfer->state_lock);
			
			if(transfer->state == WD_TRANSFER_WAITING &&
			   transfer->waiting_time + WD_TRANSFERS_WAITING_INTERVAL < interval) {
				wd_transfers_add_or_remove_transfer(transfer, false);
				
				wi_array_remove_data_at_index(wd_transfers, i);
				
				count--;
				i--;
				update = true;
			}
			
			wi_condition_lock_unlock(transfer->state_lock);
			
			wi_release(transfer);
		}
	}

	wi_array_unlock(wd_transfers);

	if(update)
		wd_transfers_update_queue();
}



static wi_string_t * wd_transfers_transfer_key_for_user(wd_user_t *user) {
	wi_string_t		*login, *ip;
	
	login	= wd_user_login(user);
	ip		= wd_user_ip(user);
	
	if(login && ip)
		return wi_string_by_appending_string(login, ip);
	
	return NULL;
}



static void wd_transfers_add_or_remove_transfer(wd_transfer_t *transfer, wi_boolean_t add) {
	wi_dictionary_t		*dictionary;
	wi_array_t			*array;
	
	wi_lock_lock(wd_transfers_status_lock);
	
	if(transfer->type == WD_TRANSFER_DOWNLOAD) {
		wd_transfers_active_downloads += add ? 1 : -1;
		dictionary = wd_transfers_user_downloads;
	} else {
		wd_transfers_active_uploads += add ? 1 : -1;
		dictionary = wd_transfers_user_uploads;
	}
	
	array = wi_dictionary_data_for_key(dictionary, transfer->key);
	
	if(!array && add) {
		array = wi_array();
		wi_dictionary_set_data_for_key(dictionary, array, transfer->key);
	}
	
	if(array) {
		if(add) {
			wi_array_add_data(array, transfer);
		} else {
			wi_array_remove_data(array, transfer);
		
			if(wi_array_count(array) == 0)
				wi_dictionary_remove_data_for_key(dictionary, transfer->key);
		}
	}

	wi_lock_unlock(wd_transfers_status_lock);
}



#pragma mark -

void wd_transfers_queue_download(wi_string_t *path, wi_file_offset_t offset, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realpath;
	wd_transfer_t		*transfer;
	wi_fs_stat_t		sb;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_stat_path(realpath, &sb)) {
		wi_log_err(WI_STR("Could not open %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);

		return;
	}
	
	if(offset > (wi_file_offset_t) sb.size) {
		wi_log_err(WI_STR("Could not seek to %llu which is beyond file size %llu in %@: %m"), offset, (wi_file_offset_t) sb.size, realpath);
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
		
		return;
	}
	
	transfer				= wi_autorelease(wd_transfer_init_download_with_user(wd_transfer_alloc(), user));
	transfer->path			= wi_retain(path);
	transfer->realpath		= wi_retain(realpath);
	transfer->size			= sb.size;
	transfer->offset		= offset;
	transfer->transferred	= offset;
	
	if(!wi_p7_message_get_uint32_for_name(message, &transfer->transaction, WI_STR("wired.transaction")))
		transfer->transaction = 0;
	
	wd_user_set_transfer(user, transfer);
	
	wi_array_wrlock(wd_transfers);
	wi_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();
}



void wd_transfers_queue_upload(wi_string_t *path, wi_file_offset_t size, wi_boolean_t executable, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realpath;
	wd_transfer_t		*transfer;
	wi_file_offset_t	offset;
	wi_fs_stat_t		sb;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(wi_fs_stat_path(realpath, &sb)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

		return;
	}
	
	if(!wi_string_has_suffix(realpath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION)))
		wi_string_append_string(realpath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION));
	
	if(wi_fs_stat_path(realpath, &sb))
		offset = sb.size;
	else
		offset = 0;
	
	if(size < offset) {
		wi_log_err(WI_STR("Could not seek to %llu which is beyond file size %llu in %@: %m"), offset, size, realpath);
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
		
		return;
	}

	transfer				= wi_autorelease(wd_transfer_init_upload_with_user(wd_transfer_alloc(), user));
	transfer->path			= wi_retain(path);
	transfer->realpath		= wi_retain(realpath);
	transfer->size			= size;
	transfer->offset		= offset;
	transfer->transferred	= offset;
	transfer->executable	= executable;
	
	if(!wi_p7_message_get_uint32_for_name(message, &transfer->transaction, WI_STR("wired.transaction")))
		transfer->transaction = 0;

	wd_user_set_transfer(user, transfer);
	
	wi_array_wrlock(wd_transfers);
	wi_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();
}



void wd_transfers_remove_user(wd_user_t *user) {
	wi_string_t				*key;
	wd_transfer_t			*transfer;
	wi_uinteger_t			i, count;
	wi_boolean_t			update = false;
	wd_transfer_state_t		state;
	
	key = wd_transfers_transfer_key_for_user(user);
	
	if(!key)
		return;
	
	wi_array_wrlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = wi_autorelease(wi_retain(WI_ARRAY(wd_transfers, i)));
		
		if(wi_is_equal(key, transfer->key)) {
			state = wd_transfer_state(transfer);
			
			if(state == WD_TRANSFER_RUNNING) {
				wi_array_unlock(wd_transfers);
				
				transfer->disconnected = true;
				
				wd_transfer_set_state(transfer, WD_TRANSFER_STOP);
				
				wi_condition_lock_lock_when_condition(transfer->state_lock, WD_TRANSFER_STOPPED, 1.0);
				wi_condition_lock_unlock(transfer->state_lock);
				
				wi_array_wrlock(wd_transfers);
			}
			else if(state == WD_TRANSFER_QUEUED || state == WD_TRANSFER_WAITING) {
				wi_array_remove_data(wd_transfers, transfer);
				wd_user_set_state(transfer->user, WD_USER_DISCONNECTED);

				update = true;
			}
		}
	}
	
	wi_array_unlock(wd_transfers);
	
	if(update)
		wd_transfers_update_queue();
}



wd_transfer_t * wd_transfers_transfer_with_path(wd_user_t *user, wi_string_t *path) {
	wd_transfer_t	*transfer, *value = NULL;
	wi_uinteger_t	i, count;

	wi_array_rdlock(wd_transfers);
	
	count = wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(transfer->user == user && wi_is_equal(transfer->path, path)) {
			value = wi_autorelease(wi_retain(transfer));

			break;          
		}
	}

	wi_array_unlock(wd_transfers);

	return value;
}



#pragma mark -

static void wd_transfers_update_queue(void) {
	wi_p7_message_t		*message;
	wi_set_t			*users;
	wi_dictionary_t		*user_queues;
	wi_array_t			*sorted_users, *user_queue, *user_transfers;
	wi_string_t			*key;
	wd_transfer_t		*transfer;
	wd_user_t			*user;
	wd_account_t		*account;
	wi_uinteger_t		position;
	wi_uinteger_t		i, count;
	wi_uinteger_t		total_downloads, total_uploads, user_downloads, user_uploads;
	wi_boolean_t		queue;
	
	wi_array_rdlock(wd_transfers);
	wi_lock_lock(wd_transfers_status_lock);
	wi_lock_lock(wd_transfers_update_queue_lock);
	
	total_downloads	= wi_config_integer_for_name(wd_config, WI_STR("total downloads"));
	total_uploads	= wi_config_integer_for_name(wd_config, WI_STR("total uploads"));
	users			= wi_set_init(wi_set_alloc());
	user_queues		= wi_dictionary_init(wi_dictionary_alloc());
	count			= wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(wd_transfer_state(transfer) == WD_TRANSFER_QUEUED) {
			user_queue = wi_dictionary_data_for_key(user_queues, transfer->key);
			
			if(!user_queue) {
				user_queue = wi_array();
				wi_dictionary_set_data_for_key(user_queues, user_queue, transfer->key);
			}
			
			wi_array_add_data(user_queue, transfer);
			wi_set_add_data(users, transfer->user);
		}
	}
	
	count = wi_set_count(users);
	
	if(count > 0) {
		sorted_users = wi_set_all_data(users);
		wi_array_sort(sorted_users, wd_transfers_compare_user);
		
		position = 1;
		
		while(count > 0) {
			for(i = 0; i < count; i++) {
				user		= WI_ARRAY(sorted_users, i);
				account		= wd_user_account(user);
				key			= wd_transfers_transfer_key_for_user(user);
				
				if(key) {
					user_queue	= wi_dictionary_data_for_key(user_queues, wd_transfers_transfer_key_for_user(user));
					transfer	= WI_ARRAY(user_queue, 0);
					
					if(transfer->type == WD_TRANSFER_DOWNLOAD) {
						user_downloads = wd_account_transfer_download_limit(account);
						user_transfers = wi_dictionary_data_for_key(wd_transfers_user_downloads, transfer->key);
						
						queue = ((total_downloads > 0 && wd_transfers_active_downloads >= total_downloads) ||
								 (user_downloads > 0 && user_transfers && wi_array_count(user_transfers) >= user_downloads));
					} else {
						user_uploads = wd_account_transfer_upload_limit(account);
						user_transfers = wi_dictionary_data_for_key(wd_transfers_user_uploads, transfer->key);
						
						queue = ((total_uploads > 0 && wd_transfers_active_uploads >= total_uploads) ||
								 (user_uploads > 0 && user_transfers && wi_array_count(user_transfers) >= user_uploads));
					}
					
					if(queue) {
						if(transfer->queue != position) {
							transfer->queue = position;
							
							message = wi_p7_message_with_name(WI_STR("wired.transfer.queue"), wd_p7_spec);
							wi_p7_message_set_string_for_name(message, transfer->path, WI_STR("wired.transfer.path"));
							wi_p7_message_set_uint32_for_name(message, transfer->queue, WI_STR("wired.transfer.queue_position"));
							
							if(transfer->transaction > 0)
								wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
							
							wd_user_send_message(transfer->user, message);
						}

						position++;
					} else {
						transfer->queue = 0;
						transfer->waiting_time = wi_time_interval();

						wd_transfer_set_state(transfer, WD_TRANSFER_WAITING);
					
						if(wd_transfer_open(transfer)) {
							if(transfer->type == WD_TRANSFER_DOWNLOAD) {
								wd_transfer_start(transfer);
							} else {
								message = wi_p7_message_with_name(WI_STR("wired.transfer.upload_ready"), wd_p7_spec);
								wi_p7_message_set_string_for_name(message, transfer->path, WI_STR("wired.file.path"));
								wi_p7_message_set_uint64_for_name(message, transfer->offset, WI_STR("wired.transfer.offset"));
								
								if(transfer->transaction > 0)
									wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
								
								wd_user_send_message(transfer->user, message);
							}
						}
					}
					
					wi_array_remove_data_at_index(user_queue, 0);
				} else {
					user_queue = NULL;
				}
				
				if(!user_queue || wi_array_count(user_queue) == 0) {
					wi_array_remove_data_at_index(sorted_users, i);
				
					count--;
					i--;
				}
			}
		}
	}
	
	wi_release(users);
	
	wi_lock_unlock(wd_transfers_update_queue_lock);
	wi_lock_unlock(wd_transfers_status_lock);
	wi_array_unlock(wd_transfers);
}



static wi_integer_t wd_transfers_compare_user(wi_runtime_instance_t *instance1, wi_runtime_instance_t *instance2) {
	wd_user_t			*user1 = instance1;
	wd_user_t			*user2 = instance2;
	wd_transfer_t		*transfer1, *transfer2;
	
	transfer1 = wd_user_transfer(user1);
	transfer2 = wd_user_transfer(user2);
	
	if(transfer1->queue_time > transfer2->queue_time)
		return 1;
	else if(transfer2->queue_time > transfer1->queue_time)
		return -1;
	
	return 0;
}



#pragma mark -

wd_transfer_t * wd_transfer_alloc(void) {
	return wi_runtime_create_instance(wd_transfer_runtime_id, sizeof(wd_transfer_t));
}



static wd_transfer_t * wd_transfer_init_with_user(wd_transfer_t *transfer, wd_user_t *user) {
	transfer->state			= WD_TRANSFER_QUEUED;
	transfer->queue_time	= wi_time_interval();
	transfer->user			= wi_retain(user);
	transfer->key			= wi_retain(wd_transfers_transfer_key_for_user(user));
	transfer->state_lock	= wi_condition_lock_init_with_condition(wi_condition_lock_alloc(), transfer->state);
	transfer->fd			= -1;

	return transfer;
}



static wd_transfer_t * wd_transfer_init_download_with_user(wd_transfer_t *transfer, wd_user_t *user) {
	transfer				= wd_transfer_init_with_user(transfer, user);
	transfer->type			= WD_TRANSFER_DOWNLOAD;
	
	return transfer;
}



static wd_transfer_t * wd_transfer_init_upload_with_user(wd_transfer_t *transfer, wd_user_t *user) {
	transfer				= wd_transfer_init_with_user(transfer, user);
	transfer->type			= WD_TRANSFER_UPLOAD;
	
	return transfer;
}



static void wd_transfer_dealloc(wi_runtime_instance_t *instance) {
	wd_transfer_t		*transfer = instance;
	
	wd_transfer_close(transfer);

	wi_release(transfer->user);
	wi_release(transfer->key);

	wi_release(transfer->path);
	wi_release(transfer->realpath);

	wi_release(transfer->state_lock);
}



static wi_string_t * wd_transfer_description(wi_runtime_instance_t *instance) {
	wd_transfer_t		*transfer = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{path = %@, user = %@}"),
		wi_runtime_class_name(transfer),
		transfer,
		transfer->path,
		transfer->user);
}



#pragma mark -

static void wd_transfer_set_state(wd_transfer_t *transfer, wd_transfer_state_t state) {
	wi_condition_lock_lock(transfer->state_lock);
	transfer->state = state;
	wi_condition_lock_unlock_with_condition(transfer->state_lock, transfer->state);
}



static wd_transfer_state_t wd_transfer_state(wd_transfer_t *transfer) {
	wd_transfer_state_t		state;
	
	wi_condition_lock_lock(transfer->state_lock);
	state = transfer->state;
	wi_condition_lock_unlock(transfer->state_lock);
	
	return state;
}



static inline void wd_transfer_limit_speed(wd_transfer_t *transfer, wi_uinteger_t totalspeed, wi_uinteger_t accountspeed, ssize_t bytes, wi_time_interval_t now, wi_time_interval_t then) {
	wi_uinteger_t	limit, totallimit;
	
	if(totalspeed > 0 || accountspeed > 0) {
		totallimit = (totalspeed > 0)
			? (float) totalspeed / (float) wd_current_downloads
			: 0;
		
		if(totallimit > 0 && accountspeed > 0)
			limit = WI_MIN(totallimit, accountspeed);
		else if(totallimit > 0)
			limit = totallimit;
		else
			limit = accountspeed;

		if(limit > 0) {
			while(transfer->speed > limit) {
				usleep(10000);
				now += 0.01;
				
				transfer->speed = bytes / (now - then);
			}
		}
	}
}



#pragma mark -

void wd_transfer_start(wd_transfer_t *transfer) {
	wd_user_set_state(transfer->user, WD_USER_TRANSFERRING);

	if(!wi_thread_create_thread(wd_transfer_thread, transfer)) {
		wi_log_err(WI_STR("Could not create a transfer thread for %@: %m"),
			wd_user_identifier(transfer->user));
	}
}



static void wd_transfer_thread(wi_runtime_instance_t *argument) {
	wi_pool_t			*pool;
	wd_transfer_t		*transfer = argument;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	wi_condition_lock_lock(transfer->state_lock);
	
	if(transfer->state == WD_TRANSFER_WAITING) {
		transfer->state = WD_TRANSFER_RUNNING;
		wi_condition_lock_unlock_with_condition(transfer->state_lock, transfer->state);
		
		wi_socket_set_interactive(wd_user_socket(transfer->user), false);

		if(transfer->type == WD_TRANSFER_DOWNLOAD)
			wd_transfer_download(transfer);
		else
			wd_transfer_upload(transfer);

		wi_socket_set_interactive(wd_user_socket(transfer->user), true);
	} else {
		wi_condition_lock_unlock(transfer->state_lock);
	}

	wd_user_set_transfer(transfer->user, NULL);
	wd_user_set_state(transfer->user, WD_USER_LOGGED_IN);
	
	if(transfer->disconnected)
		wd_user_set_state(transfer->user, WD_USER_DISCONNECTED);

	wi_array_wrlock(wd_transfers);
	wi_array_remove_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);

	wd_transfers_update_queue();
	
	wi_release(pool);
}



static wi_boolean_t wd_transfer_open(wd_transfer_t *transfer) {
	wi_p7_message_t		*message;

	if(transfer->type == WD_TRANSFER_DOWNLOAD)
		transfer->fd = open(wi_string_cstring(transfer->realpath), O_RDONLY, 0);
	else
		transfer->fd = open(wi_string_cstring(transfer->realpath), O_WRONLY | O_APPEND | O_CREAT, 0666);
	
	if(transfer->fd < 0) {
		wi_log_err(WI_STR("Could not open %@: %s"),
			transfer->realpath, strerror(errno));
		
		message = wi_p7_message_with_name(WI_STR("wired.error"), wd_p7_spec);
		wi_p7_message_set_enum_name_for_name(message, WI_STR("wired.error.internal_error"), WI_STR("wired.error"));
		wi_p7_message_set_string_for_name(message, wi_string_with_cstring(strerror(errno)), WI_STR("wired.error.string"));
		
		if(transfer->transaction > 0)
			wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
		
		wd_user_send_message(transfer->user, message);
		
		return false;
	}
	
	return true;
}



static void wd_transfer_close(wd_transfer_t *transfer) {
	if(transfer->fd >= 0) {
		close(transfer->fd);
		transfer->fd = -1;
	}
}



static void wd_transfer_download(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wi_p7_message_t			*message;
	wd_account_t			*account;
	char					buffer[WD_TRANSFER_BUFFER_SIZE];
	wi_socket_state_t		state;
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_file_offset_t		sendbytes, speedbytes, statsbytes;
	wi_uinteger_t			i;
	wi_integer_t			result;
	ssize_t					readbytes;
	int						sd;
	
	wi_log_info(WI_STR("Sending \"%@\" to %@"),
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));
	
	transfer->remainingsize = transfer->size - transfer->offset;

	message = wi_p7_message_with_name(WI_STR("wired.transfer.download"), wd_p7_spec);
	wi_p7_message_set_string_for_name(message, transfer->path, WI_STR("wired.file.path"));
	wi_p7_message_set_oobdata_for_name(message, transfer->remainingsize, WI_STR("wired.transfer.data"));
	
	if(transfer->transaction > 0)
		wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
	
	wd_user_send_message(transfer->user, message);
	
	lseek(transfer->fd, transfer->offset, SEEK_SET);
	
	interval = speedinterval = statusinterval = accountinterval = wi_time_interval();
	speedbytes = statsbytes = 0;
	i = 0;

	socket		= wd_user_socket(transfer->user);
	sd			= wi_socket_descriptor(socket);
	p7_socket	= wd_user_p7_socket(transfer->user);
	account		= wd_user_account(transfer->user);
	
	wi_lock_lock(wd_status_lock);
	wd_current_downloads++;
	wd_total_downloads++;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);
	
	wd_transfers_add_or_remove_transfer(transfer, true);
	
	pool = wi_pool_init(wi_pool_alloc());

	while(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING && transfer->remainingsize > 0) {
		readbytes = read(transfer->fd, buffer, sizeof(buffer));

		if(readbytes <= 0) {
			if(readbytes < 0) {
				wi_log_err(WI_STR("Could not read download from %@: %m"),
					transfer->realpath, strerror(errno));
			}
			
			break;
		}

		timeout = 0.0;
		
		do {
			state = wi_socket_wait_descriptor(sd, 0.1, false, true);
			
			if(state == WI_SOCKET_TIMEOUT) {
				timeout += 0.1;
				
				if(timeout >= 30.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && wd_transfer_state(transfer) == WD_TRANSFER_RUNNING);

		if(state == WI_SOCKET_ERROR) {
			wi_log_err(WI_STR("Could not wait for download to %@: %m"),
				wd_user_identifier(transfer->user));

			break;
		}
		
		if(timeout >= 30.0) {
			wi_log_err(WI_STR("Timed out waiting to write download to %@"),
				wd_user_identifier(transfer->user));
		
			break;
		}

		sendbytes = (transfer->remainingsize < (wi_file_offset_t) readbytes) ? transfer->remainingsize : (wi_file_offset_t) readbytes;
		result = wi_p7_socket_write_oobdata(p7_socket, 30.0, buffer, sendbytes);
		
		if(!result) {
			wi_log_err(WI_STR("Could not write download to %@: %m"),
				wd_user_identifier(transfer->user));
			
			break;
		}

		interval = wi_time_interval();
		transfer->transferred += sendbytes;
		speedbytes += sendbytes;
		statsbytes += sendbytes;
		transfer->remainingsize -= sendbytes;
		
		transfer->speed = speedbytes / (interval - speedinterval);

		wd_transfer_limit_speed(transfer,
								wd_transfers_total_download_speed,
								wd_account_transfer_download_speed_limit(account),
								speedbytes,
								interval,
								speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		if(interval - statusinterval > wd_current_downloads) {
			wi_lock_lock(wd_status_lock);
			wd_downloads_traffic += statsbytes;
			wd_write_status(false);
			wi_lock_unlock(wd_status_lock);

			statsbytes = 0;
			statusinterval = interval;
		}
		
		if(interval - accountinterval > 15.0) {
			account = wd_user_account(transfer->user);
			accountinterval = interval;
		}
		
		if(++i % 1000 == 0)
			wi_pool_drain(pool);
	}

	wi_release(pool);

	wi_log_info(WI_STR("Sent %@/%@ (%llu/%llu bytes) of \"%@\" to %@"),
		wd_files_string_for_bytes(transfer->transferred - transfer->offset),
		wd_files_string_for_bytes(transfer->size - transfer->offset),
		transfer->transferred - transfer->offset,
		transfer->size - transfer->offset,
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));
	
	wd_transfer_set_state(transfer, WD_TRANSFER_STOPPED);
 
	wd_transfers_add_or_remove_transfer(transfer, false);
	
	wi_lock_lock(wd_status_lock);
	wd_current_downloads--;
	wd_downloads_traffic += statsbytes;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	wd_transfer_close(transfer);
}



static void wd_transfer_upload(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wi_string_t				*path;
	wd_account_t			*account;
	void					*buffer;
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_socket_state_t		state;
	ssize_t					result, speedbytes, statsbytes;
	wi_uinteger_t			i;
	wi_integer_t			readbytes;
	int						sd;
	
	wi_log_info(WI_STR("Receiving \"%@\" from %@"),
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));
	
	interval = speedinterval = statusinterval = accountinterval = wi_time_interval();
	speedbytes = statsbytes = 0;
	i = 0;
	
	socket		= wd_user_socket(transfer->user);
	sd			= wi_socket_descriptor(socket);
	p7_socket	= wd_user_p7_socket(transfer->user);
	account		= wd_user_account(transfer->user);
	
	wi_lock_lock(wd_status_lock);
	wd_current_uploads++;
	wd_total_uploads++;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);
	
	wd_transfers_add_or_remove_transfer(transfer, true);

	pool = wi_pool_init(wi_pool_alloc());
	
	while(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING && transfer->remainingsize > 0) {
		timeout = 0.0;
		
		do {
			state = wi_socket_wait_descriptor(sd, 0.1, true, false);
			
			if(state == WI_SOCKET_TIMEOUT) {
				timeout += 0.1;
				
				if(timeout >= 30.0)
					break;
			}
		} while(state == WI_SOCKET_TIMEOUT && wd_transfer_state(transfer) == WD_TRANSFER_RUNNING);
		
		if(state == WI_SOCKET_ERROR) {
			wi_log_err(WI_STR("Could not wait for upload from %@: %m"),
				wd_user_identifier(transfer->user));

			break;
		}
		
		if(timeout >= 30.0) {
			wi_log_err(WI_STR("Timed out waiting to read upload from %@"),
				wd_user_identifier(transfer->user));
			
			break;
		}
		
		readbytes = wi_p7_socket_read_oobdata(p7_socket, 30.0, &buffer);

		if(readbytes <= 0) {
			if(readbytes < 0) {
				wi_log_err(WI_STR("Could not read upload from %@: %m"),
					wd_user_identifier(transfer->user));
			}
			
			break;
		}

		result = write(transfer->fd, buffer, readbytes);
		
		if(result <= 0) {
			if(result < 0) {
				wi_log_err(WI_STR("Could not write upload to %@: %s"),
					transfer->realpath, strerror(errno));
			}
			
			break;
		}

		interval = wi_time_interval();
		transfer->transferred += readbytes;
		speedbytes += readbytes;
		statsbytes += readbytes;
		transfer->remainingsize -= readbytes;

		transfer->speed = speedbytes / (interval - speedinterval);

		wd_transfer_limit_speed(transfer,
								wd_transfers_total_upload_speed,
								wd_account_transfer_upload_speed_limit(account),
								speedbytes,
								interval,
								speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		if(interval - statusinterval > wd_current_uploads) {
			wi_lock_lock(wd_status_lock);
			wd_uploads_traffic += statsbytes;
			wd_write_status(false);
			wi_lock_unlock(wd_status_lock);

			statsbytes = 0;
			statusinterval = interval;
		}
		
		if(interval - accountinterval > 15.0) {
			account = wd_user_account(transfer->user);
			accountinterval = interval;
		}
		
		if(++i % 1000 == 0)
			wi_pool_drain(pool);
	}
	
	wi_release(pool);
	
	wi_log_info(WI_STR("Received %@/%@ (%llu/%llu bytes) of \"%@\" from %@"),
		wd_files_string_for_bytes(transfer->transferred - transfer->offset),
		wd_files_string_for_bytes(transfer->size - transfer->offset),
		transfer->transferred - transfer->offset,
		transfer->size - transfer->offset,
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));

	wd_transfer_set_state(transfer, WD_TRANSFER_STOPPED);
	
	wd_transfers_add_or_remove_transfer(transfer, false);

	wi_lock_lock(wd_status_lock);
	wd_uploads_traffic += statsbytes;
	wd_current_uploads--;
	wd_write_status(true);
	wi_lock_unlock(wd_status_lock);

	if(transfer->transferred == transfer->size) {
		path = wi_string_by_deleting_path_extension(transfer->realpath);

		if(wi_fs_rename_path(transfer->realpath, path)) {
			if(transfer->executable) {
				if(!wi_fs_set_mode_for_path(path, 0755))
					wi_log_warn(WI_STR("Could not set mode for %@: %m"), path);
			}

			path = wi_string_by_appending_string(transfer->path, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION));

			wd_files_move_comment(path, transfer->path, NULL, NULL);
		} else {
			wi_log_warn(WI_STR("Could not move %@ to %@: %m"),
				transfer->realpath, path);
		}
	}

	wd_transfer_close(transfer);
}
