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

#ifdef HAVE_CORESERVICES_CORESERVICES_H
#import <CoreFoundation/CoreFoundation.h>
#endif

#include <sys/param.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <wired/wired.h>

#include "accounts.h"
#include "files.h"
#include "main.h"
#include "server.h"
#include "settings.h"
#include "trackers.h"
#include "transfers.h"
#include "users.h"

#define WD_FILES_COMMENT_FIELD_SEPARATOR		"\34"
#define WD_FILES_COMMENT_SEPARATOR				"\35"
#define WD_FILES_PERMISSIONS_FIELD_SEPARATOR	"\34"

#define WD_FILES_INDEX_MAGIC					"WDIX"
#define WD_FILES_INDEX_VERSION					1


struct _wd_files_index_header {
	char										magic[4];
	uint32_t									version;
	uint32_t									files_count;
	uint32_t									directories_count;
	uint64_t									files_size;
};
typedef struct _wd_files_index_header			wd_files_index_header_t;


static wi_file_offset_t							wd_files_count_path(wi_string_t *, wd_user_t *, wi_p7_message_t *);

static void										wd_files_move_thread(wi_runtime_instance_t *);

static void										wd_files_index_update(wi_timer_t *);
static wi_boolean_t								wd_files_index_update_size(void);
static void										wd_files_index_thread(wi_runtime_instance_t *);
static void										wd_files_index_path_to_file(wi_string_t *, wi_file_t *, wi_string_t *);
static void										wd_files_index_write_entry(wi_file_t *, wi_string_t *, wd_file_type_t, uint64_t, wi_time_interval_t, wi_time_interval_t, wi_boolean_t, wi_boolean_t);

static void										wd_files_fsevents_thread(wi_runtime_instance_t *);
static void										wd_files_fsevents_callback(wi_string_t *);

static wd_file_type_t							wd_files_type_with_stat(wi_string_t *, wi_fs_stat_t *);

static void										wd_files_clear_comment(wi_string_t *, wd_user_t *, wi_p7_message_t *);
static wi_boolean_t								wd_files_read_comment(wi_file_t *, wi_string_t **, wi_string_t **);

static wi_boolean_t								wd_files_drop_box_path_is_xable(wi_string_t *, wd_user_t *, wi_uinteger_t);
static wi_boolean_t								wd_files_drop_box_path_is_listable(wi_string_t *, wd_account_t *);
static wi_string_t *							wd_files_drop_box_path_in_path(wi_string_t *, wd_user_t *);
static wi_boolean_t								wd_files_name_matches_query(wi_string_t *, wi_string_t *);


static wi_string_t								*wd_files;

static wi_time_interval_t						wd_files_index_time;
static wi_string_t								*wd_files_index_path;
static wi_timer_t								*wd_files_index_timer;
static wi_rwlock_t								*wd_files_index_lock;
static wi_lock_t								*wd_files_indexer_lock;
static wi_uinteger_t							wd_files_index_level;
static wi_dictionary_t							*wd_files_index_dictionary;

wi_fsevents_t									*wd_files_fsevents;

wi_uinteger_t									wd_files_count;
wi_uinteger_t									wd_directories_count;
wi_file_offset_t								wd_files_size;


void wd_files_init(void) {
	wd_files_index_path = WI_STR("index");
	
	wd_files_index_lock = wi_rwlock_init(wi_rwlock_alloc());
	wd_files_indexer_lock = wi_lock_init(wi_lock_alloc());
	
	wd_files_index_timer = wi_timer_init_with_function(wi_timer_alloc(),
													   wd_files_index_update,
													   0.0,
													   true);
	
	wd_files_fsevents = wi_fsevents_init(wi_fsevents_alloc());
	
	if(wd_files_fsevents)
		wi_fsevents_set_callback(wd_files_fsevents, wd_files_fsevents_callback);
	else
		wi_log_warn(WI_STR("Could not create fsevents: %m"));
}



void wd_files_apply_settings(wi_set_t *changes) {
	wi_release(wd_files);
	wd_files = wi_retain(wi_config_path_for_name(wd_config, WI_STR("files")));
}



void wd_files_schedule(void) {
	wd_files_index_time = wi_config_time_interval_for_name(wd_config, WI_STR("index time"));
	
	if(wd_files_index_time > 0.0)
		wi_timer_reschedule(wd_files_index_timer, wd_files_index_time);
	else
		wi_timer_invalidate(wd_files_index_timer);
	
	if(wd_files_fsevents) {
		if(!wi_thread_create_thread(wd_files_fsevents_thread, NULL))
			wi_log_err(WI_STR("Could not create an fsevents thread: %m"));
	}
}



#pragma mark -

