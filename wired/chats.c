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

#include <stdlib.h>
#include <wired/wired.h>

#include "chats.h"
#include "server.h"

#define WD_PUBLIC_CHAT_ID			1


struct _wd_chat {
	wi_runtime_base_t				base;
	
	wd_cid_t						id;
	wd_topic_t						*topic;
	wi_mutable_array_t				*users;
	
	wi_recursive_lock_t				*lock;
};


struct _wd_topic {
	wi_runtime_base_t				base;
	
	wi_string_t						*topic;
	wi_date_t						*date;
	wi_string_t						*nick;
};


static wd_chat_t *					wd_chat_alloc(void);
static wd_chat_t *					wd_chat_init(wd_chat_t *);
static wd_chat_t *					wd_chat_init_public_chat(wd_chat_t *);
static wd_chat_t *					wd_chat_init_private_chat(wd_chat_t *);
static void							wd_chat_dealloc(wi_runtime_instance_t *);
static wi_string_t *				wd_chat_description(wi_runtime_instance_t *);

static wd_cid_t						wd_chat_random_id(void);

static wd_topic_t *					wd_topic_alloc(void);
static wd_topic_t *					wd_topic_init_with_user_and_string(wd_topic_t *, wd_user_t *, wi_string_t *);
static wd_topic_t *					wd_topic_init_with_dictionary(wd_topic_t *, wi_dictionary_t *);
static void							wd_topic_dealloc(wi_runtime_instance_t *);

static wi_dictionary_t *			wd_topic_dictionary(wd_topic_t *topic);


wd_chat_t							*wd_public_chat;

static wi_mutable_dictionary_t		*wd_chats;

static wi_string_t					*wd_topic_path;

static wi_runtime_id_t				wd_chat_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t			wd_chat_runtime_class = {
	"wd_chat_t",
	wd_chat_dealloc,
	NULL,
	NULL,
	wd_chat_description,
	NULL
};

static wi_runtime_id_t				wd_topic_runtime_id = WI_RUNTIME_ID_NULL;
static wi_runtime_class_t			wd_topic_runtime_class = {
	"wd_topic_t",
	wd_topic_dealloc,
	NULL,
	NULL,
	NULL,
	NULL
};



void wd_chats_init(void) {
	wi_runtime_instance_t	*instance;
	
	wd_chat_runtime_id = wi_runtime_register_class(&wd_chat_runtime_class);
	wd_topic_runtime_id = wi_runtime_register_class(&wd_topic_runtime_class);

	wd_chats = wi_dictionary_init(wi_mutable_dictionary_alloc());

	wd_topic_path = WI_STR("topic");

	wd_public_chat = wd_chat_init_public_chat(wd_chat_alloc());
	wd_chats_add_chat(wd_public_chat);
	
	instance = wi_plist_read_instance_from_file(wd_topic_path);
	
	if(instance && wi_runtime_id(instance) == wi_dictionary_runtime_id())
		wd_public_chat->topic = wd_topic_init_with_dictionary(wd_topic_alloc(), instance);
}



#pragma mark -

void wd_chats_add_chat(wd_chat_t *chat) {
	wi_dictionary_wrlock(wd_chats);
	wi_mutable_dictionary_set_data_for_key(wd_chats, chat, wi_number_with_int32(chat->id));
	wi_dictionary_unlock(wd_chats);
}



wd_chat_t * wd_chats_chat_with_id(wd_cid_t id) {
	wd_chat_t		*chat;
	
	wi_dictionary_rdlock(wd_chats);
	chat = wi_autorelease(wi_retain(wi_dictionary_data_for_key(wd_chats, wi_number_with_int32(id))));
	wi_dictionary_unlock(wd_chats);
	
	return chat;
}



void wd_chats_remove_user(wd_user_t *user) {
	wi_enumerator_t	*enumerator;
	wd_chat_t		*chat;
	void			*key;

	wi_dictionary_wrlock(wd_chats);
	
	enumerator = wi_array_data_enumerator(wi_dictionary_all_keys(wd_chats));
	
	while((key = wi_enumerator_next_data(enumerator))) {
		chat = wi_dictionary_data_for_key(wd_chats, key);
		
		wi_array_wrlock(chat->users);
		wi_mutable_array_remove_data(chat->users, user);
		wi_array_unlock(chat->users);

		if(chat != wd_public_chat && wi_array_count(chat->users) == 0)
			wi_mutable_dictionary_remove_data_for_key(wd_chats, key);
	}
	
	wi_dictionary_unlock(wd_chats);
}



#pragma mark -

wd_chat_t * wd_chat_private_chat(void) {
	return wi_autorelease(wd_chat_init_private_chat(wd_chat_alloc()));
}



#pragma mark -

static wd_chat_t * wd_chat_alloc(void) {
	return wi_runtime_create_instance(wd_chat_runtime_id, sizeof(wd_chat_t));
}



