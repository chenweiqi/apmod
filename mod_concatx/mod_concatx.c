/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * mod_concatx.c
 * mod_concatx.c: concatantes several files together
 * URLs will be in the form /<basedir>/??file1,dir2/file2,...
 * eg: http://www.example.com/js/??js1.js,js2.js
 * Thanks to the great job of mod_concat.c
 * Vicky
 * 2013/7/17
 */

/**
 * usage:(file location: Apache2.2\conf\httpd.conf)
	LoadModule concatx_module modules/mod_concatx.dll

	#default setting
	<IfModule concatx_module>
	ConcatxDisable Off
	ConcatxCheckModified On
	ConcatxSeparator On
	ConcatxMaxSize 1024
	ConcatxMaxCount 10
	ConcatxFileType js,css
	</IfModule>
 *
 */

/*
 * Original edition explanation:
 * mod_concat.c
 * mod_concat.c: concatantes several files together
 * URLs will be in the form /<basedir>/??file1,dir2/file2,...
 * The Idea was initially thought of by David Davis in Vox, and reimplemented in perlbal.
 * Ian Holsman
 * 15/6/7
 */


#include "apr_strings.h"
#include "apr_fnmatch.h"
#include "apr_strings.h"
#include "apr_lib.h"
#define APR_WANT_STRFUNC
#include "apr_want.h"

#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#define CORE_PRIVATE
#include "http_core.h"
#include "http_request.h"
#include "http_protocol.h"
#include "http_log.h"
#include "http_main.h"
#include "util_script.h"

#include "mod_core.h"

module AP_MODULE_DECLARE_DATA concatx_module;

typedef struct concat_config_struct {
	int disabled;
	int checkModified;
	int separator;
} concat_config_rec;

static int max_count = 10;
static apr_off_t max_length = 1024* 1024;	//bytes
static int set_file_type = -1;				//-1:default limit;0:no limit;>0:user limit
static char **sep_file_type = NULL;

static const char s_szDefalteFilterName[]="DEFLATE";  // gzip; deflate
extern  __declspec(dllimport) module **ap_loaded_modules;
static int mod_defalte_loaded = 0;

static const char *
	get_max_size(cmd_parms *cmd, void *dconfig, const char *value) {
		long n = strtol(value, NULL, 0);
		if (n<=0) {
			n = 1024;
		}
		max_length = n * 1024;
		return NULL;
}

static const char *
	get_max_count(cmd_parms *cmd, void *dconfig, const char *value) {
		long n = strtol(value, NULL, 0);
		if (n<=0) {
			max_count = 10;
		} else {
			max_count = n;
		}
		return NULL;
}

static const char *
	get_file_type(cmd_parms *cmd, void *dconfig, const char *value) {
		if (value != NULL && value[0] != 0) {
			char *configs;
			char *token;
			char *strtokstate;
			int count = 0;
			int memsize = 8;
			apr_pool_t *p = cmd->pool;
			char **sep = (char **)malloc(sizeof(char *) * memsize);
			char **sept = NULL;

			configs = apr_pstrdup(p, value);
			token = apr_strtok(configs, ",", &strtokstate);
			while(token){
				if(count >= memsize){
					memsize += 8;
					sept = (char **)realloc(sep, sizeof(char *) * memsize);
					if (sept != NULL) {
						sep = sept;
					} else {
						free(sep);
						return NULL;
					}
				}
				sep[count] = token;
				count ++;
				token = apr_strtok(NULL, ",", &strtokstate);
			}

			set_file_type = count;

			if(count > 0){
				sep_file_type = (char**)apr_pmemdup(p, sep, sizeof(char *) * memsize);
			}

			free(sep);
			sep = NULL;
		}

		return NULL;
}

static const command_rec concat_cmds[] =
{

	AP_INIT_FLAG("ConcatxDisable", ap_set_flag_slot,
	(void *)APR_OFFSETOF(concat_config_rec, disabled),
	OR_INDEXES, "disable concat in this location"),
	AP_INIT_FLAG("ConcatxCheckModified", ap_set_flag_slot,
	(void *)APR_OFFSETOF(concat_config_rec, checkModified),
	OR_INDEXES, "if check modified in this location"),
	AP_INIT_FLAG("ConcatxSeparator", ap_set_flag_slot,
	(void *)APR_OFFSETOF(concat_config_rec, separator),
	OR_INDEXES, "if add separator in this location"),
	AP_INIT_TAKE1("ConcatxMaxSize", get_max_size,
	NULL, RSRC_CONF, "concat max size in one request"),
	AP_INIT_TAKE1("ConcatxMaxCount", get_max_count,
	NULL, RSRC_CONF, "concat max count in one request"),
	AP_INIT_TAKE1("ConcatxFileType", get_file_type,
	NULL, RSRC_CONF, "which file type is allowed"),
	{NULL}
};

