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

#define WD_FILES_META_TYPE_PATH							".wired/type"
#define WD_FILES_META_COMMENTS_PATH						".wired/comments"
#define WD_FILES_META_PERMISSIONS_PATH					".wired/permissions"
#define WD_FILES_META_LABELS_PATH						".wired/labels"

#define WD_FILES_PERMISSIONS_FIELD_SEPARATOR			"\34"

#define WD_FILES_OLDSTYLE_COMMENT_FIELD_SEPARATOR		"\34"
#define WD_FILES_OLDSTYLE_COMMENT_SEPARATOR				"\35"

#define WD_FILES_MAX_LEVEL								20

#define WD_FILES_INDEX_MAGIC							"WDIX"
#define WD_FILES_INDEX_VERSION							6


struct _wd_files_index_header {
	char												magic[4];
	uint32_t											version;
	uint32_t											files_count;
	uint32_t											directories_count;
	uint64_t											files_size;
};
typedef struct _wd_files_index_header					wd_files_index_header_t;

struct _wd_files_privileges {
	wi_runtime_base_t									base;
	
	wi_string_t											*owner;
	wi_string_t											*group;
	wi_uinteger_t										mode;
};


static wi_file_offset_t									wd_files_count_path(wi_string_t *, wd_user_t *, wi_p7_message_t *);
static void												wd_files_delete_path_callback(wi_string_t *);
static void												wd_files_move_thread(wi_runtime_instance_t *);

static wi_uinteger_t									wd_files_search_replace_privileges_for_account(char *, wi_uinteger_t, wi_string_t *, wd_account_t *);
static wi_uinteger_t									wd_files_search_replace_path_with_path(char *, wi_uinteger_t, wi_string_t *, wi_string_t *);

static void												wd_files_index_update(wi_timer_t *);
static wi_boolean_t										wd_files_index_update_size(void);
static void												wd_files_index_thread(wi_runtime_instance_t *);
static void												wd_files_index_path_to_file(wi_string_t *, wi_file_t *, wi_string_t *);
static void												wd_files_index_write_entry(wi_file_t *, wi_string_t *, wd_file_type_t, uint64_t, uint64_t, uint32_t, wi_time_interval_t, wi_time_interval_t, wi_boolean_t, wi_boolean_t, uint32_t, uint32_t);

static void												wd_files_fsevents_thread(wi_runtime_instance_t *);
static void												wd_files_fsevents_callback(wi_string_t *);

static wd_file_type_t									wd_files_type_with_stat(wi_string_t *, wi_fs_stat_t *);

static wi_string_t *									wd_files_comment(wi_string_t *);

static wd_file_label_t									wd_files_label(wi_string_t *);

static wd_files_privileges_t *							wd_files_drop_box_privileges(wi_string_t *);
static wi_string_t *									wd_files_drop_box_path_in_path(wi_string_t *, wd_user_t *);
static wi_boolean_t										wd_files_name_matches_query(wi_string_t *, wi_string_t *);

static wd_files_privileges_t *							wd_files_privileges_alloc(void);
static wi_string_t *									wd_files_privileges_description(wi_runtime_instance_t *instance);
static void												wd_files_privileges_dealloc(wi_runtime_instance_t *);

static wd_files_privileges_t *							wd_files_privileges_with_string(wi_string_t *);
static wd_files_privileges_t *							wd_files_privileges_default_privileges(void);
static wd_files_privileges_t *							wd_files_privileges_default_drop_box_privileges(void);

static wi_string_t *									wd_files_privileges_string(wd_files_privileges_t *);


static wi_string_t										*wd_files;

static wi_time_interval_t								wd_files_index_time;
static wi_string_t										*wd_files_index_path;
static wi_timer_t										*wd_files_index_timer;
static wi_rwlock_t										*wd_files_index_lock;
static wi_lock_t										*wd_files_indexer_lock;
static wi_uinteger_t									wd_files_index_level;
static wi_mutable_dictionary_t							*wd_files_index_dictionary;
static wi_mutable_dictionary_t							*wd_files_index_added_files;
static wi_mutable_set_t									*wd_files_index_deleted_files;

static uint32_t											wd_files_root_volume;

static wi_runtime_id_t									wd_files_privileges_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t								wd_files_privileges_runtime_class = {
	"wd_files_privileges_t",
	wd_files_privileges_dealloc,
	NULL,
	NULL,
	wd_files_privileges_description,
	NULL
};

wi_fsevents_t											*wd_files_fsevents;

wi_uinteger_t											wd_files_count;
wi_uinteger_t											wd_directories_count;
wi_file_offset_t										wd_files_size;



void wd_files_init(void) {
	wd_files_index_path				= WI_STR("index");
	wd_files_index_lock				= wi_rwlock_init(wi_rwlock_alloc());
	wd_files_indexer_lock			= wi_lock_init(wi_lock_alloc());
	wd_files_index_timer			= wi_timer_init_with_function(wi_timer_alloc(),
																  wd_files_index_update,
																  0.0,
																  true);
	wd_files_index_added_files		= wi_dictionary_init(wi_mutable_dictionary_alloc());
	wd_files_index_deleted_files	= wi_set_init(wi_mutable_set_alloc());
	
	wd_files_fsevents				= wi_fsevents_init(wi_fsevents_alloc());
	
	if(wd_files_fsevents)
		wi_fsevents_set_callback(wd_files_fsevents, wd_files_fsevents_callback);
	else
		wi_log_warn(WI_STR("Could not create fsevents: %m"));

	wd_files_privileges_runtime_id = wi_runtime_register_class(&wd_files_privileges_runtime_class);
}



