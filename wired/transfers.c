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

#define WD_TRANSFERS_PARTIAL_EXTENSION		"WiredTransfer"

#define WD_TRANSFERS_TIMER_INTERVAL			60.0
#define WD_TRANSFERS_WAITING_INTERVAL		20.0

#define WD_TRANSFER_BUFFER_SIZE				16384


enum _wd_transfers_statistics_type {
	WD_TRANSFER_STATISTICS_ADD,
	WD_TRANSFER_STATISTICS_REMOVE,
	WD_TRANSFER_STATISTICS_DATA
};
typedef enum _wd_transfers_statistics_type	wd_transfers_statistics_type_t;


static void									wd_transfers_update_waiting(wi_timer_t *);
static wi_string_t *						wd_transfers_transfer_key_for_user(wd_user_t *);
static void									wd_transfers_add_or_remove_transfer(wd_transfer_t *, wi_boolean_t);
static void									wd_transfers_note_statistics(wd_transfer_type_t, wd_transfers_statistics_type_t, wi_file_offset_t);

static void									wd_transfers_update_queue(void);
static wi_integer_t							wd_transfers_compare_user(wi_runtime_instance_t *, wi_runtime_instance_t *);

static wd_transfer_t *						wd_transfer_alloc(void);
static wd_transfer_t *						wd_transfer_init_with_user(wd_transfer_t *, wd_user_t *);
static wd_transfer_t *						wd_transfer_init_download_with_user(wd_transfer_t *, wd_user_t *);
static wd_transfer_t *						wd_transfer_init_upload_with_user(wd_transfer_t *, wd_user_t *);
static void									wd_transfer_dealloc(wi_runtime_instance_t *);
static wi_string_t *						wd_transfer_description(wi_runtime_instance_t *);

static void									wd_transfer_set_state(wd_transfer_t *, wd_transfer_state_t);
static wd_transfer_state_t					wd_transfer_state(wd_transfer_t *);
static inline void							wd_transfer_limit_speed(wd_transfer_t *, wi_uinteger_t, wi_uinteger_t, wi_uinteger_t, ssize_t, wi_time_interval_t, wi_time_interval_t);

static void									wd_transfer_thread(wi_runtime_instance_t *);
static wi_boolean_t							wd_transfer_open(wd_transfer_t *);
static void									wd_transfer_close(wd_transfer_t *);
static void									wd_transfer_send_download_message(wd_transfer_t *);
static void									wd_transfer_post_process_upload_transfer(wd_transfer_t *);
static void									wd_transfer_download(wd_transfer_t *);
static void									wd_transfer_upload(wd_transfer_t *);


wi_mutable_array_t							*wd_transfers;

static wi_timer_t							*wd_transfers_timer;

static wi_integer_t							wd_transfers_total_download_speed, wd_transfers_total_upload_speed;

static wi_lock_t							*wd_transfers_status_lock;
static wi_mutable_dictionary_t				*wd_transfers_user_downloads, *wd_transfers_user_uploads;
static wi_uinteger_t						wd_transfers_active_downloads, wd_transfers_active_uploads;

static wi_lock_t							*wd_transfers_update_queue_lock;

static wi_runtime_id_t						wd_transfer_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t					wd_transfer_runtime_class = {
	"wd_transfer_t",
	wd_transfer_dealloc,
	NULL,
	NULL,
	wd_transfer_description,
	NULL
};



void wd_transfers_init(void) {
	wd_transfer_runtime_id = wi_runtime_register_class(&wd_transfer_runtime_class);

	wd_transfers = wi_array_init(wi_mutable_array_alloc());

	wd_transfers_timer = wi_timer_init_with_function(wi_timer_alloc(),
													 wd_transfers_update_waiting,
													 WD_TRANSFERS_TIMER_INTERVAL,
													 true);
	
	wd_transfers_status_lock = wi_lock_init(wi_lock_alloc());
	wd_transfers_update_queue_lock = wi_lock_init(wi_lock_alloc());
	
	wd_transfers_user_downloads = wi_dictionary_init(wi_mutable_dictionary_alloc());
	wd_transfers_user_uploads = wi_dictionary_init(wi_mutable_dictionary_alloc());
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
				
				wi_mutable_array_remove_data_at_index(wd_transfers, i);
				
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
	wi_mutable_dictionary_t		*dictionary;
	wi_mutable_array_t			*array;
	
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
		array = wi_mutable_array();
		
		wi_mutable_dictionary_set_data_for_key(dictionary, array, transfer->key);
	}
	
	if(array) {
		if(add) {
			wi_mutable_array_add_data(array, transfer);
		} else {
			wi_mutable_array_remove_data(array, transfer);
		
			if(wi_array_count(array) == 0)
				wi_mutable_dictionary_remove_data_for_key(dictionary, transfer->key);
		}
	}

	wi_lock_unlock(wd_transfers_status_lock);
}