void wd_files_reply_list(wi_string_t *path, wi_boolean_t recursive, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t				*reply;
	wi_string_t					*realpath, *filepath, *resolvedpath, *virtualpath;
	wi_fsenumerator_t			*fsenumerator;
	wd_account_t				*account;
	wi_fs_statfs_t				sfb;
	wi_fs_stat_t				sb, lsb;
	wi_fsenumerator_status_t	status;
	wi_file_offset_t			size, available;
	wi_uinteger_t				pathlength, depthlimit;
	wd_file_type_t				type, pathtype;
	wi_boolean_t				root, upload, alias, readable;
	
	root		= wi_is_equal(path, WI_STR("/"));
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	account		= wd_user_account(user);
	
	pathtype = wd_files_type(realpath);
	
	if(pathtype == WD_FILE_TYPE_DROPBOX) {
		if(!wd_files_drop_box_path_is_listable(realpath, account))
			goto done;
	}
	
	depthlimit = account->file_recursive_list_depth_limit;
	
	fsenumerator = wi_fs_enumerator_at_path(realpath);
	
	if(!fsenumerator) {
		wi_log_warn(WI_STR("Could not open %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return;
	}

	pathlength = wi_string_length(realpath);
	
	if(pathlength == 1)
		pathlength--;
	
	while((status = wi_fsenumerator_get_next_path(fsenumerator, &filepath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_warn(WI_STR("Could not list %@: %m"), filepath);
			
			continue;
		}
	
		if(depthlimit > 0 && wi_fsenumerator_level(fsenumerator) > depthlimit) {
			wi_fsenumerator_skip_descendents(fsenumerator);
			
			continue;
		}
		
		if(!recursive)
			wi_fsenumerator_skip_descendents(fsenumerator);
		
		virtualpath = wi_string_substring_from_index(filepath, pathlength);
		
		if(!root)
			wi_string_insert_string_at_index(virtualpath, path, 0);
		
		alias = wi_fs_path_is_alias(filepath);
		
		if(alias)
			resolvedpath = wi_string_by_resolving_aliases_in_path(filepath);
		else
			resolvedpath = filepath;

		if(!wi_fs_lstat_path(resolvedpath, &lsb)) {
			wi_log_warn(WI_STR("Could not list %@: %m"), resolvedpath);

			continue;
		}

		if(!wi_fs_stat_path(resolvedpath, &sb))
			sb = lsb;

		type = wd_files_type_with_stat(resolvedpath, &sb);
		
		if(type == WD_FILE_TYPE_DROPBOX)
			readable = wd_files_drop_box_path_is_listable(resolvedpath, account);
		else
			readable = true;

		switch(type) {
			case WD_FILE_TYPE_DIR:
			case WD_FILE_TYPE_UPLOADS:
			case WD_FILE_TYPE_DROPBOX:
				if(readable)
					size = wd_files_count_path(resolvedpath, user, message);
				else
					size = 0;
				break;

			case WD_FILE_TYPE_FILE:
			default:
				size = sb.size;
				break;
		}
		
		reply = wi_p7_message_init_with_name(wi_p7_message_alloc(), WI_STR("wired.file.list"), wd_p7_spec);
		wi_p7_message_set_string_for_name(reply, virtualpath, WI_STR("wired.file.path"));
		wi_p7_message_set_uint64_for_name(reply, size, WI_STR("wired.file.size"));
		wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
		wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
		wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
		wi_p7_message_set_bool_for_name(reply, alias || S_ISLNK(lsb.mode), WI_STR("wired.file.link"));
		wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
		wd_user_reply_message(user, reply, message);
		wi_release(reply);
		
		if(recursive && !readable) {
			wi_fsenumerator_skip_descendents(fsenumerator);
				
			continue;
		}
	}
	
done:
	if(account->transfer_upload_anywhere)
		upload = true;
	else if(pathtype == WD_FILE_TYPE_DROPBOX || pathtype == WD_FILE_TYPE_UPLOADS)
		upload = account->transfer_upload_files;
	else
		upload = false;

	if(upload && wi_fs_statfs_path(realpath, &sfb))
		available = (wi_file_offset_t) sfb.bavail * (wi_file_offset_t) sfb.frsize;
	else
		available = 0;
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.list.done"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	wi_p7_message_set_uint64_for_name(reply, available, WI_STR("wired.file.available"));
	wd_user_reply_message(user, reply, message);
}



static wi_file_offset_t wd_files_count_path(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	DIR						*dir;
	struct dirent			de, *dep;
	wi_file_offset_t		count = 0;
	
	dir = opendir(wi_string_cstring(path));
	
	if(!dir) {
		wi_log_warn(WI_STR("Could not open %@: %s"),
			path, strerror(errno));
		
		if(user)
			wd_user_reply_file_errno(user, message);
		
		return 0;
	}
	
	while(readdir_r(dir, &de, &dep) == 0 && dep) {
		if(dep->d_name[0] == '.')
			continue;
		
		count++;
	}

	closedir(dir);
	
	return count;
}




void wd_files_reply_info(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_string_t			*realpath, *comment, *owner, *group;
	wi_file_offset_t	size;
	wd_file_type_t		type;
	wi_fs_stat_t		sb, lsb;
	wi_uinteger_t		mode;
	wi_boolean_t		alias, readable;
	
	realpath = wd_files_real_path(path, user);
	alias = wi_fs_path_is_alias(realpath);
	
	if(alias)
		wi_string_resolve_aliases_in_path(realpath);

	if(!wi_fs_lstat_path(realpath, &lsb)) {
		wi_log_warn(WI_STR("Could not read info for %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return;
	}
	
	if(!wi_fs_stat_path(realpath, &sb))
		sb = lsb;

	type = wd_files_type_with_stat(realpath, &sb);
	
	if(type == WD_FILE_TYPE_DROPBOX)
		readable = wd_files_drop_box_path_is_listable(realpath, wd_user_account(user));
	else
		readable = true;
	
	comment = wd_files_comment(path, user);
	
	switch(type) {
		case WD_FILE_TYPE_DIR:
		case WD_FILE_TYPE_UPLOADS:
		case WD_FILE_TYPE_DROPBOX:
			if(readable)
				size = wd_files_count_path(realpath, user, message);
			else
				size = 0;
			break;

		case WD_FILE_TYPE_FILE:
		default:
			size = sb.size;
			break;
	}
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.info"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
	wi_p7_message_set_uint64_for_name(reply, size, WI_STR("wired.file.size"));
	wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
	wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
	wi_p7_message_set_string_for_name(reply, comment, WI_STR("wired.file.comment"));
	wi_p7_message_set_bool_for_name(reply, (alias || S_ISLNK(lsb.mode)), WI_STR("wired.file.link"));
	wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		if(!wd_files_get_permissions(realpath, &owner, &group, &mode)) {
			owner = WI_STR("");
			group = WI_STR("");
			mode = WD_FILE_EVERYONE_WRITE;
		}
		
		wi_p7_message_set_string_for_name(reply, owner, WI_STR("wired.file.owner"));
		wi_p7_message_set_bool_for_name(reply, (mode & WD_FILE_OWNER_WRITE), WI_STR("wired.file.owner.write"));
		wi_p7_message_set_bool_for_name(reply, (mode & WD_FILE_OWNER_READ), WI_STR("wired.file.owner.read"));
		wi_p7_message_set_string_for_name(reply, group, WI_STR("wired.file.group"));
		wi_p7_message_set_bool_for_name(reply, (mode & WD_FILE_GROUP_WRITE), WI_STR("wired.file.group.write"));
		wi_p7_message_set_bool_for_name(reply, (mode & WD_FILE_GROUP_READ), WI_STR("wired.file.group.read"));
		wi_p7_message_set_bool_for_name(reply, (mode & WD_FILE_EVERYONE_WRITE), WI_STR("wired.file.everyone.write"));
		wi_p7_message_set_bool_for_name(reply, (mode & WD_FILE_EVERYONE_READ), WI_STR("wired.file.everyone.read"));
	}
	
	wd_user_reply_message(user, reply, message);
}



wi_boolean_t wd_files_create_path(wi_string_t *path, wd_file_type_t type, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_create_directory(realpath, 0777)) {
		wi_log_warn(WI_STR("Could not create %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(type != WD_FILE_TYPE_DIR)
		wd_files_set_type(path, type, user, message);
	
	return true;
}



wi_boolean_t wd_files_delete_path(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *component;
	wi_boolean_t	result;
	
	realpath	= wd_files_real_path(path, user);
	component	= wi_string_last_path_component(realpath);

	wi_string_delete_last_path_component(realpath);
	wi_string_resolve_aliases_in_path(realpath);
	wi_string_append_path_component(realpath, component);
	
	result = wi_fs_delete_path(realpath);
	
	if(result) {
		wd_files_clear_comment(path, user, message);
	} else {
		wi_log_warn(WI_STR("Could not delete %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
	}
	
	return result;
}



wi_boolean_t wd_files_move_path(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_array_t			*array;
	wi_string_t			*realfrompath, *realtopath;
	wi_string_t			*realfromname, *realtoname;
	wi_string_t			*path;
	wi_fs_stat_t		sb;
	wi_boolean_t		result = false;
	
	realfrompath	= wd_files_real_path(frompath, user);
	realtopath		= wd_files_real_path(topath, user);
	realfromname	= wi_string_last_path_component(realfrompath);
	realtoname		= wi_string_last_path_component(realtopath);

	wi_string_resolve_aliases_in_path(realfrompath);
	wi_string_delete_last_path_component(realtopath);
	wi_string_resolve_aliases_in_path(realtopath);
	wi_string_append_path_component(realtopath, realtoname);
	
	if(!wi_fs_lstat_path(realfrompath, &sb)) {
		wi_log_warn(WI_STR("Could not rename %@: %m"), realfrompath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(wi_string_case_insensitive_compare(realfrompath, realtopath) == 0) {
		path = wi_fs_temporary_path_with_template(
			wi_string_with_format(WI_STR("%@/.%@.XXXXXXXX"),
				  wi_string_by_deleting_last_path_component(realfrompath),
				  realfromname));
		
		if(path) {
			result = wi_fs_rename_path(realfrompath, path);
		
			if(result)
				result = wi_fs_rename_path(path, realtopath);
		}
	} else {
		if(wi_fs_lstat_path(realtopath, &sb)) {
			wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

			return false;
		}
		
		result = wi_fs_rename_path(realfrompath, realtopath);
	}
	
	if(result) {
		wd_files_move_comment(frompath, topath, user, message);
	} else {
		if(wi_error_code() == EXDEV) {
			array = wi_array_init_with_data(wi_array_alloc(),
				frompath,
				topath,
				realfrompath,
				realtopath,
				(void *) NULL);
			
			result = wi_thread_create_thread(wd_files_move_thread, array);
			
			if(!result) {
				wi_log_err(WI_STR("Could not create a copy thread: %m"));
				wd_user_reply_error(user, WI_STR("wired.error.internal_error"), message);
			}
			
			wi_release(array);
		} else {
			wi_log_warn(WI_STR("Could not rename %@ to %@: %m"),
				realfrompath, realtopath);
			wd_user_reply_file_errno(user, message);
		}
	}
	
	return result;
}



static void wd_files_move_thread(wi_runtime_instance_t *argument) {
	wi_pool_t		*pool;
	wi_array_t		*array = argument;
	wi_string_t		*frompath, *topath, *realfrompath, *realtopath;
	
	pool			= wi_pool_init(wi_pool_alloc());
	frompath		= WI_ARRAY(array, 0);
	topath			= WI_ARRAY(array, 1);
	realfrompath	= WI_ARRAY(array, 2);
	realtopath		= WI_ARRAY(array, 3);
	
	if(wi_fs_copy_path(realfrompath, realtopath)) {
		if(wi_fs_delete_path(realfrompath))
			wd_files_move_comment(frompath, topath, NULL, NULL);
		else
			wi_log_warn(WI_STR("Could not delete %@: %m"), realfrompath);
	} else {
		wi_log_warn(WI_STR("Could not copy %@ to %@: %m"), realfrompath, realtopath);
	}
	
	wi_release(pool);
}



wi_boolean_t wd_files_link_path(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realfrompath, *realtopath;
	wi_string_t			*realfromname, *realtoname;
	wi_fs_stat_t		sb;
	
	realfrompath	= wd_files_real_path(frompath, user);
	realtopath		= wd_files_real_path(topath, user);
	realfromname	= wi_string_last_path_component(realfrompath);
	realtoname		= wi_string_last_path_component(realtopath);

	wi_string_delete_last_path_component(realfrompath);
	wi_string_resolve_aliases_in_path(realfrompath);
	wi_string_resolve_aliases_in_path(realtopath);
	wi_string_append_path_component(realfrompath, realfromname);
	
	if(!wi_fs_lstat_path(realfrompath, &sb)) {
		wi_log_warn(WI_STR("Could not link %@: %m"), realfrompath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(wi_fs_lstat_path(realtopath, &sb)) {
		wd_user_reply_error(user, WI_STR("wired.error.file_exists"), message);

		return false;
	}
	
	if(!wi_fs_symlink_path(realfrompath, realtopath)) {
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	return true;
}



#pragma mark -

void wd_files_search(wi_string_t *query, wd_user_t *user, wi_p7_message_t *message) {
	wi_pool_t			*pool;
	wi_p7_message_t		*reply;
	wi_file_t			*file;
	wi_string_t			*name;
	wd_account_t		*account;
	char				*buffer = NULL;
	wi_uinteger_t		i = 0, pathlength, bufferlength = 0, messagelength;
	uint32_t			entrylength, namelength;
	
	wi_rwlock_rdlock(wd_files_index_lock);
	
	file = wi_file_for_reading(wd_files_index_path);
	
	if(!file) {
		wi_log_warn(WI_STR("Could not open %@: %m"), wd_files_index_path);
		wd_user_reply_file_errno(user, message);

		goto end;
	}
	
	account = wd_user_account(user);

	if(account->files)
		pathlength = wi_string_length(account->files);
	else
		pathlength = 0;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	wi_file_seek(file, sizeof(wd_files_index_header_t));

	while(wi_file_read_buffer(file, &entrylength, sizeof(entrylength)) > 0) {
		if(!buffer) {
			bufferlength = entrylength * 2;
			buffer = wi_malloc(bufferlength);
		}
		else if(entrylength > bufferlength) {
			bufferlength = entrylength * 2;
			buffer = wi_realloc(buffer, bufferlength);
		}
		
		if(wi_file_read_buffer(file, buffer, entrylength) != (wi_integer_t) entrylength)
			break;
		
		namelength		= *(uint32_t *) (uintptr_t) buffer;
		name			= wi_string_init_with_bytes(wi_string_alloc(), buffer + sizeof(namelength), namelength);
		messagelength	= entrylength - sizeof(namelength) - namelength;

		if(wd_files_name_matches_query(name, query)) {
			reply = wi_p7_message_with_bytes(buffer + sizeof(namelength) + namelength, messagelength, WI_P7_BINARY, wd_p7_spec);

			if(reply)
				wd_user_reply_message(user, reply, message);
		}
		
		wi_release(name);

		if(++i % 100 == 0)
			wi_pool_drain(pool);
	}

	if(buffer)
		wi_free(buffer);

	wi_release(pool);

end:
	reply = wi_p7_message_with_name(WI_STR("wired.file.search_list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
	
	wi_rwlock_unlock(wd_files_index_lock);
}



#pragma mark -

static void wd_files_index_update(wi_timer_t *timer) {
	wd_files_index(false, true);
}



void wd_files_index(wi_boolean_t startup, wi_boolean_t force) {
	wi_fs_stat_t		sb;
	wi_time_interval_t	interval, index_time;
	wi_boolean_t		index = true;
	
	if(!force) {
		if(wi_fs_stat_path(wd_files_index_path, &sb)) {
			interval = wi_date_time_interval_since_now(wi_date_with_time(sb.mtime));
			index_time  = (wd_files_index_time > 0.0) ? wd_files_index_time : 3600.0;
			
			if(interval < index_time) {
				wi_log_info(WI_STR("Reusing existing index created %.2f seconds ago"), interval);
				
				index = false;
			}
		}
	}
	
	if(startup) {
		if(!wd_files_index_update_size())
			index = true;
		
		wd_trackers_register();
	}

	if(index) {
		if(!wi_thread_create_thread_with_priority(wd_files_index_thread, wi_number_with_bool(startup), 0.0))
			wi_log_warn(WI_STR("Could not create an index thread: %m"));
	}
}



static wi_boolean_t wd_files_index_update_size(void) {
	wi_file_t					*file;
	wd_files_index_header_t		header;
	
	file = wi_file_for_reading(wd_files_index_path);
	
	if(!file)
		return false;
	
	if(wi_file_read_buffer(file, &header, sizeof(header)) != sizeof(header))
		return false;
	
	if(strcmp(header.magic, WD_FILES_INDEX_MAGIC) && header.version == WD_FILES_INDEX_VERSION) {
		wd_files_count			= header.files_count;
		wd_directories_count	= header.directories_count;
		wd_files_size			= header.files_size;

		wi_log_info(WI_STR("Found %u %s and %u %s for a total of %llu %s in %@"),
			wd_files_count,
			wd_files_count == 1
				? "file"
				: "files",
			wd_directories_count,
			wd_directories_count == 1
				? "directory"
				: "directories",
			wd_files_size,
			wd_files_size == 1
				? "byte"
				: "bytes",
			wd_files_index_path);
		
		return true;
	} else {
		wi_log_info(WI_STR("Could not read %@: Wrong magic or version"), wd_files_index_path);
		
		return false;
	}
}



static void wd_files_index_thread(wi_runtime_instance_t *argument) {
	wi_pool_t					*pool;
	wi_file_t					*file;
	wi_string_t					*path;
	wd_files_index_header_t		header = { WD_FILES_INDEX_MAGIC, WD_FILES_INDEX_VERSION, 0, 0, 0 };
	wi_time_interval_t			interval;
	wi_boolean_t				startup = wi_number_bool(argument);
	
	pool = wi_pool_init(wi_pool_alloc());
	
	if(wi_lock_trylock(wd_files_indexer_lock)) {
		wi_log_info(WI_STR("Indexing files..."));
		
		interval				= wi_time_interval();
		wd_files_count			= 0;
		wd_directories_count	= 0;
		wd_files_size			= 0;
		wd_files_index_level	= 0;
		
		wd_files_index_dictionary = wi_dictionary_init_with_capacity_and_callbacks(wi_dictionary_alloc(), 0,
			wi_dictionary_null_key_callbacks, wi_dictionary_default_value_callbacks);
		
		path = wi_string_with_format(WI_STR("%@~"), wd_files_index_path);
		file = wi_file_for_writing(path);
		
		if(!file) {
			wi_log_warn(WI_STR("Could not open %@: %m"), path);
		} else {
			wi_file_write_buffer(file, &header, sizeof(header));
			
			wd_files_index_path_to_file(wi_string_by_resolving_aliases_in_path(wd_files), file, NULL);
			
			header.files_count			= wd_files_count;
			header.directories_count	= wd_directories_count;
			header.files_size			= wd_files_size;
			
			wi_file_seek(file, 0);
			wi_file_write_buffer(file, &header, sizeof(header));
			
			wi_rwlock_wrlock(wd_files_index_lock);
			
			if(wi_fs_rename_path(path, wd_files_index_path)) {
				wi_log_info(WI_STR("Indexed %u %s and %u %s for a total of %llu %s in %.2f seconds"),
					wd_files_count,
					wd_files_count == 1
						? "file"
						: "files",
					wd_directories_count,
					wd_directories_count == 1
						? "directory"
						: "directories",
					wd_files_size,
					wd_files_size == 1
						? "byte"
						: "bytes",
					wi_time_interval() - interval);
			} else {
				wi_log_warn(WI_STR("Could not rename %@ to %@: %m"),
					path, wd_files_index_path);
			}
			
			wi_rwlock_unlock(wd_files_index_lock);
			
			wd_broadcast_message(wd_server_info_message());
			
			if(startup)
				wd_trackers_register();
		}
		
		wi_lock_unlock(wd_files_indexer_lock);
	}
	
	wi_release(pool);
}



static void wd_files_index_path_to_file(wi_string_t *path, wi_file_t *file, wi_string_t *pathprefix) {
	wi_pool_t					*pool;
	wi_fsenumerator_t			*fsenumerator;
	wi_string_t					*filepath, *virtualpath, *resolvedpath, *newpathprefix;
	wi_set_t					*set;
	wi_number_t					*number;
	wi_file_offset_t			size;
	wi_fs_stat_t				sb, lsb;
	wi_fsenumerator_status_t	status;
	wi_uinteger_t				i = 0, pathlength;
	wi_boolean_t				alias, recurse;
	wd_file_type_t				type;
	
	if(wd_files_index_level >= WD_FILES_MAX_LEVEL) {
		wi_log_warn(WI_STR("Skipping index of %@: %s"),
			path, "Directory too deep");
		
		return;
	}

	fsenumerator = wi_fs_enumerator_at_path(path);

	if(!fsenumerator) {
		wi_log_warn(WI_STR("Could not open %@: %m"), path);
		
		return;
	}
	
	pool = wi_pool_init_with_debug(wi_pool_alloc(), false);
	
	pathlength = wi_string_length(path);
	
	if(pathlength == 1)
		pathlength--;
	
	wd_files_index_level++;

	while((status = wi_fsenumerator_get_next_path(fsenumerator, &filepath)) != WI_FSENUMERATOR_EOF) {
		if(status == WI_FSENUMERATOR_ERROR) {
			wi_log_warn(WI_STR("Skipping index of %@: %m"), path);
				
			continue;
		}

		alias = wi_fs_path_is_alias(filepath);
		
		if(alias)
			resolvedpath = wi_string_by_resolving_aliases_in_path(filepath);
		else
			resolvedpath = filepath;
		
		if(!wi_fs_lstat_path(resolvedpath, &lsb)) {
			wi_log_warn(WI_STR("Skipping index of %@: %m"), resolvedpath);
			wi_fsenumerator_skip_descendents(fsenumerator);
		} else {
			if(!wi_fs_stat_path(resolvedpath, &sb))
				sb = lsb;
			
			set = wi_dictionary_data_for_key(wd_files_index_dictionary, (void *) (intptr_t) lsb.dev);
			
			if(!set) {
				set = wi_set_init_with_capacity(wi_set_alloc(), 1000, false);
				wi_dictionary_set_data_for_key(wd_files_index_dictionary, set, (void *) (intptr_t) lsb.dev);
				wi_release(set);
			}
			
			number = wi_number_init_with_value(wi_number_alloc(), WI_NUMBER_INT64, &lsb.ino);
			
			if(!wi_set_contains_data(set, number)) {
				wi_set_add_data(set, number);
				
				recurse = (alias && S_ISDIR(sb.mode));
				
				type = wd_files_type_with_stat(resolvedpath, &sb);
				
				switch(type) {
					case WD_FILE_TYPE_DROPBOX:
						size = 0;
						break;
						
					case WD_FILE_TYPE_DIR:
					case WD_FILE_TYPE_UPLOADS:
						size = wd_files_count_path(resolvedpath, NULL, NULL);
						break;
						
					case WD_FILE_TYPE_FILE:
					default:
						size = sb.size;
						break;
				}
				
				virtualpath	= wi_string_substring_from_index(filepath, pathlength);
				
				if(pathprefix)
					wi_string_insert_string_at_index(virtualpath, pathprefix, 0);
				
				wd_files_index_write_entry(file,
										   virtualpath,
										   type,
										   size,
										   sb.birthtime,
										   sb.mtime,
										   (alias || S_ISLNK(lsb.mode)),
										   (type == WD_FILE_TYPE_FILE && sb.mode & 0111));

				if(S_ISDIR(sb.mode)) {
					wd_directories_count++;
				} else {
					wd_files_count++;
					wd_files_size += size;
				}
				
				if(type == WD_FILE_TYPE_DROPBOX) {
					wi_fsenumerator_skip_descendents(fsenumerator);
				}
				else if(recurse) {
					if(pathprefix)
						newpathprefix = wi_string_by_appending_path_component(pathprefix, wi_string_substring_from_index(filepath, pathlength + 1));
					else
						newpathprefix = wi_string_substring_from_index(filepath, pathlength);
					
					wd_files_index_path_to_file(resolvedpath, file, newpathprefix);
				}
			}
		
			wi_release(number);
		}

		if(++i % 100 == 0)
			wi_pool_drain(pool);
	}
	
	wd_files_index_level--;
	
	wi_release(pool);
}



static void wd_files_index_write_entry(wi_file_t *file, wi_string_t *path, wd_file_type_t type, uint64_t size, wi_time_interval_t creationtime, wi_time_interval_t modificationtime, wi_boolean_t link, wi_boolean_t executable) {
	static char				*buffer;
	static wi_uinteger_t	bufferlength;
	static uint32_t			searchlistid, pathid, typeid, sizeid, creationid, modificationid, linkid, executableid;
	wi_string_t				*name, *creationstring, *modificationstring;
	uint32_t				entrylength, namelength, pathlength, creationlength, modificationlength;
	char					*p;
	
	if(searchlistid == 0) {
		searchlistid	= wi_p7_spec_message_id(wi_p7_spec_message_with_name(wd_p7_spec, WI_STR("wired.file.search_list")));
		pathid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.path")));
		typeid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.type")));
		sizeid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.size")));
		creationid		= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.creation_time")));
		modificationid	= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.modification_time")));
		linkid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.link")));
		executableid	= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.executable")));
	}

	name				= wi_string_last_path_component(path);
	creationstring		= wi_time_interval_rfc3339_string(creationtime);
	modificationstring	= wi_time_interval_rfc3339_string(modificationtime);

	namelength			= wi_string_length(name) + 1;
	pathlength			= wi_string_length(path) + 1;
	creationlength		= wi_string_length(creationstring) + 1;
	modificationlength	= wi_string_length(modificationstring) + 1;
	entrylength			= sizeof(entrylength) +
						  sizeof(namelength) + namelength +
						  sizeof(searchlistid) + 
						  sizeof(pathid) + sizeof(pathlength) + pathlength +
						  sizeof(typeid) + sizeof(type) + 
						  sizeof(sizeid) + sizeof(size) +
						  sizeof(creationid) + creationlength +
						  sizeof(modificationid) + modificationlength +
						  sizeof(linkid) + 1 +
						  sizeof(executableid) + 1;
	
	if(!buffer) {
		bufferlength = entrylength * 2;
		buffer = wi_malloc(bufferlength);
	}
	else if(bufferlength < entrylength) {
		bufferlength = entrylength * 2;
		buffer = wi_realloc(buffer, bufferlength);
	}
	
	p = buffer;
	
	memcpy(p, &entrylength, sizeof(entrylength));							p += sizeof(entrylength);
	memcpy(p, &namelength, sizeof(namelength));								p += sizeof(namelength);
	memcpy(p, wi_string_cstring(name), namelength);							p += namelength;
	wi_write_swap_host_to_big_int32(p, 0, searchlistid);					p += sizeof(searchlistid);
	wi_write_swap_host_to_big_int32(p, 0, pathid);							p += sizeof(pathid);
	wi_write_swap_host_to_big_int32(p, 0, pathlength);						p += sizeof(pathlength);
	memcpy(p, wi_string_cstring(path), pathlength);							p += pathlength;
	wi_write_swap_host_to_big_int32(p, 0, typeid);							p += sizeof(typeid);
	wi_write_swap_host_to_big_int32(p, 0, type);							p += sizeof(type);
	wi_write_swap_host_to_big_int32(p, 0, sizeid);							p += sizeof(sizeid);
	wi_write_swap_host_to_big_int64(p, 0, size);							p += sizeof(size);
	wi_write_swap_host_to_big_int32(p, 0, creationid);						p += sizeof(creationid);
	memcpy(p, wi_string_cstring(creationstring), creationlength);			p += creationlength;
	wi_write_swap_host_to_big_int32(p, 0, modificationid);					p += sizeof(modificationid);
	memcpy(p, wi_string_cstring(modificationstring), modificationlength);	p += modificationlength;
	wi_write_swap_host_to_big_int32(p, 0, linkid);							p += sizeof(linkid);
	memcpy(p, link ? "\1" : "\0", 1);										p += 1;
	wi_write_swap_host_to_big_int32(p, 0, executableid);					p += sizeof(executableid);
	memcpy(p, executable ? "\1" : "\0", 1);									p += 1;
	
	wi_file_write_buffer(file, buffer, sizeof(entrylength) + entrylength);
}



#pragma mark -

static void wd_files_fsevents_thread(wi_runtime_instance_t *instance) {
	wi_pool_t		*pool;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	while(true) {
		if(!wi_fsevents_run_with_timeout(wd_files_fsevents, 0.0))
			wi_log_info(WI_STR("Could not listen on fsevents: %m"));
		
		wi_pool_drain(pool);
	}
	
	wi_release(pool);
}



static void wd_files_fsevents_callback(wi_string_t *path) {
	wi_pool_t			*pool;
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*message;
	wi_string_t			*virtualpath;
	wd_user_t			*user;
	wi_uinteger_t		pathlength;
	
	pool = wi_pool_init(wi_pool_alloc());
	
	pathlength = wi_string_length(wd_files);
	
	if(pathlength == wi_string_length(path))
		virtualpath = WI_STR("/");
	else
		virtualpath = wi_string_substring_from_index(path, pathlength);
	
	wi_dictionary_rdlock(wd_users);

	enumerator = wi_dictionary_data_enumerator(wd_users);
	
	while((user = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(user) == WD_USER_LOGGED_IN && wi_set_contains_data(wd_user_subscribed_paths(user), path)) {
			message = wi_p7_message_with_name(WI_STR("wired.file.directory_changed"), wd_p7_spec);
			wi_p7_message_set_string_for_name(message, virtualpath, WI_STR("wired.file.path"));
			wd_user_send_message(user, message);
		}
	}
	
	wi_dictionary_unlock(wd_users);
	
	wi_release(pool);
}



#pragma mark -

wd_file_type_t wd_files_type(wi_string_t *path) {
	wi_fs_stat_t	sb;
	
	if(!wi_fs_stat_path(path, &sb)) {
		wi_log_warn(WI_STR("Could not read type for %@: %m"), path);
		
		return WD_FILE_TYPE_FILE;
	}
	
	return wd_files_type_with_stat(path, &sb);
}



static wd_file_type_t wd_files_type_with_stat(wi_string_t *realpath, wi_fs_stat_t *sbp) {
	wi_string_t		*typepath, *string;
	wi_fs_stat_t	sb;
	wd_file_type_t	type;
	
	if(!S_ISDIR(sbp->mode))
		return WD_FILE_TYPE_FILE;
	
	typepath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(!wi_fs_stat_path(typepath, &sb) || sb.size > 8)
		return WD_FILE_TYPE_DIR;
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), typepath));
	
	if(!string)
		return WD_FILE_TYPE_DIR;
	
	wi_string_delete_surrounding_whitespace(string);
	
	type = wi_string_uint32(string);
	
	if(type == WD_FILE_TYPE_FILE)
		type = WD_FILE_TYPE_DIR;
	
	return type;
}



void wd_files_set_type(wi_string_t *path, wd_file_type_t type, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *metapath, *typepath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	typepath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(type != WD_FILE_TYPE_DIR) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				wd_user_reply_file_errno(user, message);

				return;
			}
		}
		
		if(!wi_string_write_to_file(wi_string_with_format(WI_STR("%u\n"), type), typepath)) {
			wi_log_warn(WI_STR("Could not write to %@: %m"), typepath);
			wd_user_reply_file_errno(user, message);
		}
	} else {
		if(wi_fs_delete_path(typepath))
			(void) rmdir(wi_string_cstring(metapath));
	}
}



#pragma mark -

void wd_files_set_executable(wi_string_t *path, wi_boolean_t executable, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_set_mode_for_path(realpath, executable ? 0755 : 0644)) {
		wi_log_warn(WI_STR("Could not set mode for %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
	}
}



#pragma mark -

wi_string_t * wd_files_comment(wi_string_t *path, wd_user_t *user) {
	wi_file_t		*file;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t		*realpath, *comment;
#endif
	wi_string_t		*name, *dirpath, *realdirpath, *commentpath;
	wi_string_t		*eachname, *eachcomment;

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	comment		= wi_fs_finder_comment_for_path(realpath);
	
	if(comment)
		return comment;
#endif

	name		= wi_string_last_path_component(path);
	dirpath		= wi_string_by_deleting_last_path_component(path);
	realdirpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	commentpath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	file		= wi_file_for_reading(commentpath);
	
	if(!file)
		return NULL;

	while(wd_files_read_comment(file, &eachname, &eachcomment)) {
		if(wi_is_equal(name, eachname))
			return eachcomment;
	}
	
	return NULL;
}



void wd_files_set_comment(wi_string_t *path, wi_string_t *comment, wd_user_t *user, wi_p7_message_t *message) {
	wi_file_t		*file, *tmpfile;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t		*realpath;
#endif
	wi_string_t		*name, *dirpath, *realdirpath, *metapath, *commentpath;
	wi_string_t		*string, *eachname, *eachcomment;
	wi_uinteger_t	comments = 0;
	
	name		= wi_string_last_path_component(path);
	dirpath		= wi_string_by_deleting_last_path_component(path);
	realdirpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	commentpath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	
	if(comment && wi_string_length(comment) > 0) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				
				if(user)
					wd_user_reply_file_errno(user, message);
				
				return;
			}
		}
	}
	
	file = wi_file_for_updating(commentpath);
	
	if(!file)
		return;
	
	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_warn(WI_STR("Could not create a temporary file: %m"));
		
		if(user)
			wd_user_reply_file_errno(user, message);

		return;
	}
	
	if(comment && wi_string_length(comment) > 0) {
		wi_file_write_format(tmpfile, WI_STR("%#@%s%#@%s"),
							 name,		WD_FILES_COMMENT_FIELD_SEPARATOR,
							 comment,	WD_FILES_COMMENT_SEPARATOR);
		comments++;
	}
	
	while(wd_files_read_comment(file, &eachname, &eachcomment)) {
		if(!wi_is_equal(name, eachname)) {
			wi_file_write_format(tmpfile, WI_STR("%#@%s%#@%s"),
								 eachname,		WD_FILES_COMMENT_FIELD_SEPARATOR,
								 eachcomment,	WD_FILES_COMMENT_SEPARATOR);
		}
		
		comments++;
	}
	
	if(comments > 0) {
		wi_file_truncate(file, 0);
		wi_file_seek(tmpfile, 0);
	
		while((string = wi_file_read(tmpfile, WI_FILE_BUFFER_SIZE)))
			wi_file_write_format(file, WI_STR("%@"), string);

		wi_file_close(file);
		wi_file_close(tmpfile);
	} else {
		wi_file_close(file);
		wi_file_close(tmpfile);
		
		if(wi_fs_delete_path(commentpath))
			(void) rmdir(wi_string_cstring(metapath));
	}

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_comment_for_path(realpath, comment))
			wi_log_err(WI_STR("Could not set Finder comment: %m"));
	}
#endif
}



static void wd_files_clear_comment(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wd_files_set_comment(path, NULL, user, message);
}



void wd_files_move_comment(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*comment;
	
	comment = wd_files_comment(frompath, user);
	
	if(comment) {
		wd_files_set_comment(frompath, NULL, user, message);
		wd_files_set_comment(topath, comment, user, message);
	}
}



static wi_boolean_t wd_files_read_comment(wi_file_t *file, wi_string_t **name, wi_string_t **comment) {
	wi_array_t		*array;
	wi_string_t		*string;

	string = wi_file_read_to_string(file, WI_STR(WD_FILES_COMMENT_SEPARATOR));
	
	if(string) {
		array = wi_string_components_separated_by_string(string, WI_STR(WD_FILES_COMMENT_FIELD_SEPARATOR));
	
		if(wi_array_count(array) == 2) {
			*name		= WI_ARRAY(array, 0);
			*comment	= WI_ARRAY(array, 1);
			
			return true;
		}
	}
	
	return false;
}



#pragma mark -

wi_boolean_t wd_files_get_permissions(wi_string_t *realpath, wi_string_t **owner, wi_string_t **group, wi_uinteger_t *mode) {
	wi_string_t		*permissionspath, *string;
	wi_array_t		*array;
	wi_fs_stat_t	sb;
	
	permissionspath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PERMISSIONS_PATH));
	
	if(!wi_fs_stat_path(permissionspath, &sb) || sb.size > 128)
		return false;
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), permissionspath));
	
	if(!string)
		return false;
	
	wi_string_delete_surrounding_whitespace(string);
	
	array = wi_string_components_separated_by_string(string, WI_STR(WD_FILES_PERMISSIONS_FIELD_SEPARATOR));
	
	if(wi_array_count(array) != 3)
		return false;
	
	*owner = WI_ARRAY(array, 0);
	*group = WI_ARRAY(array, 1);
	*mode = wi_string_uint32(WI_ARRAY(array, 2));
	
	return true;
}