void wd_files_apply_settings(wi_set_t *changes) {
	wi_string_t			*realpath;
	wi_fs_stat_t		sb;
	
	wi_release(wd_files);
	wd_files = wi_retain(wi_config_path_for_name(wd_config, WI_STR("files")));
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files);
	
	if(wi_fs_stat_path(realpath, &sb))
		wd_files_root_volume = sb.dev;
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
	wd_files_privileges_t		*privileges;
	wi_fs_statfs_t				sfb;
	wi_fs_stat_t				sb, lsb;
	wi_fsenumerator_status_t	status;
	wi_file_offset_t			datasize, rsrcsize, available;
	wd_file_label_t				label;
	wi_uinteger_t				pathlength, depthlimit, directorycount;
	wd_file_type_t				type, pathtype;
	wi_boolean_t				root, upload, alias, readable, writable;
	
	root		= wi_is_equal(path, WI_STR("/"));
	realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	account		= wd_user_account(user);
	pathtype	= wd_files_type(realpath);
	
	if(pathtype == WD_FILE_TYPE_DROPBOX) {
		privileges = wd_files_drop_box_privileges(realpath);
		
		if(!wd_files_privileges_is_readable_by_account(privileges, account))
			goto done;
	}
	
	depthlimit		= wd_account_file_recursive_list_depth_limit(account);
	fsenumerator	= wi_fs_enumerator_at_path(realpath);
	
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
		
		if(wi_fs_path_is_invisible(filepath)) {
			wi_fsenumerator_skip_descendents(fsenumerator);
			
			continue;
		}
		
		if(!recursive)
			wi_fsenumerator_skip_descendents(fsenumerator);
		
		virtualpath = wi_string_substring_from_index(filepath, pathlength);
		
		if(!root)
			virtualpath = wi_string_by_inserting_string_at_index(virtualpath, path, 0);
		
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

		readable	= false;
		writable	= false;
		type		= wd_files_type_with_stat(resolvedpath, &sb);
		
		if(type == WD_FILE_TYPE_DROPBOX) {
			privileges	= wd_files_drop_box_privileges(resolvedpath);
			readable	= wd_files_privileges_is_readable_by_account(privileges, account);
			writable	= wd_files_privileges_is_writable_by_account(privileges, account);
		}

		switch(type) {
			case WD_FILE_TYPE_DIR:
			case WD_FILE_TYPE_UPLOADS:
				datasize		= 0;
				rsrcsize		= 0;
				directorycount	= wd_files_count_path(resolvedpath, user, message);
				break;
				
			case WD_FILE_TYPE_DROPBOX:
				datasize		= 0;
				rsrcsize		= 0;
				directorycount	= readable ? wd_files_count_path(resolvedpath, user, message) : 0;
				break;

			case WD_FILE_TYPE_FILE:
			default:
				datasize		= sb.size;
				rsrcsize		= wi_fs_resource_fork_size_for_path(resolvedpath);
				directorycount	= 0;
				break;
		}
		
		label = wd_files_label(filepath);
		
		reply = wi_p7_message_init_with_name(wi_p7_message_alloc(), WI_STR("wired.file.file_list"), wd_p7_spec);
		wi_p7_message_set_string_for_name(reply, virtualpath, WI_STR("wired.file.path"));
		
		if(type == WD_FILE_TYPE_FILE) {
			wi_p7_message_set_uint64_for_name(reply, datasize, WI_STR("wired.file.data_size"));
			wi_p7_message_set_uint64_for_name(reply, rsrcsize, WI_STR("wired.file.rsrc_size"));
		} else {
			wi_p7_message_set_uint32_for_name(reply, directorycount, WI_STR("wired.file.directory_count"));
		}
		
		wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
		wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
		wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
		wi_p7_message_set_bool_for_name(reply, alias || S_ISLNK(lsb.mode), WI_STR("wired.file.link"));
		wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
		wi_p7_message_set_enum_for_name(reply, label, WI_STR("wired.file.label"));
		wi_p7_message_set_uint32_for_name(reply, sb.dev == wd_files_root_volume ? 0 : sb.dev, WI_STR("wired.file.volume"));
		
		if(type == WD_FILE_TYPE_DROPBOX) {
			wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.file.readable"));
			wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.file.writable"));
		}
		
		wd_user_reply_message(user, reply, message);
		wi_release(reply);
		
		if(recursive && (type == WD_FILE_TYPE_DROPBOX && !readable)) {
			wi_fsenumerator_skip_descendents(fsenumerator);
				
			continue;
		}
	}
	
done:
	if(wd_account_transfer_upload_anywhere(account))
		upload = true;
	else if(pathtype == WD_FILE_TYPE_DROPBOX || pathtype == WD_FILE_TYPE_UPLOADS)
		upload = wd_account_transfer_upload_files(account);
	else
		upload = false;

	if(upload && wi_fs_statfs_path(realpath, &sfb))
		available = (wi_file_offset_t) sfb.bavail * (wi_file_offset_t) sfb.frsize;
	else
		available = 0;
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.file_list.done"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	wi_p7_message_set_uint64_for_name(reply, available, WI_STR("wired.file.available"));
	wd_user_reply_message(user, reply, message);
}



static wi_file_offset_t wd_files_count_path(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_mutable_string_t		*filepath;
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
	
	filepath = wi_mutable_copy(path);
	
	wi_mutable_string_append_cstring(filepath, "/");
	
	while(readdir_r(dir, &de, &dep) == 0 && dep) {
		if(dep->d_name[0] != '.') {
			wi_mutable_string_append_cstring(filepath, dep->d_name);
			
			if(!wi_fs_path_is_invisible(filepath))
				count++;
			
			wi_mutable_string_delete_characters_from_index(filepath, wi_string_length(filepath) - strlen(dep->d_name));
		}
	}
	
	wi_release(filepath);

	closedir(dir);
	
	return count;
}