static void wd_transfers_note_statistics(wd_transfer_type_t type, wd_transfers_statistics_type_t statistics, wi_file_offset_t bytes) {
	wi_lock_lock(wd_status_lock);

	if(type == WD_TRANSFER_DOWNLOAD) {
		if(statistics == WD_TRANSFER_STATISTICS_ADD) {
			wd_current_downloads++;
			wd_total_downloads++;
		}
		else if(statistics == WD_TRANSFER_STATISTICS_REMOVE) {
			wd_current_downloads--;
		}
		
		wd_downloads_traffic += bytes;
	} else {
		if(statistics == WD_TRANSFER_STATISTICS_ADD) {
			wd_current_uploads++;
			wd_total_uploads++;
		}
		else if(statistics == WD_TRANSFER_STATISTICS_REMOVE) {
			wd_current_uploads--;
		}
		
		wd_uploads_traffic += bytes;
	}

	wd_write_status((statistics != WD_TRANSFER_STATISTICS_DATA));
	
	wi_lock_unlock(wd_status_lock);
}



#pragma mark -

void wd_transfers_queue_download(wi_string_t *path, wi_file_offset_t dataoffset, wi_file_offset_t rsrcoffset, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realdatapath, *realrsrcpath;
	wd_transfer_t		*transfer;
	wi_fs_stat_t		sb;
	wi_file_offset_t	datasize, rsrcsize;
	
	realdatapath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(wi_fs_stat_path(realdatapath, &sb)) {
		datasize = sb.size;
	} else {
		wi_log_err(WI_STR("Could not open %@: %m"), realdatapath);
		wd_user_reply_file_errno(user, message);

		return;
	}
	
	if(dataoffset > (wi_file_offset_t) sb.size) {
		wi_log_err(WI_STR("Could not seek to %llu which is beyond file size %llu in %@"),
			dataoffset, (wi_file_offset_t) sb.size, realdatapath);
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
		
		return;
	}
	
	realrsrcpath = wi_fs_resource_fork_path_for_path(realdatapath);
	
	if(wd_user_supports_rsrc(user) && realrsrcpath) {
		if(wi_fs_stat_path(realrsrcpath, &sb))
			rsrcsize = sb.size;
		else
			rsrcsize = 0;
		
		if(rsrcoffset > (wi_file_offset_t) sb.size) {
			wi_log_err(WI_STR("Could not seek to %llu which is beyond file size %llu in %@"),
					   rsrcoffset, (wi_file_offset_t) sb.size, realrsrcpath);
			wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
			
			return;
		}
	} else {
		rsrcsize = 0;
	}
	
	transfer				= wi_autorelease(wd_transfer_init_download_with_user(wd_transfer_alloc(), user));
	transfer->path			= wi_retain(path);
	transfer->realdatapath	= wi_retain(realdatapath);
	transfer->realrsrcpath	= wi_retain(realrsrcpath);
	transfer->datasize		= datasize;
	transfer->rsrcsize		= rsrcsize;
	transfer->dataoffset	= dataoffset;
	transfer->rsrcoffset	= rsrcoffset;
	transfer->transferred	= dataoffset + rsrcoffset;
	
	if(!wi_p7_message_get_uint32_for_name(message, &transfer->transaction, WI_STR("wired.transaction")))
		transfer->transaction = 0;
	
	wd_user_set_transfer(user, transfer);
	
	wi_array_wrlock(wd_transfers);
	wi_mutable_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();
}



