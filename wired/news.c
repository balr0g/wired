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

#include <wired/wired.h>

#include "news.h"
#include "server.h"
#include "settings.h"

#define WD_NEWS_FIELD_SEPARATOR		"\34"
#define WD_NEWS_POST_SEPARATOR		"\35"


static wi_string_t					*wd_news_path;
static wi_rwlock_t					*wd_news_lock;


void wd_news_init(void) {
	wd_news_path = WI_STR("news");
	wd_news_lock = wi_rwlock_init(wi_rwlock_alloc());
}



#pragma mark -

void wd_news_reply_news(wd_user_t *user, wi_p7_message_t *message) {
	wi_p7_message_t		*reply;
	wi_array_t			*array;
	wi_file_t			*file;
	wi_string_t			*string, *post_separator, *field_separator;

	wi_rwlock_rdlock(wd_news_lock);
	
	file = wi_file_for_reading(wd_news_path);
	
	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_news_path);

		goto end;
	}
	
	post_separator = WI_STR(WD_NEWS_POST_SEPARATOR);
	field_separator = WI_STR(WD_NEWS_FIELD_SEPARATOR);
	
	while((string = wi_file_read_to_string(file, post_separator))) {
		array = wi_string_components_separated_by_string(string, field_separator);
		
		if(wi_array_count(array) == 3) {
			reply = wi_p7_message_with_name(WI_STR("wired.news.list"), wd_p7_spec);
			wi_p7_message_set_string_for_name(reply, WI_ARRAY(array, 0), WI_STR("wired.user.nick"));
			wi_p7_message_set_date_for_name(reply, wi_date_with_rfc3339_string(WI_ARRAY(array, 1)), WI_STR("wired.news.time"));
			wi_p7_message_set_string_for_name(reply, WI_ARRAY(array, 2), WI_STR("wired.news.post"));
			wd_user_reply_message(user, reply, message);
		}
	}
		
end:
	wi_rwlock_unlock(wd_news_lock);

	reply = wi_p7_message_with_name(WI_STR("wired.news.list.done"), wd_p7_spec);
	wd_user_reply_message(user, reply, message);
}



void wd_news_post_news(wd_user_t *user, wi_string_t *news) {
	wi_p7_message_t		*message;
	wi_file_t			*file, *tmpfile = NULL;
	wi_string_t			*string, *separator, *post = NULL;
	wi_date_t			*date;
	wi_uinteger_t		i = 0;
	wi_integer_t		newslimit;
	wi_boolean_t		first = true;
	
	wi_rwlock_wrlock(wd_news_lock);
	
	file = wi_file_for_updating(wd_news_path);

	if(!file) {
		wi_log_err(WI_STR("Could not open %@: %m"), wd_news_path);

		goto end;
	}

	tmpfile = wi_file_temporary_file();
	
	if(!tmpfile) {
		wi_log_err(WI_STR("Could not create a temporary file: %m"));

		goto end;
	}
	
	date = wi_date();
	
	post = wi_string_with_format(WI_STR("%#@%s%#@%s%#@"),
		 wd_user_nick(user),				WD_NEWS_FIELD_SEPARATOR,
		 wi_date_rfc3339_string(date),		WD_NEWS_FIELD_SEPARATOR,
		 news);
	
	wi_file_write_format(tmpfile, WI_STR("%#@%s"), post, WD_NEWS_POST_SEPARATOR);
	
	while((string = wi_file_read(file, WI_FILE_BUFFER_SIZE)))
		wi_file_write_format(tmpfile, WI_STR("%@"), string);
	
	wi_file_truncate(file, 0);
	wi_file_seek(tmpfile, 0);
	
	newslimit = wi_config_integer_for_name(wd_config, WI_STR("news limit"));
	
	separator = WI_STR(WD_NEWS_POST_SEPARATOR);
	
	while((string = wi_file_read_to_string(tmpfile, separator))) {
		if(!first)
			wi_file_write_format(file, WI_STR("%s"), WD_NEWS_POST_SEPARATOR);
		
		wi_file_write_format(file, WI_STR("%@"), string);

		first = false;

		if(newslimit > 0) {
			if(i >= (wi_uinteger_t) newslimit)
				break;

			i++;
		}
	}
	
	message = wi_p7_message_with_name(WI_STR("wired.news.post"), wd_p7_spec);
	wi_p7_message_set_string_for_name(message, wd_user_nick(user), WI_STR("wired.user.nick"));
	wi_p7_message_set_date_for_name(message, date, WI_STR("wired.news.time"));
	wi_p7_message_set_string_for_name(message, news, WI_STR("wired.news.post"));
	wd_chat_broadcast_message(wd_public_chat, message);

end:
	wi_rwlock_unlock(wd_news_lock);
}



void wd_news_clear_news(void) {
	wi_rwlock_wrlock(wd_news_lock);

	if(!wi_file_clear(wd_news_path))
		wi_log_err(WI_STR("Could not clear %@: %m"), wd_news_path);

	wi_rwlock_unlock(wd_news_lock);
}