static wd_chat_t * wd_chat_init(wd_chat_t *chat) {
	chat->users		= wi_array_init(wi_mutable_array_alloc());
	chat->lock		= wi_recursive_lock_init(wi_recursive_lock_alloc());
	
	return chat;
}



static wd_chat_t * wd_chat_init_public_chat(wd_chat_t *chat) {
	chat = wd_chat_init(chat);
	
	chat->id = WD_PUBLIC_CHAT_ID;
	
	return chat;
}



static wd_chat_t * wd_chat_init_private_chat(wd_chat_t *chat) {
	chat = wd_chat_init(chat);
	
	chat->id = wd_chat_random_id();
	
	return chat;
}



static void wd_chat_dealloc(wi_runtime_instance_t *instance) {
	wd_chat_t		*chat = instance;

	wi_release(chat->users);
	wi_release(chat->topic);
	wi_release(chat->lock);
}



static wi_string_t * wd_chat_description(wi_runtime_instance_t *instance) {
	wd_chat_t		*chat = instance;

	return wi_string_with_format(WI_STR("<%@ %p>{id = %u, users = %@}"),
		wi_runtime_class_name(chat),
		chat,
		chat->id,
		chat->users);
}



#pragma mark -

static wd_cid_t wd_chat_random_id(void) {
	wd_cid_t	id;

	do {
		id = ((wd_cid_t) random() % UINT_MAX) + 1;
	} while(wd_chats_chat_with_id(id));
	
	return id;
}



#pragma mark -

wi_boolean_t wd_chat_contains_user(wd_chat_t *chat, wd_user_t *user) {
	wi_boolean_t	contains;

	wi_array_rdlock(chat->users);
	contains = wi_array_contains_data(chat->users, user);
	wi_array_unlock(chat->users);

	return contains;
}



void wd_chat_add_user_and_broadcast(wd_chat_t *chat, wd_user_t *user) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.chat.user_join"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(message, chat->id, WI_STR("wired.chat.id"));
	wi_p7_message_set_uint32_for_name(message, wd_user_id(user), WI_STR("wired.user.id"));
	wi_p7_message_set_bool_for_name(message, wd_user_is_admin(user), WI_STR("wired.user.admin"));
	wi_p7_message_set_bool_for_name(message, wd_user_is_idle(user), WI_STR("wired.user.idle"));
	wi_p7_message_set_string_for_name(message, wd_user_nick(user), WI_STR("wired.user.nick"));
	wi_p7_message_set_string_for_name(message, wd_user_status(user), WI_STR("wired.user.status"));
	wi_p7_message_set_data_for_name(message, wd_user_icon(user), WI_STR("wired.user.icon"));
	wi_p7_message_set_date_for_name(message, wd_user_idle_time(user), WI_STR("wired.user.idle_time"));
	wd_chat_broadcast_message(chat, message);
	
	wi_array_wrlock(chat->users);
	wi_mutable_array_add_data(chat->users, user);
	wi_array_unlock(chat->users);
}



void wd_chat_remove_user(wd_chat_t *chat, wd_user_t *user) {
	wi_array_wrlock(chat->users);
	wi_mutable_array_remove_data(chat->users, user);
	wi_array_unlock(chat->users);

	if(chat != wd_public_chat && wi_array_count(chat->users) == 0) {
		wi_dictionary_wrlock(wd_chats);
		wi_mutable_dictionary_remove_data_for_key(wd_chats, wi_number_with_int32(chat->id));
		wi_dictionary_unlock(wd_chats);
	}
}



#pragma mark -