static void *create_concat_config(apr_pool_t *p, char *dummy)
{
	int n;
	concat_config_rec *newv =
		(concat_config_rec *) apr_pcalloc(p, sizeof(concat_config_rec));

	newv->disabled = 2;
	newv->checkModified = 2;
	newv->separator = 2;

	for (n = 0; ap_loaded_modules[n]; ++n) {
		char *s = (char *) ap_loaded_modules[n]->name;
		if (apr_strnatcasecmp(s, "mod_deflate.c") == 0) {
			mod_defalte_loaded = 1;
			break;
		}
	}
	return (void *) newv;
}

static void *merge_concat_configs(apr_pool_t *p, void *basev, void *addv)
{
	concat_config_rec *newv;
	concat_config_rec *base = (concat_config_rec *) basev;
	concat_config_rec *add = (concat_config_rec *) addv;

	newv = (concat_config_rec *) apr_pcalloc(p, sizeof(concat_config_rec));
	if (add->disabled == 2) {
		newv->disabled = base->disabled;
	} else {
		newv->disabled = add->disabled;
	}

	if (add->checkModified == 2) {
		newv->checkModified = base->checkModified;
	} else {
		newv->checkModified = add->checkModified;
	}

	if (add->separator == 2) {
		newv->separator = base->separator;
	} else {
		newv->separator = add->separator;
	}

	return newv;
}