void wd_files_reply_info(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t			*reply;
	wi_string_t				*realpath, *comment;
	wd_account_t			*account;
	wd_files_privileges_t	*privileges = NULL;
	wi_file_offset_t		datasize, rsrcsize;
	wd_file_type_t			type;
	wi_fs_stat_t			sb, lsb;
	wd_file_label_t			label;
	wi_uinteger_t			directorycount;
	wi_boolean_t			alias, readable, writable;
	
	account			= wd_user_account(user);
	realpath		= wd_files_real_path(path, user);
	alias			= wi_fs_path_is_alias(realpath);
	realpath		= wi_string_by_resolving_aliases_in_path(realpath);

	if(!wi_fs_lstat_path(realpath, &lsb)) {
		wi_log_warn(WI_STR("Could not read info for %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return;
	}
	
	if(!wi_fs_stat_path(realpath, &sb))
		sb = lsb;

	readable	= false;
	writable	= false;
	type		= wd_files_type_with_stat(realpath, &sb);
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		privileges	= wd_files_drop_box_privileges(realpath);
		readable	= wd_files_privileges_is_readable_by_account(privileges, account);
		writable	= wd_files_privileges_is_writable_by_account(privileges, account);
	}
	
	comment = wd_files_comment(realpath);
	
	switch(type) {
		case WD_FILE_TYPE_DIR:
		case WD_FILE_TYPE_UPLOADS:
			datasize		= 0;
			rsrcsize		= 0;
			directorycount	= wd_files_count_path(realpath, user, message);
			break;
			
		case WD_FILE_TYPE_DROPBOX:
			datasize		= 0;
			rsrcsize		= 0;
			directorycount	= readable ? wd_files_count_path(realpath, user, message) : 0;
			break;

		case WD_FILE_TYPE_FILE:
		default:
			datasize		= sb.size;
			rsrcsize		= wi_fs_resource_fork_size_for_path(realpath);
			directorycount	= 0;
			break;
	}

	label = wd_files_label(realpath);
	
	reply = wi_p7_message_with_name(WI_STR("wired.file.info"), wd_p7_spec);
	wi_p7_message_set_string_for_name(reply, path, WI_STR("wired.file.path"));
	wi_p7_message_set_enum_for_name(reply, type, WI_STR("wired.file.type"));
	
	if(type == WD_FILE_TYPE_FILE) {
		wi_p7_message_set_uint64_for_name(reply, datasize, WI_STR("wired.file.data_size"));
		wi_p7_message_set_uint64_for_name(reply, rsrcsize, WI_STR("wired.file.rsrc_size"));
	} else {
		wi_p7_message_set_uint32_for_name(reply, directorycount, WI_STR("wired.file.directory_count"));
	}
	
	wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.birthtime), WI_STR("wired.file.creation_time"));
	wi_p7_message_set_date_for_name(reply, wi_date_with_time(sb.mtime), WI_STR("wired.file.modification_time"));
	wi_p7_message_set_string_for_name(reply, comment, WI_STR("wired.file.comment"));
	wi_p7_message_set_bool_for_name(reply, (alias || S_ISLNK(lsb.mode)), WI_STR("wired.file.link"));
	wi_p7_message_set_bool_for_name(reply, (type == WD_FILE_TYPE_FILE && sb.mode & 0111), WI_STR("wired.file.executable"));
	wi_p7_message_set_enum_for_name(reply, label, WI_STR("wired.file.label"));
	wi_p7_message_set_uint32_for_name(reply, sb.dev == wd_files_root_volume ? 0 : sb.dev, WI_STR("wired.file.volume"));
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		wi_p7_message_set_string_for_name(reply, privileges->owner, WI_STR("wired.file.owner"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_OWNER_WRITE), WI_STR("wired.file.owner.write"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_OWNER_READ), WI_STR("wired.file.owner.read"));
		wi_p7_message_set_string_for_name(reply, privileges->group, WI_STR("wired.file.group"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_GROUP_WRITE), WI_STR("wired.file.group.write"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_GROUP_READ), WI_STR("wired.file.group.read"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_EVERYONE_WRITE), WI_STR("wired.file.everyone.write"));
		wi_p7_message_set_bool_for_name(reply, (privileges->mode & WD_FILE_EVERYONE_READ), WI_STR("wired.file.everyone.read"));
		wi_p7_message_set_bool_for_name(reply, readable, WI_STR("wired.file.readable"));
		wi_p7_message_set_bool_for_name(reply, writable, WI_STR("wired.file.writable"));
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
	wi_mutable_string_t		*realpath;
	wi_string_t				*component;
	wi_boolean_t			result;
	
	realpath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(path, user)));
	component	= wi_string_last_path_component(realpath);

	wi_mutable_string_delete_last_path_component(realpath);
	wi_mutable_string_resolve_aliases_in_path(realpath);
	wi_mutable_string_append_path_component(realpath, component);
	
	result = wi_fs_delete_path_with_callback(realpath, wd_files_delete_path_callback);
	
	if(result) {
		wd_files_remove_comment(path, NULL, NULL);
		wd_files_remove_label(path, NULL, NULL);
	} else {
		wi_log_warn(WI_STR("Could not delete %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
	}
	
	return result;
}



static void wd_files_delete_path_callback(wi_string_t *path) {
	wd_files_index_delete_file(wi_string_substring_from_index(path, wi_string_length(wd_files)));
}



wi_boolean_t wd_files_move_path(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_array_t				*array;
	wi_mutable_string_t		*realfrompath, *realtopath;
	wi_string_t				*realfromname, *realtoname;
	wi_string_t				*path;
	wi_fs_stat_t			sb;
	wi_boolean_t			result = false;
	
	realfrompath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(frompath, user)));
	realtopath		= wi_autorelease(wi_mutable_copy(wd_files_real_path(topath, user)));
	realfromname	= wi_string_last_path_component(realfrompath);
	realtoname		= wi_string_last_path_component(realtopath);

	wi_mutable_string_resolve_aliases_in_path(realfrompath);
	wi_mutable_string_delete_last_path_component(realtopath);
	wi_mutable_string_resolve_aliases_in_path(realtopath);
	wi_mutable_string_append_path_component(realtopath, realtoname);
	
	if(!wi_fs_lstat_path(realfrompath, &sb)) {
		wi_log_warn(WI_STR("Could not rename %@: %m"), realfrompath);
		wd_user_reply_file_errno(user, message);

		return false;
	}

	if(wi_string_case_insensitive_compare(realfrompath, realtopath) == 0) {
		path = wi_fs_temporary_path_with_template(
			wi_string_with_format(WI_STR("%@/.%@.XXXXXXXX"),
				  wi_string_by_deleting_last_path_component(realfrompath),
				  wi_string_last_path_component(realfromname)));
		
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
		wd_files_move_label(frompath, topath, user, message);
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
				wd_user_reply_internal_error(user, message);
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
		wd_files_move_comment(frompath, topath, NULL, NULL);
		wd_files_move_label(frompath, topath, NULL, NULL);
		
		if(!wi_fs_delete_path(realfrompath))
			wi_log_warn(WI_STR("Could not delete %@: %m"), realfrompath);
	} else {
		wi_log_warn(WI_STR("Could not copy %@ to %@: %m"), realfrompath, realtopath);
	}
	
	wi_release(pool);
}



wi_boolean_t wd_files_link_path(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_mutable_string_t		*realfrompath, *realtopath;
	wi_string_t				*realfromname;
	wi_fs_stat_t			sb;
	
	realfrompath	= wi_autorelease(wi_mutable_copy(wd_files_real_path(frompath, user)));
	realtopath		= wi_autorelease(wi_mutable_copy(wd_files_real_path(topath, user)));
	realfromname	= wi_string_last_path_component(realfrompath);

	wi_mutable_string_delete_last_path_component(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realfrompath);
	wi_mutable_string_resolve_aliases_in_path(realtopath);
	wi_mutable_string_append_path_component(realfrompath, realfromname);
	
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
	wi_string_t			*name, *path, *accountpath, *newpath;
	wd_account_t		*account;
	char				*buffer = NULL, *messagebuffer;
	wi_uinteger_t		i = 0, bufferlength, messagelength, accountpathlength;
	uint32_t			entrylength, namelength;
	wi_boolean_t		deleted, sendreply;
	
	wi_rwlock_rdlock(wd_files_index_lock);
	
	file = wi_file_for_reading(wd_files_index_path);
	
	if(!file) {
		wi_log_warn(WI_STR("Could not open %@: %m"), wd_files_index_path);
		wd_user_reply_file_errno(user, message);

		goto end;
	}
	
	account				= wd_user_account(user);
	accountpath			= wd_account_files(account);
	accountpathlength	= accountpath ? wi_string_length(accountpath) : 0;
	bufferlength		= 0;
	
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
		name			= wi_string_init_with_cstring_no_copy(wi_string_alloc(), buffer + sizeof(namelength), false);
		messagelength	= entrylength - sizeof(namelength) - namelength;
		
		if(wd_files_name_matches_query(name, query)) {
			messagebuffer	= buffer + sizeof(namelength) + namelength;
			path			= wi_string_init_with_cstring_no_copy(wi_string_alloc(),
																  messagebuffer + sizeof(int32_t) + sizeof(int32_t) + sizeof(int32_t),
																  false);
			
			wi_set_rdlock(wd_files_index_deleted_files);
			deleted = wi_set_contains_data(wd_files_index_deleted_files, path);
			wi_set_unlock(wd_files_index_deleted_files);
			
			if(!deleted) {
				sendreply = true;
				messagelength = wd_files_search_replace_privileges_for_account(messagebuffer, messagelength, path, account);
				
				if(accountpath && accountpathlength > 0) {
					if(wi_string_has_prefix(path, accountpath)) {
						newpath = wi_string_substring_from_index(path, accountpathlength);
						
						if(wi_string_has_prefix(newpath, WI_STR("/")))
							messagelength = wd_files_search_replace_path_with_path(messagebuffer, messagelength, path, newpath);
						else
							sendreply = false;
					}
				}
	
				if(sendreply) {
					reply = wi_p7_message_with_bytes(messagebuffer, messagelength, WI_P7_BINARY, wd_p7_spec);

					if(reply)
						wd_user_reply_message(user, reply, message);
					else
						wi_log_err(WI_STR("Could not create message from search entry: %m"));
				}
			}
			
			wi_release(path);
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



static wi_uinteger_t wd_files_search_replace_privileges_for_account(char *messagebuffer, wi_uinteger_t messagelength, wi_string_t *path, wd_account_t *account) {
	wi_string_t				*realpath;
	wd_files_privileges_t	*privileges;
	wi_uinteger_t			pathlength, typeoffset;
	uint32_t				type;
	
	pathlength	= wi_string_length(path);
	typeoffset	= sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) + pathlength + 1 + sizeof(uint32_t);
	type		= wi_read_swap_big_to_host_int32(messagebuffer, typeoffset);
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		realpath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, NULL));
		privileges	= wd_files_drop_box_privileges(realpath);
		
		if(wd_files_privileges_is_readable_by_account(privileges, account))
			memcpy(messagebuffer + typeoffset + sizeof(uint32_t) + sizeof(uint32_t), "\1", 1);

		if(wd_files_privileges_is_writable_by_account(privileges, account))
			memcpy(messagebuffer + typeoffset + sizeof(uint32_t) + sizeof(uint32_t) + 1 + sizeof(uint32_t), "\1", 1);
	}
	
	return messagelength;
}