void wd_files_set_permissions(wi_string_t *path, wi_string_t *owner, wi_string_t *group, wi_uinteger_t mode, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *metapath, *permissionspath;
	wi_string_t		*string;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	permissionspath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PERMISSIONS_PATH));
	
	if(wi_string_length(owner) > 0 || wi_string_length(group) > 0 || mode != WD_FILE_EVERYONE_WRITE) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				wd_user_reply_file_errno(user, message);

				return;
			}
		}
		
		string = wi_string_with_format(WI_STR("%#@%s%#@%s%u\n"),
			owner,			WD_FILES_PERMISSIONS_FIELD_SEPARATOR,
			group,			WD_FILES_PERMISSIONS_FIELD_SEPARATOR,
			mode);
		
		if(!wi_string_write_to_file(string, permissionspath)) {
			wi_log_warn(WI_STR("Could not write to %@: %m"), permissionspath);
			wd_user_reply_file_errno(user, message);
		}
	} else {
		if(wi_fs_delete_path(permissionspath))
			(void) rmdir(wi_string_cstring(metapath));
	}
}



#pragma mark -

wi_boolean_t wd_files_path_is_valid(wi_string_t *path) {
	if(wi_string_has_prefix(path, WI_STR(".")))
		return false;

	if(wi_string_contains_string(path, WI_STR("/.."), 0))
        return false;

	if(wi_string_contains_string(path, WI_STR("../"), 0))
        return false;

	return true;
}