void wd_chat_reply_user_list(wd_chat_t *chat, wd_user_t *user, wi_p7_message_t *message) {
	wi_enumerator_t		*enumerator;
	wi_p7_message_t		*reply;
	wd_user_t			*peer;
	
	wi_array_rdlock(chat->users);
	
	enumerator = wi_array_data_enumerator(chat->users);
	
	while((peer = wi_enumerator_next_data(enumerator))) {
		if(wd_user_state(peer) == WD_USER_LOGGED_IN) {
			reply = wi_p7_message_with_name(WI_STR("wired.chat.user_list"), wd_p7_spec);
			wi_p7_message_set_uint32_for_name(reply, chat->id, WI_STR("wired.chat.id"));
			wi_p7_message_set_uint32_for_name(reply, wd_user_id(peer), WI_STR("wired.user.id"));
			wi_p7_message_set_bool_for_name(reply, wd_user_is_admin(peer), WI_STR("wired.user.admin"));
			wi_p7_message_set_bool_for_name(reply, wd_user_is_idle(peer), WI_STR("wired.user.idle"));
			wi_p7_message_set_string_for_name(reply, wd_user_nick(peer), WI_STR("wired.user.nick"));
			wi_p7_message_set_string_for_name(reply, wd_user_status(peer), WI_STR("wired.user.status"));
			wi_p7_message_set_data_for_name(reply, wd_user_icon(peer), WI_STR("wired.user.icon"));
			wi_p7_message_set_date_for_name(reply, wd_user_idle_time(peer), WI_STR("wired.user.idle_time"));
			wd_user_reply_message(user, reply, message);
		}
	}
	
	wi_array_unlock(chat->users);

	reply = wi_p7_message_with_name(WI_STR("wired.chat.user_list.done"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(reply, chat->id, WI_STR("wired.chat.id"));
	wd_user_reply_message(user, reply, message);
}



void wd_chat_broadcast_user_leave(wd_chat_t *chat, wd_user_t *user) {
	wi_p7_message_t		*message;
	
	message = wi_p7_message_with_name(WI_STR("wired.chat.user_leave"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(message, chat->id, WI_STR("wired.chat.id"));
	wi_p7_message_set_uint32_for_name(message, wd_user_id(user), WI_STR("wired.user.id"));
	wd_chat_broadcast_message(chat, message);
}



wi_p7_message_t * wd_chat_topic_message(wd_chat_t *chat) {
	wi_p7_message_t		*message;
	wd_topic_t			*topic;
	
	topic = wd_chat_topic(chat);
	
	if(!topic)
		return NULL;
	
	message = wi_p7_message_with_name(WI_STR("wired.chat.topic"), wd_p7_spec);
	wi_p7_message_set_uint32_for_name(message, chat->id, WI_STR("wired.chat.id"));
	wi_p7_message_set_string_for_name(message, topic->nick, WI_STR("wired.user.nick"));
	wi_p7_message_set_string_for_name(message, topic->topic, WI_STR("wired.chat.topic.topic"));
	wi_p7_message_set_date_for_name(message, topic->date, WI_STR("wired.chat.topic.time"));
	
	return message;
}



#pragma mark -

void wd_chat_set_topic(wd_chat_t *chat, wd_topic_t *topic) {
	wi_recursive_lock_lock(chat->lock);
	wi_retain(topic);
	wi_release(chat->topic);
	chat->topic = topic;
	wi_recursive_lock_unlock(chat->lock);
	
	if(chat->id == WD_PUBLIC_CHAT_ID) {
		if(!wi_plist_write_instance_to_file(wd_topic_dictionary(topic), wd_topic_path))
			wi_log_err(WI_STR("Could not write to %@: %m"), wd_topic_path);
	}
}



wd_topic_t * wd_chat_topic(wd_chat_t *chat) {
	wd_topic_t		*topic;
	
	wi_recursive_lock_lock(chat->lock);
	topic = wi_autorelease(wi_retain(chat->topic));
	wi_recursive_lock_unlock(chat->lock);
	
	return topic;
}



#pragma mark -

wd_cid_t wd_chat_id(wd_chat_t *chat) {
	return chat->id;
}



wi_array_t * wd_chat_users(wd_chat_t *chat) {
	return chat->users;
}



#pragma mark -

wd_topic_t * wd_topic_with_user_and_string(wd_user_t *user, wi_string_t *string) {
	return wi_autorelease(wd_topic_init_with_user_and_string(wd_topic_alloc(), user, string));
}



#pragma mark -

static wd_topic_t * wd_topic_alloc(void) {
	return wi_runtime_create_instance(wd_topic_runtime_id, sizeof(wd_topic_t));
}



static wd_topic_t * wd_topic_init_with_user_and_string(wd_topic_t *topic, wd_user_t *user, wi_string_t *string) {
	topic->topic	= wi_retain(string);
	topic->date		= wi_date_init(wi_date_alloc());
	topic->nick		= wi_copy(wd_user_nick(user));
	
	return topic;
}



static wd_topic_t * wd_topic_init_with_dictionary(wd_topic_t *topic, wi_dictionary_t *dictionary) {
	topic->topic	= wi_retain(wi_dictionary_data_for_key(dictionary, WI_STR("wired.chat.topic.topic")));
	topic->date		= wi_retain(wi_dictionary_data_for_key(dictionary, WI_STR("wired.chat.topic.time")));
	topic->nick		= wi_retain(wi_dictionary_data_for_key(dictionary, WI_STR("wired.user.nick")));
	
	if(!topic->topic || !topic->date || !topic->nick) {
		wi_release(topic);
		
		return NULL;
	}
	
	return topic;
}



static void wd_topic_dealloc(wi_runtime_instance_t *instance) {
	wd_topic_t		*topic = instance;
	
	wi_release(topic->topic);
	wi_release(topic->date);
	wi_release(topic->nick);
}



#pragma mark -

static wi_dictionary_t * wd_topic_dictionary(wd_topic_t *topic) {
	return wi_dictionary_with_data_and_keys(
		topic->topic,	WI_STR("wired.chat.topic.topic"),
		topic->date,	WI_STR("wired.chat.topic.time"),
		topic->nick,	WI_STR("wired.user.nick"),
		NULL);
}