static wi_uinteger_t wd_files_search_replace_path_with_path(char *messagebuffer, wi_uinteger_t messagelength, wi_string_t *oldpath, wi_string_t *newpath) {
	wi_uinteger_t		oldpathlength, newpathlength, pathlengthoffset, pathoffset;
	
	oldpathlength		= wi_string_length(oldpath);
	newpathlength		= wi_string_length(newpath);
	pathlengthoffset	= sizeof(uint32_t) + sizeof(uint32_t);
	pathoffset			= pathlengthoffset + sizeof(uint32_t);
	
	wi_write_swap_host_to_big_int32(messagebuffer, pathlengthoffset, newpathlength + 1);
	memmove(messagebuffer + pathoffset + newpathlength + 1,
			messagebuffer + pathoffset + oldpathlength + 1,
			messagelength - pathoffset - oldpathlength);
	memcpy(messagebuffer + pathoffset, wi_string_cstring(newpath), newpathlength + 1);
	
	return messagelength - (oldpathlength - newpathlength);
}



#pragma mark -

static void wd_files_index_update(wi_timer_t *timer) {
	wd_files_index(false);
}



void wd_files_index(wi_boolean_t startup) {
	wi_fs_stat_t		sb;
	wi_time_interval_t	interval, index_time;
	wi_boolean_t		index = true;
	
	if(startup) {
		if(wi_fs_stat_path(wd_files_index_path, &sb)) {
			interval = wi_date_time_interval_since_now(wi_date_with_time(sb.mtime));
			index_time  = (wd_files_index_time > 0.0) ? wd_files_index_time : 3600.0;
			
			if(interval < index_time && wd_files_index_update_size()) {
				wi_log_info(WI_STR("Found %u %s and %u %s for a total of %@ (%llu bytes) in %@ created %.2f seconds ago"),
					wd_files_count,
					wd_files_count == 1
						? "file"
						: "files",
					wd_directories_count,
					wd_directories_count == 1
						? "directory"
						: "directories",
					wd_files_string_for_bytes(wd_files_size),
					wd_files_size,
					wd_files_index_path,
					interval);
				
				wd_trackers_register();
				
				index = false;
			}
		}
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
	
	if(memcmp(header.magic, WD_FILES_INDEX_MAGIC, sizeof(header.magic)) == 0) {
		if(header.version == WD_FILES_INDEX_VERSION) {
			wd_files_count			= header.files_count;
			wd_directories_count	= header.directories_count;
			wd_files_size			= header.files_size;
			
			return true;
		} else {
			wi_log_warn(WI_STR("Could not read %@: Wrong version (%u != %u)"),
				wd_files_index_path, header.version, WD_FILES_INDEX_VERSION);
		}
	} else {
		wi_log_warn(WI_STR("Could not read %@: Wrong magic"),
			wd_files_index_path);
	}

	return false;
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
		
		wd_files_index_dictionary = wi_dictionary_init_with_capacity_and_callbacks(wi_mutable_dictionary_alloc(), 0,
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
				wi_log_info(WI_STR("Indexed %u %s and %u %s for a total of %@ (%llu bytes) in %.2f seconds"),
					wd_files_count,
					wd_files_count == 1
						? "file"
						: "files",
					wd_directories_count,
					wd_directories_count == 1
						? "directory"
						: "directories",
					wd_files_string_for_bytes(wd_files_size),
					wd_files_size,
					wi_time_interval() - interval);
			} else {
				wi_log_warn(WI_STR("Could not rename %@ to %@: %m"),
					path, wd_files_index_path);
			}
			
			wi_set_wrlock(wd_files_index_deleted_files);
			wi_mutable_set_remove_all_data(wd_files_index_deleted_files);
			wi_set_unlock(wd_files_index_deleted_files);
			
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
	wi_mutable_set_t			*set;
	wi_number_t					*number;
	wi_file_offset_t			datasize, rsrcsize;
	wi_fs_stat_t				sb, lsb;
	wi_fsenumerator_status_t	status;
	wd_file_label_t				label;
	wi_uinteger_t				i = 0, pathlength, directorycount;
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
			wi_log_warn(WI_STR("Skipping index of %@: %m"), filepath);
			
			continue;
		}
		
		if(wi_fs_path_is_invisible(filepath)) {
			wi_fsenumerator_skip_descendents(fsenumerator);
			
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
				set = wi_set_init_with_capacity(wi_mutable_set_alloc(), 1000, false);
				wi_mutable_dictionary_set_data_for_key(wd_files_index_dictionary, set, (void *) (intptr_t) lsb.dev);
				wi_release(set);
			}
			
			number = wi_number_init_with_value(wi_number_alloc(), WI_NUMBER_INT64, &lsb.ino);
			
			if(!wi_set_contains_data(set, number)) {
				wi_mutable_set_add_data(set, number);
				
				recurse = (alias && S_ISDIR(sb.mode));
				
				type = wd_files_type_with_stat(resolvedpath, &sb);
				
				switch(type) {
					case WD_FILE_TYPE_DROPBOX:
						datasize		= 0;
						rsrcsize		= 0;
						directorycount	= 0;
						break;
						
					case WD_FILE_TYPE_DIR:
					case WD_FILE_TYPE_UPLOADS:
						datasize		= 0;
						rsrcsize		= 0;
						directorycount	= wd_files_count_path(resolvedpath, NULL, NULL);
						break;
						
					case WD_FILE_TYPE_FILE:
					default:
						datasize		= sb.size;
						rsrcsize		= wi_fs_resource_fork_size_for_path(resolvedpath);
						directorycount	= 0;
						break;
				}
				
				label = wd_files_label(filepath);
				
				virtualpath	= wi_string_substring_from_index(filepath, pathlength);
				
				if(pathprefix)
					virtualpath = wi_string_by_inserting_string_at_index(virtualpath, pathprefix, 0);
				
				wd_files_index_write_entry(file,
										   virtualpath,
										   type,
										   datasize,
										   rsrcsize,
										   directorycount,
										   sb.birthtime,
										   sb.mtime,
										   (alias || S_ISLNK(lsb.mode)),
										   (type == WD_FILE_TYPE_FILE && sb.mode & 0111),
										   label,
										   sb.dev == wd_files_root_volume ? 0 : sb.dev);

				if(S_ISDIR(sb.mode)) {
					wd_directories_count++;
				} else {
					wd_files_count++;
					wd_files_size += datasize + rsrcsize;
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



static void wd_files_index_write_entry(wi_file_t *file, wi_string_t *path, wd_file_type_t type, uint64_t datasize, uint64_t rsrcsize, uint32_t directorycount, wi_time_interval_t creationtime, wi_time_interval_t modificationtime, wi_boolean_t link, wi_boolean_t executable, uint32_t label, uint32_t volume) {
	static char				*buffer;
	static wi_uinteger_t	bufferlength;
	static uint32_t			searchlistid, pathid, typeid, datasizeid, rsrcsizeid, directorycountid, creationid, modificationid;
	static uint32_t			linkid, executableid, labelid, volumeid, readableid, writableid;
	wi_string_t				*name, *creationstring, *modificationstring;
	uint32_t				totalentrylength, entrylength, namelength, pathlength, creationlength, modificationlength;
	char					*p;
	
	if(searchlistid == 0) {
		searchlistid		= wi_p7_spec_message_id(wi_p7_spec_message_with_name(wd_p7_spec, WI_STR("wired.file.search_list")));
		pathid				= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.path")));
		typeid				= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.type")));
		datasizeid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.data_size")));
		rsrcsizeid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.rsrc_size")));
		directorycountid	= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.directory_count")));
		creationid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.creation_time")));
		modificationid		= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.modification_time")));
		linkid				= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.link")));
		executableid		= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.executable")));
		labelid				= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.label")));
		volumeid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.volume")));
		readableid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.readable")));
		writableid			= wi_p7_spec_field_id(wi_p7_spec_field_with_name(wd_p7_spec, WI_STR("wired.file.writable")));
	}

	name					= wi_string_last_path_component(path);
	creationstring			= wi_time_interval_rfc3339_string(creationtime);
	modificationstring		= wi_time_interval_rfc3339_string(modificationtime);

	namelength				= wi_string_length(name) + 1;
	pathlength				= wi_string_length(path) + 1;
	creationlength			= wi_string_length(creationstring) + 1;
	modificationlength		= wi_string_length(modificationstring) + 1;
	entrylength				= sizeof(namelength) + namelength +
							  sizeof(searchlistid) + 
							  sizeof(pathid) + sizeof(pathlength) + pathlength +
							  sizeof(typeid) + sizeof(type) + 
							  sizeof(creationid) + creationlength +
							  sizeof(modificationid) + modificationlength +
							  sizeof(linkid) + 1 +
							  sizeof(executableid) + 1 +
							  sizeof(labelid) + sizeof(label) +
							  sizeof(volumeid) + sizeof(volume);
	
	if(type == WD_FILE_TYPE_FILE) {
		entrylength += sizeof(datasizeid) + sizeof(datasizeid);
		entrylength += sizeof(rsrcsizeid) + sizeof(rsrcsizeid);
	} else {
		entrylength += sizeof(directorycountid) + sizeof(directorycount);
	}
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		entrylength += sizeof(readableid) + 1;
		entrylength += sizeof(writableid) + 1;
	}
	
	totalentrylength	= sizeof(entrylength) + entrylength;
	
	if(!buffer) {
		bufferlength = totalentrylength * 2;
		buffer = wi_malloc(bufferlength);
	}
	else if(bufferlength < totalentrylength) {
		bufferlength = totalentrylength * 2;
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
	
	if(type == WD_FILE_TYPE_DROPBOX) {
		wi_write_swap_host_to_big_int32(p, 0, readableid);					p += sizeof(readableid);
		memcpy(p, "\0", 1);													p += 1;
		wi_write_swap_host_to_big_int32(p, 0, writableid);					p += sizeof(writableid);
		memcpy(p, "\0", 1);													p += 1;
	}
	
	if(type == WD_FILE_TYPE_FILE) {
		wi_write_swap_host_to_big_int32(p, 0, datasizeid);					p += sizeof(datasizeid);
		wi_write_swap_host_to_big_int64(p, 0, datasize);					p += sizeof(datasize);
		wi_write_swap_host_to_big_int32(p, 0, rsrcsizeid);					p += sizeof(rsrcsizeid);
		wi_write_swap_host_to_big_int64(p, 0, rsrcsize);					p += sizeof(rsrcsize);
	} else {
		wi_write_swap_host_to_big_int32(p, 0, directorycountid);			p += sizeof(directorycountid);
		wi_write_swap_host_to_big_int32(p, 0, directorycount);				p += sizeof(directorycount);
	}
	
	wi_write_swap_host_to_big_int32(p, 0, creationid);						p += sizeof(creationid);
	memcpy(p, wi_string_cstring(creationstring), creationlength);			p += creationlength;
	wi_write_swap_host_to_big_int32(p, 0, modificationid);					p += sizeof(modificationid);
	memcpy(p, wi_string_cstring(modificationstring), modificationlength);	p += modificationlength;
	wi_write_swap_host_to_big_int32(p, 0, linkid);							p += sizeof(linkid);
	memcpy(p, link ? "\1" : "\0", 1);										p += 1;
	wi_write_swap_host_to_big_int32(p, 0, executableid);					p += sizeof(executableid);
	memcpy(p, executable ? "\1" : "\0", 1);									p += 1;
	wi_write_swap_host_to_big_int32(p, 0, labelid);							p += sizeof(labelid);
	wi_write_swap_host_to_big_int32(p, 0, label);							p += sizeof(label);
	wi_write_swap_host_to_big_int32(p, 0, volumeid);						p += sizeof(volumeid);
	wi_write_swap_host_to_big_int32(p, 0, volume);
	
	wi_file_write_buffer(file, buffer, totalentrylength);
}



void wd_files_index_add_file(wi_string_t *path) {
}



void wd_files_index_delete_file(wi_string_t *path) {
	wi_set_wrlock(wd_files_index_deleted_files);
	wi_mutable_set_add_data(wd_files_index_deleted_files, path);
	wi_set_unlock(wd_files_index_deleted_files);
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
	
	if(wi_is_equal(wi_string_last_path_component(virtualpath), WI_STR(WD_FILES_META_PATH)))
		virtualpath = wi_string_by_deleting_last_path_component(virtualpath);
	
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

wi_boolean_t wd_files_set_type(wi_string_t *path, wd_file_type_t type, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *metapath, *typepath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	metapath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	typepath = wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_TYPE_PATH));
	
	if(type != WD_FILE_TYPE_DIR) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				wd_user_reply_file_errno(user, message);
				
				return false;
			}
		}
		
		if(!wi_string_write_to_file(wi_string_with_format(WI_STR("%u\n"), type), typepath)) {
			wi_log_warn(WI_STR("Could not write to %@: %m"), typepath);
			wd_user_reply_file_errno(user, message);
			
			return false;
		}
	} else {
		if(!wi_fs_delete_path(typepath)) {
			wi_log_warn(WI_STR("Could not delete %@: %m"), typepath);
			wd_user_reply_file_errno(user, message);
			
			return false;
		}
		
		(void) rmdir(wi_string_cstring(metapath));
	}
	
	return true;
}



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
	
	type = wi_string_uint32(wi_string_by_deleting_surrounding_whitespace(string));
	
	if(type == WD_FILE_TYPE_FILE)
		type = WD_FILE_TYPE_DIR;
	
	return type;
}