wi_boolean_t wd_files_drop_box_path_is_readable(wi_string_t *path, wd_user_t *user) {
	return wd_files_drop_box_path_is_xable(path, user, WD_FILE_OWNER_READ | WD_FILE_GROUP_READ | WD_FILE_EVERYONE_READ);
}



wi_boolean_t wd_files_drop_box_path_is_writable(wi_string_t *path, wd_user_t *user) {
	return wd_files_drop_box_path_is_xable(path, user, WD_FILE_OWNER_WRITE | WD_FILE_GROUP_WRITE | WD_FILE_EVERYONE_WRITE);
}



static wi_boolean_t wd_files_drop_box_path_is_listable(wi_string_t *realpath, wd_account_t *account) {
	wi_string_t		*owner, *group;
	wi_uinteger_t	mode;
	
	if(account->file_access_all_dropboxes)
		return true;
	
	if(!wd_files_get_permissions(realpath, &owner, &group, &mode))
		return false;
	
	if(mode & WD_FILE_EVERYONE_READ)
		return true;
	
	if(mode & WD_FILE_GROUP_READ && wi_string_length(group) > 0) {
		if(wi_is_equal(group, account->group) || wi_array_contains_data(account->groups, group))
			return true;
	}
	
	if(mode & WD_FILE_OWNER_READ && wi_string_length(owner) > 0) {
		if(wi_is_equal(owner, account->name))
			return true;
	}
	
	return false;
}