void wd_transfers_queue_upload(wi_string_t *path, wi_file_offset_t datasize, wi_file_offset_t rsrcsize, wi_boolean_t executable, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realdatapath, *realrsrcpath;
	wd_transfer_t		*transfer;
	wi_file_offset_t	dataoffset, rsrcoffset;
	wi_fs_stat_t		sb;
	
	realdatapath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(wi_fs_stat_path(realdatapath, &sb)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

		return;
	}
	
	if(!wi_string_has_suffix(realdatapath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION)))
		realdatapath = wi_string_by_appending_path_extension(realdatapath, WI_STR(WD_TRANSFERS_PARTIAL_EXTENSION));
	
	if(wi_fs_stat_path(realdatapath, &sb))
		dataoffset = sb.size;
	else
		dataoffset = 0;
	
	if(datasize < dataoffset) {
		wi_log_err(WI_STR("Could not seek to %llu which is beyond file size %llu in %@: %m"),
			dataoffset, datasize, realdatapath);
		wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
		
		return;
	}
	
	if(rsrcsize > 0) {
		realrsrcpath = wi_fs_resource_fork_path_for_path(realdatapath);
		
		if(!realrsrcpath) {
			wd_user_reply_error(user, WI_STR("wired.error.rsrc_not_supported"), message);
			
			return;
		}

		if(wi_fs_stat_path(realrsrcpath, &sb))
			rsrcoffset = sb.size;
		else
			rsrcoffset = 0;
		
		if(rsrcsize < rsrcoffset) {
			wi_log_err(WI_STR("Could not seek to %llu which is beyond file size %llu in %@: %m"),
				rsrcoffset, rsrcsize, realrsrcpath);
			wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
			
			return;
		}
	} else {
		realrsrcpath = NULL;
		rsrcoffset = 0;
	}
	
	transfer				= wi_autorelease(wd_transfer_init_upload_with_user(wd_transfer_alloc(), user));
	transfer->path			= wi_retain(path);
	transfer->realdatapath	= wi_retain(realdatapath);
	transfer->realrsrcpath	= wi_retain(realrsrcpath);
	transfer->datasize		= datasize;
	transfer->rsrcsize		= rsrcsize;
	transfer->dataoffset	= dataoffset;
	transfer->rsrcoffset	= rsrcoffset;
	transfer->transferred	= dataoffset + rsrcoffset;
	transfer->executable	= executable;
	
	if(!wi_p7_message_get_uint32_for_name(message, &transfer->transaction, WI_STR("wired.transaction")))
		transfer->transaction = 0;

	wd_user_set_transfer(user, transfer);
	
	wi_array_wrlock(wd_transfers);
	wi_mutable_array_add_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);
	
	wd_transfers_update_queue();
}