#pragma mark -

wi_boolean_t wd_files_set_executable(wi_string_t *path, wi_boolean_t executable, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath;
	
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	
	if(!wi_fs_set_mode_for_path(realpath, executable ? 0755 : 0644)) {
		wi_log_warn(WI_STR("Could not set mode for %@: %m"), realpath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	return true;
}



#pragma mark -

static wi_string_t * wd_files_comment(wi_string_t *path) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	return wi_fs_finder_comment_for_path(path);
#else
	wi_runtime_instance_t	*instance;
	wi_file_t				*file;
	wi_array_t				*array;
	wi_string_t				*name, *dirpath, *commentspath, *string;

	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	commentspath	= wi_string_by_appending_path_component(dirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	instance		= wi_plist_read_instance_from_file(commentspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id()) {
		file = wi_file_for_reading(commentspath);
		
		if(!file)
			return NULL;
		
		while((string = wi_file_read_to_string(file, WI_STR(WD_FILES_OLDSTYLE_COMMENT_SEPARATOR)))) {
			array = wi_string_components_separated_by_string(string, WI_STR(WD_FILES_OLDSTYLE_COMMENT_FIELD_SEPARATOR));
			  
			if(wi_array_count(array) == 2 && wi_is_equal(WI_ARRAY(array, 0), name))
				return WI_ARRAY(array, 1);
		}
	}

	return wi_dictionary_data_for_key(instance, name);
#endif
}



wi_boolean_t wd_files_set_comment(wi_string_t *path, wi_string_t *comment, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t				*realpath;
#endif
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *commentspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	commentspath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	
	if(comment && wi_string_length(comment) > 0) {
		if(!wi_fs_create_directory(metapath, 0777)) {
			if(wi_error_code() != EEXIST) {
				wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
				
				if(user)
					wd_user_reply_file_errno(user, message);
				
				return false;
			}
		}
	}
	
	instance = wi_plist_read_instance_from_file(commentspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id())
		instance = wi_mutable_dictionary();
	
	wi_mutable_dictionary_set_data_for_key(instance, comment, name);
	
	if(!wi_plist_write_instance_to_file(instance, commentspath)) {
		wi_log_warn(WI_STR("Could not write to %@: %m"), commentspath);
		
		if(user)
			wd_user_reply_file_errno(user, message);
		
		return false;
	}

#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_comment_for_path(comment, realpath)) {
			wi_log_err(WI_STR("Could not set Finder comment: %m"));
			wd_user_reply_internal_error(user, message);
			
			return false;
		}
	}
#endif
	
	return true;
}