static wi_boolean_t wd_files_drop_box_path_is_xable(wi_string_t *path, wd_user_t *user, wi_uinteger_t inmode) {
	wi_string_t		*realpath, *owner, *group;
	wd_account_t	*account;
	wi_uinteger_t	mode;
	
	account = wd_user_account(user);
	
	if(account->file_access_all_dropboxes)
		return true;
	
	realpath = wd_files_drop_box_path_in_path(path, user);
	
	if(!realpath)
		return true;
	
	if(!wd_files_get_permissions(realpath, &owner, &group, &mode)) {
		if(inmode & WD_FILE_EVERYONE_WRITE)
			return true;
		
		return false;
	}
	
	if(mode & inmode)
		return true;
	
	if(mode & inmode && wi_string_length(group) > 0) {
		if(wi_is_equal(group, account->group) || wi_array_contains_data(account->groups, group))
			return true;
	}
	
	if(mode & inmode && wi_string_length(owner) > 0) {
		if(wi_is_equal(owner, account->name))
			return true;
	}
	
	return false;
}



static wi_string_t * wd_files_drop_box_path_in_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*realpath, *dirpath;
	wi_array_t		*array;
	wi_uinteger_t	i, count;
	
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(WI_STR("/"), user));
	dirpath		= wi_string_by_deleting_last_path_component(path);
	array		= wi_string_path_components(dirpath);
	count		= wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		wi_string_append_path_component(realpath, WI_ARRAY(array, i));
		
		if(wd_files_type(realpath) == WD_FILE_TYPE_DROPBOX)
			return realpath;
	}
	
	return NULL;
}