void wd_transfers_remove_user(wd_user_t *user, wi_boolean_t removingallusers) {
	wi_enumerator_t			*enumerator;
	wi_string_t				*key;
	wd_user_t				*each_user;
	wd_transfer_t			*transfer;
	wi_uinteger_t			i;
	wi_boolean_t			update = false, present = false;
	wd_transfer_state_t		state;
	
	key = wd_transfers_transfer_key_for_user(user);
	
	if(!key)
		return;
	
	if(!removingallusers) {
		wi_dictionary_rdlock(wd_users);
		
		enumerator = wi_dictionary_data_enumerator(wd_users);
		
		while((each_user = wi_enumerator_next_data(enumerator))) {
			if(wd_user_state(each_user) == WD_USER_LOGGED_IN && wi_is_equal(wd_transfers_transfer_key_for_user(each_user), key)) {
				present = true;
				
				break;
			}
		}
		
		wi_dictionary_unlock(wd_users);
	}
	
	if(!present) {
		wi_array_wrlock(wd_transfers);
		
		for(i = 0; i < wi_array_count(wd_transfers); i++) {
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
					wi_mutable_array_remove_data(wd_transfers, transfer);
					wd_user_set_state(transfer->user, WD_USER_DISCONNECTED);

					update = true;
				}
			}
		}
		
		wi_array_unlock(wd_transfers);
	}
	
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
	wi_p7_message_t				*message;
	wi_mutable_set_t			*users;
	wi_mutable_dictionary_t		*user_queues;
	wi_mutable_array_t			*user_queue, *sorted_users, *user_transfers;
	wi_string_t					*key;
	wd_transfer_t				*transfer;
	wd_user_t					*user;
	wd_account_t				*account;
	wi_uinteger_t				position;
	wi_uinteger_t				i, count;
	wi_uinteger_t				total_downloads, total_uploads, user_downloads, user_uploads;
	wi_boolean_t				queue;
	
	wi_array_rdlock(wd_transfers);
	wi_lock_lock(wd_transfers_status_lock);
	wi_lock_lock(wd_transfers_update_queue_lock);
	
	total_downloads	= wi_config_integer_for_name(wd_config, WI_STR("total downloads"));
	total_uploads	= wi_config_integer_for_name(wd_config, WI_STR("total uploads"));
	users			= wi_set_init(wi_mutable_set_alloc());
	user_queues		= wi_dictionary_init(wi_mutable_dictionary_alloc());
	count			= wi_array_count(wd_transfers);
	
	for(i = 0; i < count; i++) {
		transfer = WI_ARRAY(wd_transfers, i);
		
		if(wd_transfer_state(transfer) == WD_TRANSFER_QUEUED) {
			user_queue = wi_dictionary_data_for_key(user_queues, transfer->key);
			
			if(!user_queue) {
				user_queue = wi_mutable_array();
				
				wi_mutable_dictionary_set_data_for_key(user_queues, user_queue, transfer->key);
			}
			
			wi_mutable_array_add_data(user_queue, transfer);
			wi_mutable_set_add_data(users, transfer->user);
		}
	}
	
	count = wi_set_count(users);
	
	if(count > 0) {
		sorted_users = wi_autorelease(wi_mutable_copy(wi_set_all_data(users)));
		
		wi_mutable_array_sort(sorted_users, wd_transfers_compare_user);
		
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
								wi_p7_message_set_uint64_for_name(message, transfer->dataoffset, WI_STR("wired.transfer.data_offset"));
								wi_p7_message_set_uint64_for_name(message, transfer->rsrcoffset, WI_STR("wired.transfer.rsrc_offset"));
								
								if(transfer->transaction > 0)
									wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
								
								wd_user_send_message(transfer->user, message);
							}
						}
					}
					
					wi_mutable_array_remove_data_at_index(user_queue, 0);
				} else {
					user_queue = NULL;
				}
				
				if(!user_queue || wi_array_count(user_queue) == 0) {
					wi_mutable_array_remove_data_at_index(sorted_users, i);
				
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
	transfer->datafd		= -1;
	transfer->rsrcfd		= -1;

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
	wi_release(transfer->realdatapath);
	wi_release(transfer->realrsrcpath);

	wi_release(transfer->state_lock);
	
	wi_release(transfer->finderinfo);
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



static inline void wd_transfer_limit_speed(wd_transfer_t *transfer, wi_uinteger_t totalspeed, wi_uinteger_t accountspeed, wi_uinteger_t count, ssize_t bytes, wi_time_interval_t now, wi_time_interval_t then) {
	wi_uinteger_t	limit, totallimit;
	
	if(totalspeed > 0 || accountspeed > 0) {
		totallimit = (totalspeed > 0 && count > 0)
			? (float) totalspeed / (float) count
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
	
	if(transfer->type == WD_TRANSFER_DOWNLOAD)
		wd_accounts_add_download_statistics(wd_user_account(transfer->user), transfer->finished, transfer->actualtransferred);
	else
		wd_accounts_add_upload_statistics(wd_user_account(transfer->user), transfer->finished, transfer->actualtransferred);

	wd_user_set_transfer(transfer->user, NULL);
	wd_user_set_state(transfer->user, WD_USER_LOGGED_IN);
	
	if(transfer->disconnected)
		wd_user_set_state(transfer->user, WD_USER_DISCONNECTED);

	wi_array_wrlock(wd_transfers);
	wi_mutable_array_remove_data(wd_transfers, transfer);
	wi_array_unlock(wd_transfers);

	wd_transfers_update_queue();
	
	wi_release(pool);
}



static wi_boolean_t wd_transfer_open(wd_transfer_t *transfer) {
	wi_p7_message_t		*message;

	if(transfer->type == WD_TRANSFER_DOWNLOAD)
		transfer->datafd = open(wi_string_cstring(transfer->realdatapath), O_RDONLY, 0);
	else
		transfer->datafd = open(wi_string_cstring(transfer->realdatapath), O_WRONLY | O_APPEND | O_CREAT, 0666);
	
	if(transfer->datafd < 0) {
		wi_log_err(WI_STR("Could not open %@: %s"),
			transfer->realdatapath, strerror(errno));
		
		message = wi_p7_message_with_name(WI_STR("wired.error"), wd_p7_spec);
		wi_p7_message_set_enum_name_for_name(message, WI_STR("wired.error.internal_error"), WI_STR("wired.error"));
		wi_p7_message_set_string_for_name(message, wi_string_with_cstring(strerror(errno)), WI_STR("wired.error.string"));
		
		if(transfer->transaction > 0)
			wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
		
		wd_user_send_message(transfer->user, message);
		
		return false;
	}
	
	if(transfer->realrsrcpath) {
		if(transfer->type == WD_TRANSFER_DOWNLOAD)
			transfer->rsrcfd = open(wi_string_cstring(transfer->realrsrcpath), O_RDONLY, 0);
		else
			transfer->rsrcfd = open(wi_string_cstring(transfer->realrsrcpath), O_WRONLY | O_APPEND | O_CREAT, 0666);
	}
	
	return true;
}



static void wd_transfer_close(wd_transfer_t *transfer) {
	if(transfer->datafd >= 0) {
		close(transfer->datafd);
		transfer->datafd = -1;
	}
	
	if(transfer->rsrcfd >= 0) {
		close(transfer->rsrcfd);
		transfer->rsrcfd = -1;
	}
}



static void wd_transfer_send_download_message(wd_transfer_t *transfer) {
	wi_p7_message_t			*message;
	wi_data_t				*data;
	
	message = wi_p7_message_with_name(WI_STR("wired.transfer.download"), wd_p7_spec);
	wi_p7_message_set_string_for_name(message, transfer->path, WI_STR("wired.file.path"));
	wi_p7_message_set_oobdata_for_name(message, transfer->remainingdatasize, WI_STR("wired.transfer.data"));
	wi_p7_message_set_oobdata_for_name(message, transfer->remainingrsrcsize, WI_STR("wired.transfer.rsrc"));
	
	data = wi_fs_finder_info_for_path(transfer->realdatapath);
	
	wi_p7_message_set_data_for_name(message, data ? data : wi_data(), WI_STR("wired.transfer.finderinfo"));
	
	if(transfer->transaction > 0)
		wi_p7_message_set_uint32_for_name(message, transfer->transaction, WI_STR("wired.transaction"));
	
	wd_user_send_message(transfer->user, message);
}



static void wd_transfer_post_process_upload_transfer(wd_transfer_t *transfer) {
	wi_string_t		*path;
	
	path = wi_string_by_deleting_path_extension(transfer->realdatapath);
	
	if(wi_fs_rename_path(transfer->realdatapath, path)) {
		if(transfer->executable) {
			if(!wi_fs_set_mode_for_path(path, 0755))
				wi_log_warn(WI_STR("Could not set mode for %@: %m"), path);
		}
		
		wd_files_move_comment(transfer->realdatapath, path, NULL, NULL);
		wd_files_move_label(transfer->realdatapath, path, NULL, NULL);
		
		if(wi_data_length(transfer->finderinfo) > 0)
			wi_fs_set_finder_info_for_path(transfer->finderinfo, path);
		
		wd_files_index_add_file(path);
	} else {
		wi_log_warn(WI_STR("Could not move %@ to %@: %m"),
			transfer->realdatapath, path);
	}
}



static void wd_transfer_download(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wd_account_t			*account;
	char					buffer[WD_TRANSFER_BUFFER_SIZE];
	wi_socket_state_t		state;
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_file_offset_t		sendbytes, speedbytes, statsbytes;
	wi_uinteger_t			i;
	wi_integer_t			result;
	ssize_t					readbytes;
	int						sd;
	wi_boolean_t			data;
	
	wi_log_info(WI_STR("Sending \"%@\" to %@"),
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));
	
	transfer->remainingdatasize		= transfer->datasize - transfer->dataoffset;
	transfer->remainingrsrcsize		= transfer->rsrcsize - transfer->rsrcoffset;
	interval						= wi_time_interval();
	speedinterval					= interval;
	statusinterval					= interval;
	accountinterval					= interval;
	speedbytes						= 0;
	statsbytes						= 0;
	i								= 0;
	socket							= wd_user_socket(transfer->user);
	sd								= wi_socket_descriptor(socket);
	p7_socket						= wd_user_p7_socket(transfer->user);
	account							= wd_user_account(transfer->user);
	data							= true;
	
	wd_transfer_send_download_message(transfer);
	
	wd_transfers_note_statistics(WD_TRANSFER_DOWNLOAD, WD_TRANSFER_STATISTICS_ADD, 0);
	wd_transfers_add_or_remove_transfer(transfer, true);
	
	lseek(transfer->datafd, transfer->dataoffset, SEEK_SET);
	
	if(transfer->rsrcfd >= 0)
		lseek(transfer->rsrcfd, transfer->rsrcoffset, SEEK_SET);
	
	pool = wi_pool_init(wi_pool_alloc());
	
	while(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING) {
		if(data && transfer->remainingdatasize == 0)
			data = false;
			  
		if(!data && transfer->remainingrsrcsize == 0)
			break;
		
		readbytes = read(data ? transfer->datafd : transfer->rsrcfd, buffer, sizeof(buffer));
		
		if(readbytes <= 0) {
			if(readbytes < 0) {
				wi_log_err(WI_STR("Could not read download from %@: %m"),
					data ? transfer->realdatapath : transfer->realrsrcpath, strerror(errno));
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

		if(data) {
			sendbytes = (transfer->remainingdatasize < (wi_file_offset_t) readbytes)
				? transfer->remainingdatasize
				: (wi_file_offset_t) readbytes;
		} else {
			sendbytes = (transfer->remainingrsrcsize < (wi_file_offset_t) readbytes)
				? transfer->remainingrsrcsize
				: (wi_file_offset_t) readbytes;
		}
		
		result = wi_p7_socket_write_oobdata(p7_socket, 30.0, buffer, sendbytes);
		
		if(!result) {
			wi_log_err(WI_STR("Could not write download to %@: %m"),
				wd_user_identifier(transfer->user));
			
			break;
		}

		if(data)
			transfer->remainingdatasize		-= sendbytes;
		else
			transfer->remainingrsrcsize		-= sendbytes;
		
		interval							= wi_time_interval();
		transfer->transferred				+= sendbytes;
		transfer->actualtransferred			+= sendbytes;
		speedbytes							+= sendbytes;
		statsbytes							+= sendbytes;
		transfer->speed						= speedbytes / (interval - speedinterval);

		wd_transfer_limit_speed(transfer,
								wd_transfers_total_download_speed,
								wd_account_transfer_download_speed_limit(account),
								wd_current_downloads,
								speedbytes,
								interval,
								speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		if(interval - statusinterval > wd_current_downloads) {
			wd_transfers_note_statistics(WD_TRANSFER_DOWNLOAD, WD_TRANSFER_STATISTICS_DATA, statsbytes);

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
		wd_files_string_for_bytes(transfer->transferred - transfer->dataoffset - transfer->rsrcoffset),
		wd_files_string_for_bytes((transfer->datasize - transfer->dataoffset) + (transfer->rsrcsize - transfer->rsrcoffset)),
		transfer->transferred - transfer->dataoffset - transfer->rsrcoffset,
		(transfer->datasize - transfer->dataoffset) + (transfer->rsrcsize - transfer->rsrcoffset),
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));
	
	wd_transfer_set_state(transfer, WD_TRANSFER_STOPPED);
 
	wd_transfers_add_or_remove_transfer(transfer, false);
	wd_transfers_note_statistics(WD_TRANSFER_DOWNLOAD, WD_TRANSFER_STATISTICS_REMOVE, statsbytes);

	wd_transfer_close(transfer);
}



static void wd_transfer_upload(wd_transfer_t *transfer) {
	wi_pool_t				*pool;
	wi_socket_t				*socket;
	wi_p7_socket_t			*p7_socket;
	wd_account_t			*account;
	void					*buffer;
	wi_time_interval_t		timeout, interval, speedinterval, statusinterval, accountinterval;
	wi_socket_state_t		state;
	ssize_t					result, speedbytes, statsbytes;
	wi_uinteger_t			i;
	wi_integer_t			readbytes;
	int						sd;
	wi_boolean_t			data;
	
	wi_log_info(WI_STR("Receiving \"%@\" from %@"),
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));
	
	transfer->remainingdatasize		= transfer->datasize - transfer->dataoffset;
	transfer->remainingrsrcsize		= transfer->rsrcsize - transfer->rsrcoffset;
	interval						= wi_time_interval();
	speedinterval					= interval;
	statusinterval					= interval;
	accountinterval					= interval;
	speedbytes						= 0;
	statsbytes						= 0;
	i								= 0;
	socket							= wd_user_socket(transfer->user);
	sd								= wi_socket_descriptor(socket);
	p7_socket						= wd_user_p7_socket(transfer->user);
	account							= wd_user_account(transfer->user);
	data							= true;
	
	wd_transfers_note_statistics(WD_TRANSFER_UPLOAD, WD_TRANSFER_STATISTICS_ADD, 0);
	wd_transfers_add_or_remove_transfer(transfer, true);

	pool = wi_pool_init(wi_pool_alloc());
	
	while(wd_transfer_state(transfer) == WD_TRANSFER_RUNNING) {
		if(transfer->remainingdatasize == 0)
			data = false;
		
		if(!data && transfer->remainingrsrcsize == 0)
			break;
		
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

		result = write(data ? transfer->datafd : transfer->rsrcfd, buffer, readbytes);
		
		if(result <= 0) {
			if(result < 0) {
				wi_log_err(WI_STR("Could not write upload to %@: %s"),
					data ? transfer->realdatapath : transfer->realrsrcpath, strerror(errno));
			}
			
			break;
		}

		if(data)
			transfer->remainingdatasize		-= readbytes;
		else
			transfer->remainingrsrcsize		-= readbytes;

		interval							= wi_time_interval();
		transfer->transferred				+= readbytes;
		transfer->actualtransferred			+= readbytes;
		speedbytes							+= readbytes;
		statsbytes							+= readbytes;
		transfer->speed						= speedbytes / (interval - speedinterval);

		wd_transfer_limit_speed(transfer,
								wd_transfers_total_upload_speed,
								wd_account_transfer_upload_speed_limit(account),
								wd_current_uploads,
								speedbytes,
								interval,
								speedinterval);
		
		if(interval - speedinterval > 30.0) {
			speedbytes = 0;
			speedinterval = interval;
		}

		if(interval - statusinterval > wd_current_uploads) {
			wd_transfers_note_statistics(WD_TRANSFER_UPLOAD, WD_TRANSFER_STATISTICS_DATA, statsbytes);

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
		wd_files_string_for_bytes(transfer->transferred - transfer->dataoffset - transfer->rsrcoffset),
		wd_files_string_for_bytes((transfer->datasize - transfer->dataoffset) + (transfer->rsrcsize - transfer->rsrcoffset)),
		transfer->transferred - transfer->dataoffset - transfer->rsrcoffset,
		(transfer->datasize - transfer->dataoffset) + (transfer->rsrcsize - transfer->rsrcoffset),
		wd_files_virtual_path(transfer->path, transfer->user),
		wd_user_identifier(transfer->user));

	wd_transfer_set_state(transfer, WD_TRANSFER_STOPPED);
	
	wd_transfers_add_or_remove_transfer(transfer, false);
	wd_transfers_note_statistics(WD_TRANSFER_UPLOAD, WD_TRANSFER_STATISTICS_REMOVE, statsbytes);
	
	transfer->finished = (transfer->transferred == (transfer->datasize + transfer->rsrcsize));
	
	if(transfer->finished)
		wd_transfer_post_process_upload_transfer(transfer);

	wd_transfer_close(transfer);
}