void wd_files_move_comment(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realfrompath, *comment;
	
	realfrompath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(frompath, user));
	comment			= wd_files_comment(realfrompath);
	
	if(comment) {
		wd_files_remove_comment(frompath, user, message);
		wd_files_set_comment(topath, comment, user, message);
	}
}



wi_boolean_t wd_files_remove_comment(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *commentspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	commentspath	= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_COMMENTS_PATH));
	
	instance = wi_plist_read_instance_from_file(commentspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id())
		instance = wi_mutable_dictionary();
	
	wi_mutable_dictionary_remove_data_for_key(instance, name);
	
	if(wi_dictionary_count(instance) > 0) {
		if(!wi_plist_write_instance_to_file(instance, commentspath)) {
			wi_log_warn(WI_STR("Could not write to %@: %m"), commentspath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}
	} else {
		if(!wi_fs_delete_path(commentspath)) {
			wi_log_warn(WI_STR("Could not delete %@: %m"), commentspath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}

		(void) rmdir(wi_string_cstring(metapath));
	}
	
	return true;
}



#pragma mark -

static wd_file_label_t wd_files_label(wi_string_t *path) {
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	return wi_fs_finder_label_for_path(path);
#else
	wi_runtime_instance_t	*instance;
	wi_number_t				*label;
	wi_string_t				*name, *dirpath, *labelspath;

	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	labelspath		= wi_string_by_appending_path_component(dirpath, WI_STR(WD_FILES_META_LABELS_PATH));
	instance		= wi_plist_read_instance_from_file(labelspath);
	label			= instance ? wi_dictionary_data_for_key(instance, name) : NULL;
	
	return label ? wi_number_int32(label) : WD_FILE_LABEL_NONE;
#endif
}



wi_boolean_t wd_files_set_label(wi_string_t *path, wd_file_label_t label, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	wi_string_t				*realpath;
#endif
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *labelspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	labelspath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_LABELS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}
	}
	
	instance = wi_plist_read_instance_from_file(labelspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id())
		instance = wi_mutable_dictionary();
	
	wi_mutable_dictionary_set_data_for_key(instance, WI_INT32(label), name);
	
	if(!wi_plist_write_instance_to_file(instance, labelspath)) {
		wi_log_warn(WI_STR("Could not write to %@: %m"), labelspath);
		
		if(user)
			wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
#ifdef HAVE_CORESERVICES_CORESERVICES_H
	realpath = wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));

	if(wi_fs_path_exists(realpath, NULL)) {
		if(!wi_fs_set_finder_label_for_path(label, realpath)) {
			wi_log_err(WI_STR("Could not set Finder label: %m"));
			wd_user_reply_internal_error(user, message);
			
			return false;
		}
	}
#endif
	
	return true;
}