wi_string_t * wd_files_virtual_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*virtualpath;
	wd_account_t	*account;
	
	account = user ? wd_user_account(user) : NULL;
	
	if(account && account->files)
		virtualpath = wi_string_by_normalizing_path(wi_string_with_format(WI_STR("%@/%@"), account->files, path));
	else
		virtualpath = path;
	
	return virtualpath;
}



wi_string_t * wd_files_real_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*realpath;
	wd_account_t	*account;
	
	account = user ? wd_user_account(user) : NULL;
	
	if(account && account->files)
		realpath = wi_string_with_format(WI_STR("%@/%@/%@"), wd_files, account->files, path);
	else
		realpath = wi_string_with_format(WI_STR("%@/%@"), wd_files, path);
	
	return realpath;
}



static wi_boolean_t wd_files_name_matches_query(wi_string_t *name, wi_string_t *query) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	CFMutableStringRef		nameString;
	CFStringRef				queryString;
	CFRange					range;
	
	nameString = CFStringCreateMutable(NULL, 0);
	CFStringAppendCString(nameString, wi_string_cstring(name), kCFStringEncodingUTF8);
	CFStringNormalize(nameString, kCFStringNormalizationFormC);
	
	queryString = CFStringCreateWithCString(NULL, wi_string_cstring(query), kCFStringEncodingUTF8);

	range = CFStringFind(nameString, queryString, kCFCompareCaseInsensitive);

	CFRelease(nameString);
	CFRelease(queryString);
	
	return (range.location != kCFNotFound);
#else
	return (wi_string_index_of_string(name, query, WI_STRING_CASE_INSENSITIVE) != WI_NOT_FOUND);
#endif
}