static int concat_handler(request_rec *r)
{
	concat_config_rec *conf;
	conn_rec *c = r->connection;

	core_dir_config *d;
	apr_off_t length=0;
	apr_time_t mtime;
	int count=0;
	char *file_string;
	char *token;
	char *strtokstate;
	apr_bucket_brigade *bb;

	apr_bucket *b;
	apr_status_t rv;

	char *crlf = "\r\n";
	int crlfLen = strlen(crlf);

	int setFileType = set_file_type;
	char **sepFileType = sep_file_type;

	int maxCount = max_count;
	apr_off_t maxLength = max_length;

	r->allowed |= (AP_METHOD_BIT << M_GET);
	if (r->method_number != M_GET) {
		return DECLINED;
	}

	if (!r->args || r->args[0] != '?') {
		return DECLINED;
	}

	conf = (concat_config_rec *) ap_get_module_config(r->per_dir_config, &concatx_module);
	if (conf->disabled == 1)
		return DECLINED;

	d = (core_dir_config *)ap_get_module_config(r->per_dir_config,	&core_module);

	file_string = &(r->args[1]);
	token = apr_strtok(file_string, ",", &strtokstate);
	bb = apr_brigade_create(r->pool, c->bucket_alloc);

	if(token == NULL)
		return DECLINED;

	while (token) {
		char *filename;
		char *file2;
		char *tmp;
		apr_file_t *f = NULL;
		apr_finfo_t finfo;

		if(++count > maxCount)
			break;

		// deal with http://xxx/js/??a.js,b.js?ver=123
		tmp = strchr(token, '?');
		if (tmp != NULL)	{
			tmp[0] = '\0';
		}

		if(setFileType != 0){
			char *ext = strrchr(token, '.');
			if(ext == NULL){
				ap_log_rerror(APLOG_MARK, APLOG_ERR, (apr_status_t)NULL, r,
					"mod_concat:filename looks illegal: %s", token);
				return HTTP_FORBIDDEN;
			}
			ext ++;
			if(setFileType == -1){
				if(apr_strnatcasecmp(ext, "js") != 0 && apr_strnatcasecmp(ext, "css") != 0){
					ap_log_rerror(APLOG_MARK, APLOG_ERR, (apr_status_t)NULL, r,
						"mod_concat:filename looks illegal: %s", token);
					return HTTP_FORBIDDEN;
				}
			}else{
				int i = 0, m = 0;
				for(;i<set_file_type;i++){
					if(apr_strnatcasecmp(ext, sepFileType[i]) == 0){
						m = 1;
						break;
					}
				}
				if(m == 0){
					ap_log_rerror(APLOG_MARK, APLOG_ERR, (apr_status_t)NULL, r,
						"mod_concat:filename looks illegal: %s", token);
					return HTTP_FORBIDDEN;
				}
			}
		}

		rv = apr_filepath_merge(&file2, NULL, token,
			APR_FILEPATH_SECUREROOTTEST |
			APR_FILEPATH_NOTABSOLUTE, r->pool);

		if (rv != APR_SUCCESS) {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
				"mod_concat:filename looks fishy: %s", token);
			return HTTP_FORBIDDEN;
		}

		// bugfix: while default page is there, apr will give me http://xxx/index.html??1.js,2.js
		tmp = strrchr(r->filename, '/');
		tmp[1] = '\0';
		
		filename = apr_pstrcat (r->pool, r->filename,  file2, NULL);
		if ((rv = apr_file_open(&f, filename, APR_READ
#if APR_HAS_SENDFILE
			| ((d->enable_sendfile == ENABLE_SENDFILE_OFF)
			? 0 : APR_SENDFILE_ENABLED)
#endif
			, APR_OS_DEFAULT, r->pool)) != APR_SUCCESS) {
				ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
					"mod_concat:file permissions deny server access: %s %s", filename,r->uri);
				return HTTP_FORBIDDEN;
		}
		if (( rv = apr_file_info_get( &finfo, APR_FINFO_MIN, f))!= APR_SUCCESS )  {
			ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
				"mod_concat:file info failure: %s", filename);
			return HTTP_INTERNAL_SERVER_ERROR;
		}

		length += finfo.size;
		if (count == 1) {
			request_rec *sub_req;
			mtime = finfo.mtime;
			sub_req = ap_sub_req_lookup_file(filename, r, NULL);
			if (sub_req->status != HTTP_OK) {
				int res = sub_req->status;
				ap_destroy_sub_req(sub_req);
				return res;
			}
			ap_set_content_type(r, sub_req->content_type);
		}
		else {
			if (finfo.mtime > mtime ) {
				mtime = finfo.mtime;
			}
		}
		apr_brigade_insert_file(bb, f, 0, finfo.size, r->pool);
		if(conf->separator != 0){
			apr_brigade_puts(bb, NULL, NULL, crlf);
			length += crlfLen;
		}
		if(length > maxLength)
			break;
		token = apr_strtok( NULL, ",", &strtokstate);
	}
	b = apr_bucket_eos_create(c->bucket_alloc);
	APR_BRIGADE_INSERT_TAIL(bb, b);
	ap_set_content_length(r, length);
	r->mtime = mtime;
	ap_set_last_modified(r);

	if(conf->checkModified != 0){
		const char *ifSetMod = apr_table_get(r->headers_in, "If-Modified-Since");
		if(ifSetMod != NULL && apr_strnatcmp(apr_table_get(r->headers_out, "Last-Modified"), ifSetMod) == 0){
			return HTTP_NOT_MODIFIED;
		}
	}

	apr_table_unset(r->headers_out, "ETag");
	rv = ap_pass_brigade(r->output_filters, bb);
	if (rv != APR_SUCCESS && !c->aborted) {
		ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
			"mod_concat: ap_pass_brigade failed for uri %s", r->uri);
		return HTTP_INTERNAL_SERVER_ERROR;
	}
	return OK;
}

static int concat_type_checker(request_rec *r)
{
	if (!r->args || r->args[0] != '?') {
		return DECLINED;
	}

	// support gzip
	if ( mod_defalte_loaded ) {
		ap_add_output_filter(s_szDefalteFilterName,NULL,r,r->connection);
	}
	
    return DECLINED;
}

static void register_hooks(apr_pool_t *p)
{
	static const char * const aszPost[] = { "mod_autoindex.c", NULL };
	// we want to have a look at the directories *BEFORE* autoindex gets to it
    ap_hook_type_checker(concat_type_checker, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(concat_handler,NULL,aszPost,APR_HOOK_MIDDLE);

}

module AP_MODULE_DECLARE_DATA concatx_module =
{
	STANDARD20_MODULE_STUFF,
	create_concat_config,    /* dir config creater */
	merge_concat_configs,    /* dir merger --- default is to override */
	NULL,                    /* server config */
	NULL,                    /* merge server config */
	concat_cmds,             /* command apr_table_t */
	register_hooks           /* register hooks */
};