wi_boolean_t wd_files_remove_label(wi_string_t *path, wd_user_t *user, wi_p7_message_t *message) {
	wi_runtime_instance_t	*instance;
	wi_string_t				*name, *dirpath, *realdirpath, *metapath, *labelspath;
	
	name			= wi_string_last_path_component(path);
	dirpath			= wi_string_by_deleting_last_path_component(path);
	realdirpath		= wi_string_by_resolving_aliases_in_path(wd_files_real_path(dirpath, user));
	metapath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_PATH));
	labelspath		= wi_string_by_appending_path_component(realdirpath, WI_STR(WD_FILES_META_LABELS_PATH));
	
	instance = wi_plist_read_instance_from_file(labelspath);
	
	if(!instance || wi_runtime_id(instance) != wi_dictionary_runtime_id())
		instance = wi_mutable_dictionary();
	
	wi_mutable_dictionary_remove_data_for_key(instance, name);
	
	if(wi_dictionary_count(instance) > 0) {
		if(!wi_plist_write_instance_to_file(instance, labelspath)) {
			wi_log_warn(WI_STR("Could not write to %@: %m"), labelspath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}
	} else {
		if(!wi_fs_delete_path(labelspath)) {
			wi_log_warn(WI_STR("Could not delete %@: %m"), labelspath);
			
			if(user)
				wd_user_reply_file_errno(user, message);
			
			return false;
		}
		
		(void) rmdir(wi_string_cstring(metapath));
	}
	
	return true;
}



void wd_files_move_label(wi_string_t *frompath, wi_string_t *topath, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t			*realfrompath;
	wd_file_label_t		label;
	
	realfrompath	= wi_string_by_resolving_aliases_in_path(wd_files_real_path(frompath, user));
	label			= wd_files_label(realfrompath);
	
	if(label != WD_FILE_LABEL_NONE) {
		wd_files_remove_label(frompath, user, message);
		wd_files_set_label(topath, label, user, message);
	}
}



#pragma mark -

wi_boolean_t wd_files_set_privileges(wi_string_t *path, wd_files_privileges_t *privileges, wd_user_t *user, wi_p7_message_t *message) {
	wi_string_t		*realpath, *metapath, *permissionspath;
	wi_string_t		*string;
	
	realpath			= wi_string_by_resolving_aliases_in_path(wd_files_real_path(path, user));
	metapath			= wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PATH));
	permissionspath		= wi_string_by_appending_path_component(realpath, WI_STR(WD_FILES_META_PERMISSIONS_PATH));
	
	if(!wi_fs_create_directory(metapath, 0777)) {
		if(wi_error_code() != EEXIST) {
			wi_log_warn(WI_STR("Could not create %@: %m"), metapath);
			wd_user_reply_file_errno(user, message);

			return false;
		}
	}
	
	string = wd_files_privileges_string(privileges);
	
	if(!wi_string_write_to_file(string, permissionspath)) {
		wi_log_warn(WI_STR("Could not write to %@: %m"), permissionspath);
		wd_user_reply_file_errno(user, message);
		
		return false;
	}
	
	return true;
}



wd_files_privileges_t * wd_files_privileges(wi_string_t *path, wd_user_t *user) {
	wi_string_t				*realpath;
	
	realpath = wd_files_drop_box_path_in_path(path, user);
	
	if(!realpath)
		return wd_files_privileges_default_privileges();
	
	return wd_files_drop_box_privileges(realpath);
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



wi_string_t * wd_files_virtual_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*accountpath, *virtualpath;
	wd_account_t	*account;
	
	account			= user ? wd_user_account(user) : NULL;
	accountpath		= account ? wd_account_files(account) : NULL;
	
	if(accountpath && wi_string_length(accountpath) > 0)
		virtualpath = wi_string_by_normalizing_path(wi_string_with_format(WI_STR("%@/%@"), wd_account_files(account), path));
	else
		virtualpath = path;
	
	return virtualpath;
}



wi_string_t * wd_files_real_path(wi_string_t *path, wd_user_t *user) {
	wi_string_t		*accountpath, *realpath;
	wd_account_t	*account;
	
	account			= user ? wd_user_account(user) : NULL;
	accountpath		= account ? wd_account_files(account) : NULL;
	
	if(accountpath && wi_string_length(accountpath) > 0)
		realpath = wi_string_with_format(WI_STR("%@/%@/%@"), wd_files, wd_account_files(account), path);
	else
		realpath = wi_string_with_format(WI_STR("%@/%@"), wd_files, path);
	
	return realpath;
}



wi_boolean_t wd_files_has_uploads_or_drop_box_in_path(wi_string_t *path, wd_user_t *user, wd_files_privileges_t **privileges) {
	wi_mutable_string_t		*realpath;
	wi_string_t				*dirpath;
	wi_array_t				*array;
	wi_uinteger_t			i, count;
	
	realpath	= wi_autorelease(wi_mutable_copy(wi_string_by_resolving_aliases_in_path(wd_files_real_path(WI_STR("/"), user))));
	dirpath		= wi_string_by_deleting_last_path_component(path);
	array		= wi_string_path_components(dirpath);
	count		= wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		wi_mutable_string_append_path_component(realpath, WI_ARRAY(array, i));
		
		switch(wd_files_type(realpath)) {
			case WD_FILE_TYPE_UPLOADS:
				*privileges = NULL;
				
				return true;
				break;
				
			case WD_FILE_TYPE_DROPBOX:
				*privileges = wd_files_drop_box_privileges(path);
				
				return true;
				break;
				
			default:
				break;
		}
	}
	
	*privileges = NULL;
	
	return false;
}



#pragma mark -

static wd_files_privileges_t * wd_files_drop_box_privileges(wi_string_t *path) {
	wi_string_t				*permissionspath, *string;
	wd_files_privileges_t	*privileges;
	wi_fs_stat_t			sb;
	
	permissionspath = wi_string_by_appending_path_component(path, WI_STR(WD_FILES_META_PERMISSIONS_PATH));
	
	if(!wi_fs_stat_path(permissionspath, &sb))
		return wd_files_privileges_default_drop_box_privileges();
	
	if(sb.size > 128) {
		wi_log_warn(WI_STR("Could not read %@: Size is too large (%u"), permissionspath, sb.size);
		
		return wd_files_privileges_default_drop_box_privileges();
	}
	
	string = wi_autorelease(wi_string_init_with_contents_of_file(wi_string_alloc(), permissionspath));
	
	if(!string) {
		wi_log_warn(WI_STR("Could not read %@: %m"), permissionspath);
		
		return wd_files_privileges_default_drop_box_privileges();
	}
	
	privileges = wd_files_privileges_with_string(string);
	
	if(!privileges) {
		wi_log_info(WI_STR("Could not read %@: Contents is malformed (%@)"), permissionspath, string);
		
		return wd_files_privileges_default_drop_box_privileges();
	}
	
	return privileges;
}



static wi_string_t * wd_files_drop_box_path_in_path(wi_string_t *path, wd_user_t *user) {
	wi_mutable_string_t		*realpath;
	wi_string_t				*dirpath;
	wi_array_t				*array;
	wi_uinteger_t			i, count;
	
	realpath	= wi_autorelease(wi_mutable_copy(wi_string_by_resolving_aliases_in_path(wd_files_real_path(WI_STR("/"), user))));
	dirpath		= wi_string_by_deleting_last_path_component(path);
	array		= wi_string_path_components(dirpath);
	count		= wi_array_count(array);
	
	for(i = 0; i < count; i++) {
		wi_mutable_string_append_path_component(realpath, WI_ARRAY(array, i));
		
		if(wd_files_type(realpath) == WD_FILE_TYPE_DROPBOX)
			return realpath;
	}
	
	return NULL;
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



#pragma mark -

wi_string_t * wd_files_string_for_bytes(wi_file_offset_t bytes) {
	double						kb, mb, gb, tb, pb;

	if(bytes < 1024)
		return wi_string_with_format(WI_STR("%llu bytes"), bytes);

	kb = (double) bytes / 1024.0;

	if(kb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f KB"), kb);

	mb = (double) kb / 1024.0;

	if(mb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f MB"), mb);

	gb = (double) mb / 1024.0;

	if(gb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f GB"), gb);

	tb = (double) gb / 1024.0;

	if(tb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f TB"), tb);

	pb = (double) tb / 1024.0;

	if(pb < 1024.0)
		return wi_string_with_format(WI_STR("%.1f PB"), pb);

	return NULL;
}




#pragma mark -

static wd_files_privileges_t * wd_files_privileges_alloc(void) {
	return wi_runtime_create_instance(wd_files_privileges_runtime_id, sizeof(wd_files_privileges_t));
}



static wi_string_t * wd_files_privileges_description(wi_runtime_instance_t *instance) {
	wd_files_privileges_t		*privileges = instance;
	
	return wi_string_with_format(WI_STR("<%@ %p>{owner = \"%@\" %c%c, group = \"%@\" %c%c, everyone = %c%c}"),
		wi_runtime_class_name(privileges),
		privileges,
		privileges->owner,
		(privileges->mode & WD_FILE_OWNER_READ) ? 'r' : '-',
		(privileges->mode & WD_FILE_OWNER_WRITE) ? 'w' : '-',
		privileges->group,
		(privileges->mode & WD_FILE_GROUP_READ) ? 'r' : '-',
		(privileges->mode & WD_FILE_GROUP_WRITE) ? 'w' : '-',
		(privileges->mode & WD_FILE_EVERYONE_READ) ? 'r' : '-',
		(privileges->mode & WD_FILE_EVERYONE_WRITE) ? 'w' : '-');
}



static void wd_files_privileges_dealloc(wi_runtime_instance_t *instance) {
	wd_files_privileges_t		*privileges = instance;
	
	wi_release(privileges->owner);
	wi_release(privileges->group);
}



#pragma mark -

wd_files_privileges_t * wd_files_privileges_with_message(wi_p7_message_t *message) {
	wd_files_privileges_t		*privileges;
	wi_p7_boolean_t				value;
	
	privileges = wd_files_privileges_alloc();
	privileges->owner = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.file.owner")));

	if(!privileges->owner)
		privileges->owner = wi_retain(WI_STR(""));
	
	privileges->group = wi_retain(wi_p7_message_string_for_name(message, WI_STR("wired.file.group")));
	
	if(!privileges->group)
		privileges->group = wi_retain(WI_STR(""));
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.owner.read")) && value)
		privileges->mode |= WD_FILE_OWNER_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.owner.write")) && value)
		privileges->mode |= WD_FILE_OWNER_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.group.read")) && value)
		privileges->mode |= WD_FILE_GROUP_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.group.write")) && value)
		privileges->mode |= WD_FILE_GROUP_WRITE;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.everyone.read")) && value)
		privileges->mode |= WD_FILE_EVERYONE_READ;
	
	if(wi_p7_message_get_bool_for_name(message, &value, WI_STR("wired.file.everyone.write")) && value)
		privileges->mode |= WD_FILE_EVERYONE_WRITE;

	return wi_autorelease(privileges);
}



static wd_files_privileges_t * wd_files_privileges_with_string(wi_string_t *string) {
	wi_array_t				*array;
	wd_files_privileges_t	*privileges;
	
	string				= wi_string_by_deleting_surrounding_whitespace(string);
	array				= wi_string_components_separated_by_string(string, WI_STR(WD_FILES_PERMISSIONS_FIELD_SEPARATOR));
	
	if(wi_array_count(array) != 3)
		return NULL;
	
	privileges			= wd_files_privileges_alloc();
	privileges->owner	= wi_retain(WI_ARRAY(array, 0));
	privileges->group	= wi_retain(WI_ARRAY(array, 1));
	privileges->mode	= wi_string_uint32(WI_ARRAY(array, 2));
	
	return wi_autorelease(privileges);
}



static wd_files_privileges_t * wd_files_privileges_default_privileges(void) {
	wd_files_privileges_t	*privileges;
	
	privileges			= wd_files_privileges_alloc();
	privileges->owner	= wi_retain(WI_STR(""));
	privileges->group	= wi_retain(WI_STR(""));
	privileges->mode	= WD_FILE_EVERYONE_WRITE | WD_FILE_EVERYONE_READ;
	
	return wi_autorelease(privileges);
}



static wd_files_privileges_t * wd_files_privileges_default_drop_box_privileges(void) {
	wd_files_privileges_t	*privileges;
	
	privileges			= wd_files_privileges_alloc();
	privileges->owner	= wi_retain(WI_STR(""));
	privileges->group	= wi_retain(WI_STR(""));
	privileges->mode	= WD_FILE_EVERYONE_WRITE;
	
	return wi_autorelease(privileges);
}



#pragma mark -

static wi_string_t * wd_files_privileges_string(wd_files_privileges_t *privileges) {
	return wi_string_with_format(WI_STR("%#@%s%#@%s%u"),
	   privileges->owner,		WD_FILES_PERMISSIONS_FIELD_SEPARATOR,
	   privileges->group,		WD_FILES_PERMISSIONS_FIELD_SEPARATOR,
	   privileges->mode);
}



wi_boolean_t wd_files_privileges_is_readable_by_account(wd_files_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_FILE_EVERYONE_READ)
		return true;
	
	if(wd_account_file_access_all_dropboxes(account))
		return true;
	
	if(privileges->mode & WD_FILE_GROUP_READ && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_FILE_OWNER_READ && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



wi_boolean_t wd_files_privileges_is_writable_by_account(wd_files_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_FILE_EVERYONE_WRITE)
		return true;
	
	if(wd_account_file_access_all_dropboxes(account))
		return true;
	
	if(privileges->mode & WD_FILE_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_FILE_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}



wi_boolean_t wd_files_privileges_is_readable_and_writable_by_account(wd_files_privileges_t *privileges, wd_account_t *account) {
	if(privileges->mode & WD_FILE_EVERYONE_READ && privileges->mode & WD_FILE_EVERYONE_WRITE)
		return true;
	
	if(wd_account_file_access_all_dropboxes(account))
		return true;
	
	if(privileges->mode & WD_FILE_GROUP_READ && privileges->mode & WD_FILE_GROUP_WRITE && wi_string_length(privileges->group) > 0) {
		if(wi_is_equal(privileges->group, wd_account_group(account)) || wi_array_contains_data(wd_account_groups(account), privileges->group))
			return true;
	}
	
	if(privileges->mode & WD_FILE_OWNER_READ && privileges->mode & WD_FILE_OWNER_WRITE && wi_string_length(privileges->owner) > 0) {
		if(wi_is_equal(privileges->owner, wd_account_name(account)))
			return true;
	}
	
	return false;
}
