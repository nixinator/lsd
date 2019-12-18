/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * config.c
 *
 * this file is part of LIBRESTACK
 *
 * Copyright (c) 2012-2019 Brett Sheffield <bacs@librecast.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see the file COPYING in the distribution).
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "db.h"
#include "err.h"
#include "lsd.h"
#include "log.h"
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

int debug = 0;

char * config_db(char db, char name[2])
{
	name[0] = db + '0';
	name[1] = 0;
	return name;
}

int config_bool_convert(char *val, int *ival)
{
	int i;
	char *truth[] = { "1", "true", "yes", "on", "y", "aye" };
	char *falsy[] = { "0", "false", "no", "off", "n", "nae" };
	for (i = 0; i < (int)sizeof(truth) / (int)sizeof(char *); i++) {
		if (strcmp(val, truth[i]) == 0) {
			*ival = 1;
			return 1;
		}
	}
	for (i = 0; i <(int) sizeof(falsy) / (int)sizeof(char *); i++) {
		if (strcmp(val, falsy[i]) == 0) {
			*ival = 0;
			return 1;
		}
	}
	return 0;
}

/* boolean to yes/no */
char * btos(int b)
{
	return (b) ? "yes" : "no";
}

int config_isbool(char *key)
{
	CONFIG_BOOLEANS(CONFIG_IN)
	return 0;
}

int config_isint(char *key)
{
	CONFIG_INTEGERS(CONFIG_IN)
	return 0;
}

int config_isstr(char *key)
{
	CONFIG_STRINGS(CONFIG_IN)
	return 0;
}

int config_isopt(char *key)
{
	CONFIG_BOOLEANS(CONFIG_IN)
	CONFIG_INTEGERS(CONFIG_IN)
	CONFIG_STRINGS(CONFIG_IN)
	return 0;
}

int config_min(char *key)
{
	CONFIG_LIMITS(CONFIG_MIN)
	return INT_MIN;
}

int config_max(char *key)
{
	CONFIG_LIMITS(CONFIG_MAX)
	return INT_MAX;
}

char * config_key(char *key)
{
	CONFIG_BOOLEANS(CONFIG_KEY)
	CONFIG_INTEGERS(CONFIG_KEY)
	CONFIG_STRINGS(CONFIG_KEY)
	return NULL;
}

/* return false if string contains any non-numeric characters */
int isnumeric(char *v)
{
	for (int i = 0; i < (int)strlen(v); i++) {
		if (!isdigit(v[i])) return 0;
	}
	return 1;
}

/* set key to val if numeric and within limits */
int config_int_set(char *klong, int *key, char *val)
{
	int min, max, i;

	if (!isnumeric(val)) return 0;
	i = atoi(val);
	min = config_min(klong);
	max = config_max(klong);
	if (i < min || i > max) {
		ERROR("%s value must be between %i and %i", klong, min, max);
		return 0;
	}
	*key = i;

	return 1;
}

int config_process_proto(char *line, size_t len, MDB_txn *txn, MDB_dbi dbi)
{
	proto_t *p;
	struct servent *service = NULL;
	MDB_val k,v;
	char *ptr = NULL;
	char *proto = NULL;
	char socktype[len];
	size_t n = 0;
	int err = 0;

	/* module (eg. https) */
	ptr = line;
	while (len > 0 && len-- && !isspace(*line++));		/* find end */
	n = line - ptr - 1;
	v.mv_size = sizeof(proto_t) + n + 1;
	p = calloc(1, v.mv_size);
	memcpy(p->module, ptr, n);
	while (len > 0 && isblank(*line)){line++;len--;}	/* skip whitespace */

	/* port */
	if (isdigit(line[0])) {
		for (; isdigit(line[0]); len--) {
			p->port *= 10;
			p->port += (unsigned char)*line++ - '0';
		}
		if (line[0] == '/') {
			line++; len--; /* skip slash */
		}
	}
	else {	/* port not provided, look it up */
		service = getservbyname(p->module, NULL);
		if (service) {
			p->port = ntohs(service->s_port);
			proto = service->s_proto;
		}
		else {
			ERROR("Unable to find port for service '%s'", p->module);
			err = LSD_ERROR_CONFIG_INVALID;
		}
	}

	/* socktype */
	if ((!err && len) || service) {
		if (!service) {
			ptr = line;
			while (len > 0 && len-- && !isspace(*line++));	/* find end */
			n = line - ptr - 1;
			memcpy(socktype, ptr, n);
			socktype[n] = '\0';
			if (strlen(socktype) > 0)
				proto = socktype;
		}
		if (!proto && !service) { /* lookup protocol for service */
			service = getservbyname(p->module, NULL);
			if (service) {
				proto = service->s_proto;
			}
		}
		if (!strncmp(proto, "tcp", 3)) {
			p->socktype = SOCK_STREAM;
		}
		else if (!strncmp(proto, "udp", 3)) {
			p->socktype = SOCK_DGRAM;
		}
		else if (!strncmp(proto, "raw", 3)) {
			p->socktype = SOCK_RAW;
		}
		else if (!strncmp(proto, "rdm", 3)) {
			p->socktype = SOCK_RDM;
		}
		else {
			ERROR("Invalid protocol '%s'", proto);
			err = LSD_ERROR_CONFIG_INVALID;
		}
		while (len > 0 && isblank(*line)){line++;len--;} /* skip whitespace */
	}

	/* address */
	if (!err) {
		if (len) {
			ptr = line;
			while (len > 0 && len-- && !isspace(*line++));	/* find end */
			n = line - ptr - 1;
			memcpy(p->addr, ptr, n);
			/* TODO: verify address is valid */
		}
		else {
			snprintf(p->addr, strlen(DEFAULT_LISTEN_ADDR) + 1, DEFAULT_LISTEN_ADDR);
		}
	}

	DEBUG("[%s][%u][%u][%s]", p->module, p->port, p->socktype, p->addr);

	if (!err) {
		/* write to db */
		k.mv_size = 6;
		k.mv_data = "proto";
		v.mv_data = p;
		err = mdb_put(txn, dbi, &k, &v, 0);
		if (err) {
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
	}

	endservent();
	free(p);

	return err;
}

#if 0
void config_process_uri(char *line, size_t len)
{
	uri_t *p, *s;

	DEBUG("processing uri");
	s = calloc(1, sizeof(uri_t));
	s->uri = line;
	s->uri_len = len;
	if (c->uris) {
		for (p = c->uris; p->next; p = p->next);
		p->next = s;
	}
	else {
		c->uris = s;
	}
}
#endif

void config_close()
{
	mdb_env_close(env);
}

/* fetch and return a copy */
int config_get_copy(const char *db, char *key, MDB_val *val, MDB_txn *txn, MDB_dbi dbi)
{
	int err = 0;
	char txn_close = 0;
	char dbi_close = 0;
	MDB_val k;
	MDB_val v;

	k.mv_size = strlen(key) + 1;
	k.mv_data = key;

	/* create new transaction and dbi handle if none */
	if (!txn) {
		DEBUG("new txn");
		if ((err = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0) {
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
		txn_close = 1;
	}
	if (!dbi) {
		DEBUG("new dbi");
		if ((err = mdb_dbi_open(txn, db, 0, &dbi)) != 0) {
			ERROR("%s: %s", __func__, mdb_strerror(err));
		}
		dbi_close = 1;
	}

	err = mdb_get(txn, dbi, &k, &v);
	if ((err != 0) && (err != MDB_NOTFOUND)) {
		ERROR("%s: %s", __func__, mdb_strerror(err));
	}
	else {	/* return a copy of the data */
		val->mv_size = v.mv_size;
		memcpy(val->mv_data, &v.mv_data, v.mv_size);
	}

	/* close handles that were opened here */
	if (dbi_close) mdb_dbi_close(env, dbi);
	if (txn_close) mdb_txn_abort(txn);

	return err;
}

int config_get(char *key, MDB_val *val, MDB_txn *txn, MDB_dbi dbi)
{
	int err = 0;
	MDB_val k;

	k.mv_size = strlen(key) + 1;
	k.mv_data = key;
	err = mdb_get(txn, dbi, &k, val);
	if ((err != 0) && (err != MDB_NOTFOUND))
		ERROR("%s: %s", __func__, mdb_strerror(err));

	return err;
}

int config_del(const char *db, char *key, char *val, MDB_txn *txn, MDB_dbi dbi)
{
	int commit = 0;
	int err = 0;
	MDB_val k,v;

	k.mv_size = strlen(key) + 1;
	k.mv_data = key;

	/* create new transaction and dbi handle if none */
	if (!txn) {
		if ((err = mdb_txn_begin(env, NULL, 0, &txn)) != 0) {
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
		commit = 1;
	}
	if (!dbi) {
		if ((err = mdb_dbi_open(txn, db, MDB_CREATE, &dbi)) != 0) {
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
	}
	if (val) { /* have value, delete matching key+val only */
		v.mv_size = strlen(val) + 1;
		v.mv_data = val;
		err = mdb_del(txn, dbi, &k, &v);
	}
	else { /* no value, delete key */
		err = mdb_del(txn, dbi, &k, NULL);
	}
	if ((err != 0) && (err != MDB_NOTFOUND))
		ERROR("%s(): %s", __func__, mdb_strerror(err));

	/* do not commit existing transactions */
	if (commit)
		err = mdb_txn_commit(txn);

	return err;
}

int config_set(const char *db, char *key, char *val, MDB_txn *txn, MDB_dbi dbi)
{
	int commit = 0;
	int err = 0;
	MDB_val k,v;

	if (!val) return 0;

	/* prepare key + value */
	k.mv_size = strlen(key) + 1;
	k.mv_data = key;
	v.mv_size = strlen(val) + 1;
	v.mv_data = val;

	/* create new transaction and dbi handle if none */
	if (!txn) {
		if ((err = mdb_txn_begin(env, NULL, 0, &txn)) != 0) {
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
		commit = 1;
	}
	if (!dbi) {
		if ((err = mdb_dbi_open(txn, db, MDB_CREATE, &dbi)) != 0) {
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
	}

	/* save key/val */
	if ((err = mdb_put(txn, dbi, &k, &v, 0)) != 0) {
		ERROR("%s(): %s", __func__, mdb_strerror(err));
	}

	/* do not commit existing transactions */
	if (commit)
		err = mdb_txn_commit(txn);

	return err;
}

int config_set_int(const char *db, char *key, int val, MDB_txn *txn, MDB_dbi dbi)
{
	int commit = 0;
	int err = 0;
	MDB_val k,v;

	/* prepare key + value */
	k.mv_size = strlen(key) + 1;
	k.mv_data = key;
	v.mv_size = sizeof(int);
	v.mv_data = &val;

	/* create new transaction and dbi handle if none */
	if (!txn) {
		if ((err = mdb_txn_begin(env, NULL, 0, &txn)) != 0) {
			ERROR("%s(%i): %s", __func__, __LINE__, mdb_strerror(err));
			return err;
		}
		commit = 1;
	}
	if (!dbi) {
		if ((err = mdb_dbi_open(txn, db, MDB_CREATE, &dbi)) != 0) {
			ERROR("%s(%i): %s", __func__, __LINE__, mdb_strerror(err));
			return err;
		}
	}

	/* save key/val */
	if ((err = mdb_put(txn, dbi, &k, &v, 0)) != 0) {
		ERROR("%s(%i): %s", __func__, __LINE__, mdb_strerror(err));
	}
	if (!(debug) && !strcmp(key, "loglevel")) loglevel = val;

	/* do not commit existing transactions */
	if (commit)
		err = mdb_txn_commit(txn);

	return err;
}

int config_yield(char db, char *key, MDB_val *val)
{
	static config_state_t state = CONFIG_INIT;
	static MDB_txn *txn;
	static MDB_dbi dbi;
	static MDB_val k;
	static MDB_cursor *cur;
	static MDB_cursor_op op = MDB_FIRST;
	static char dbname[2];
	int err = 0;

	config_db(db, dbname);
	switch (state) {
	case CONFIG_INIT:
		if ((err = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn)) != 0)
			DIE("%s()[%i]: %s", __func__, __LINE__, mdb_strerror(err));
		if((err = mdb_dbi_open(txn, dbname, MDB_DUPSORT, &dbi)) != 0) {
			ERROR("problem opening database '%s'", dbname);
			DIE("%s()[%i]: %s", __func__,  __LINE__,mdb_strerror(err));
		}
		if ((err = mdb_cursor_open(txn, dbi, &cur)) != 0)
			DIE("%s()[%i]: %s", __func__,  __LINE__,mdb_strerror(err));
		state = CONFIG_NEXT;
		break;
	case CONFIG_NEXT:
		op = MDB_NEXT;
		break;
	case CONFIG_FINAL:
		state = CONFIG_INIT;
		op = MDB_FIRST;
		mdb_cursor_close(cur);
		mdb_txn_abort(txn);
		return 0;
	}

	if (state != CONFIG_FINAL) {
		k.mv_size = strlen(key) + 1;
		k.mv_data = key;
		err = mdb_cursor_get(cur, &k, val, op);
	}

	if (err) {
		if (err == MDB_NOTFOUND) {
			state = CONFIG_FINAL;
			return state;
		}
		ERROR("%s(%i): %s", __func__, __LINE__, mdb_strerror(err));
	}

	return (err == 0) ? state : 0;
}

void config_init_db()
{
	if (env) return;
	mdb_env_create(&env);
	mdb_env_set_maxreaders(env, HANDLER_MAX + 1);
	mdb_env_set_mapsize(env, 10485760); /* TODO: how big a map do we need? */
	mdb_env_set_maxdbs(env, DB_MAX);
	mdb_env_open(env, DB_PATH, 0, 0600); /* TODO: set ownership on dropprivs */
}

int config_defaults(MDB_txn *txn, MDB_dbi dbi)
{
	int err = 0;
	char db[2];

	config_db(DB_GLOBAL, db);
	CONFIG_STRINGS(CONFIG_SET)
	CONFIG_INTEGERS(CONFIG_SET_INT)
	CONFIG_BOOLEANS(CONFIG_SET_INT)

	return err;
}

int config_dump(MDB_txn *txn, MDB_dbi dbi[])
{
	int err = 0;
	MDB_cursor *cur;
	MDB_cursor_op op;
	MDB_val key;
	MDB_val data;

	for (int i = 0; i < 80; i++) { putchar('#'); }
	puts("\n## globals");
	err = mdb_cursor_open(txn, dbi[DB_GLOBAL], &cur);
	if (err) goto config_dump_err;
	for (op = MDB_FIRST; (err = mdb_cursor_get(cur, &key, &data, op)) == 0; op = MDB_NEXT) {
		if (config_isint((char *)key.mv_data))
			printf("%s %i\n", (char *)key.mv_data, *(int *)data.mv_data);
		else if (config_isbool((char *)key.mv_data))
			printf("%s %s\n", (char *)key.mv_data, btos(*(int *)data.mv_data));
		else
			printf("%s %s\n", (char *)key.mv_data, (char *)data.mv_data);
	}
	mdb_cursor_close(cur);

	for (int i = 0; i < 80; i++) { putchar('#'); }
	puts("\n## protocols");
	err = mdb_cursor_open(txn, dbi[DB_PROTO], &cur);
	if (err) goto config_dump_err;
	for (op = MDB_FIRST; (err = mdb_cursor_get(cur, &key, &data, op)) == 0; op = MDB_NEXT) {
		if (!err) {
			proto_t *p;
			p = data.mv_data;
			printf("proto\t%s\t%u", p->module, p->port);
			switch (p->socktype) {
			case SOCK_STREAM:
				printf("/tcp");
				break;
			case SOCK_DGRAM:
				printf("/udp");
				break;
			case SOCK_RAW:
				printf("/raw");
				break;
			case SOCK_RDM:
				printf("/rdm");
				break;
			case SOCK_DCCP:
				printf("/dccp");
				break;
			}
			if (strcmp(p->addr, DEFAULT_LISTEN_ADDR))
				printf("\t%s", p->addr);
			putchar('\n');
		}
	}
	mdb_cursor_close(cur);

	for (int i = 0; i < 80; i++) { putchar('#'); }
	puts("\n## uris");
	err = mdb_cursor_open(txn, dbi[DB_URI], &cur);
	if (err) goto config_dump_err;
	for (op = MDB_FIRST; (err = mdb_cursor_get(cur, &key, &data, op)) == 0; op = MDB_NEXT) {
		if (!err) {
			uri_t *u = data.mv_data;
			printf("uri\t%s\n", u->uri);
		}
	}
	mdb_cursor_close(cur);

	return err;
config_dump_err:
	ERROR("%s(): %s", __func__, mdb_strerror(err));
	return LSD_ERROR_CONFIG_READ;
}

void config_drop(MDB_txn *txn, MDB_dbi dbi[])
{
	int err = 0;
	int flags = 0;
	char db[2];

	/* abort txn in case of previous writes */
	mdb_txn_abort(txn);
	mdb_txn_begin(env, NULL, 0, &txn);
	for (int i = 0; i <= DB_URI; i++) {
		flags = 0;
		if (i > 0) flags |= MDB_DUPSORT;
		config_db(i, db);;
		if ((err = mdb_dbi_open(txn, db, flags, &dbi[i]))
		|| ((err = mdb_drop(txn, dbi[i], 0)) != 0))
		{
			ERROR("%s(): %s", __func__, mdb_strerror(err));
		}
	}
}

int (config_cmds(int *argc, char **argv, MDB_txn *txn, MDB_dbi dbi[]))
{
	if (!(*argc)) return 0;
	/* commands must be last argument */
	char *last = argv[*argc - 1];
	if (!strcmp(last, "dump")) {
		DEBUG("dumping config");
		(*argc)--;
		config_dump(txn, dbi);
		return LSD_ERROR_CONFIG_ABORT;
	}
	else if (!strcmp(last, "reset")) {
		DEBUG("resetting database");
		config_drop(txn, dbi);
		config_defaults(txn, dbi[DB_GLOBAL]);
		return LSD_ERROR_CONFIG_COMMIT;
	}
	else if (!strcmp(last, "start")) {
		DEBUG("starting");
		(*argc)--;
		run = 1;
		return 0;
	}
	return 0;
}

int config_opt_set(char *k, char *v, MDB_txn *txn, MDB_dbi dbi)
{
	int err = 0;
	int ival = 0;
	char db[2];

	config_db(DB_GLOBAL, db);
	if (config_isstr(k)) {
		DEBUG("%s is str", k);
		if (!v) FAILMSG(LSD_ERROR_INVALID_OPTS, "%s missing value", k);
		err = config_set(db, k, v, txn, dbi);
	}
	else if (config_isint(k)) {
		DEBUG("%s is int", k);
		if (!v) FAILMSG(LSD_ERROR_INVALID_OPTS, "%s missing value", k);
		if (!isnumeric(v))
			FAILMSG(LSD_ERROR_INVALID_OPTS, "%s requires integer", k);
		ival = atoi(v);
		err = config_set_int(db, k, ival, txn, dbi);
	}
	else if (config_isbool(k)) {
		DEBUG("%s is bool", k);
		ival = 1; /* default true */
		if (v) {
			if (!config_bool_convert(v, &ival)) {
				FAILMSG(LSD_ERROR_INVALID_OPTS,
					"%s requires boolean", k);
			}
		}
		err = config_set_int(DB_GLOBAL, k, ival, txn, dbi);
	}
	return err;
}

int config_opts(int *argc, char **argv, MDB_txn *txn, MDB_dbi dbi)
{
	int err = 0;
	char *k, *v;

	for (int i = 1; i < *argc; i++) {
		if (!(strcmp(argv[i], "--debug"))) continue;
		v = argv[i+1];
		k = config_key(argv[i]);
		if (!k) {
			k = argv[i] + 2;
			if (strlen(argv[i]) > 4 && !strncmp(k, "no", 2 ) && config_isstr(k + 2)) {
				/* --no<option> */
				if ((err = config_del(DB_GLOBAL, k + 2, NULL, txn, dbi))
				&& (err != MDB_NOTFOUND))
				{
					break;
				}
				continue;
			}
			else {
				FAILMSG(LSD_ERROR_INVALID_OPTS, "Invalid option '%s'", argv[i]);
			}
		}
		if ((err = config_opt_set(k, v, txn, dbi))) break;
		i++;
	}

	return err;
}

int config_process_line(char *line, size_t len, MDB_txn *txn, MDB_dbi dbi[])
{
	int err = 0;
	char word[len];

	if (len == 0) return 0;			/* skip blank lines */
	while (isblank(*line)){line++;len--;}	/* strip leading whitespace */
	if (line[0] == '#') return 0;		/* ignore comments */

	/* grab first word */
	for (int i = 0; i < (int)len && !isspace(*line);) {
		word[i] = *(line++);
		word[++i] = '\0';
	}
	/* strip leading whitespace from remaining line */
	while (isblank(*line)){line++;len--;}

	if (config_isopt(word))
		err = config_opt_set(word, line, txn, dbi[DB_GLOBAL]);
	else if (!strcmp(word, "proto")) {
		err = config_process_proto(line, len, txn, dbi[DB_PROTO]);
	}
	/* TODO: process uris */
	else if (!strcmp(word, "uri")) {
		DEBUG("uri"); /* TODO */
	}
	else
		return LSD_ERROR_CONFIG_READ;

	return err;
}

int config_read(FILE *fd, MDB_txn *txn, MDB_dbi dbi[])
{
	int err = 0;
	int line = 1;
	int p = 0;
	size_t len = 0;
	char buf[LINE_MAX + 1];

	config_drop(txn, dbi);			/* drop old config */
	config_defaults(txn, dbi[DB_GLOBAL]);	/* reset defaults */
	while (fgets(buf + p, LINE_MAX, fd)) {
		len = strlen(buf) - 1;
		buf[len] = '\0'; /* chop newline */
		p = (buf[len - 1] == '\\') ? len: 0;
		if (p) continue; /* line continuation */
		if ((err = config_process_line(buf, len, txn, dbi)) != 0) break;
		line++;
	}
	if (err)
		ERROR("Error %i in config, line %i:\n%s", err, line, buf);

	return err;
}

int config_init(int argc, char **argv)
{
	int err = 0;
	int flags = 0;
	char *filename = NULL;
	char db[2];
	FILE *fd;
	MDB_txn *txn = NULL;
	MDB_dbi dbi[sizeof(config_db_idx_t)];
	MDB_val val;

	/* first, check if we're in debug mode */
	for (int i = 1; i < argc; i++) {
		if (!(strcmp(argv[i], "--debug"))) {
			loglevel = config_max("loglevel");
			DEBUG("Debugging mode enabled");
			break;
		}
	}

	/* initialize lmdb */
	config_init_db();

	/* wrap config write in single transaction */
	if ((err = mdb_txn_begin(env, NULL, 0, &txn)) != 0)
		FAILMSG(LSD_ERROR_CONFIG_WRITE, "config_init(): %s", mdb_strerror(err));

	/* try to open database, else create it */
	for (int i = 0; i <= DB_URI; i++) {
		flags = 0;
		if (i > 0) flags |= MDB_DUPSORT;
		config_db(i, db);
		while ((err = mdb_dbi_open(txn, db, flags, &dbi[i])) != 0) {
			if (err == MDB_NOTFOUND) {
				flags |= MDB_CREATE;
				DEBUG("creating db '%s'", db);
				continue;
			}
			ERROR("config_init(): %s", mdb_strerror(err));
			goto config_init_done;
		}
	}

	/* set defaults for new database */
	if (((flags & MDB_CREATE) == MDB_CREATE)
	&& ((err = config_defaults(txn, dbi[DB_GLOBAL])) != 0))
	{
		ERROR("Unable to set default config values");
		goto config_init_done;
	}

	/* process commands and options */
	if ((err = config_cmds(&argc, argv, txn, dbi))) goto config_init_done;
	if ((err = config_opts(&argc, argv, txn, dbi[DB_GLOBAL]))) goto config_init_done;

	/* process config file, if we have one */
	if (config_get("config", &val, txn, dbi[DB_GLOBAL]) == 0) {
		filename = (char *)val.mv_data;
		DEBUG("Loading config: '%s'", filename);
		if ((fd = fopen(filename, "r")) == NULL) FAIL(LSD_ERROR_CONFIG_READ);
		err = config_read(fd, txn, dbi);
		fclose(fd);
	}
	else if (!isatty(0)) { /* attempt to read config from stdin */
		DEBUG("Reading config from stdin");
		err = config_read(stdin, txn, dbi);
	}
	else {
		DEBUG("No config file");
	}
	if (!debug && !config_get("loglevel", &val, txn, dbi[DB_GLOBAL]))
		loglevel = *(int *)val.mv_data;
config_init_done:
	if (err && err != LSD_ERROR_CONFIG_COMMIT) {
		DEBUG("config not updated");
		mdb_txn_abort(txn);
		config_close();
	}
	else {
		DEBUG("config saved");
		mdb_txn_commit(txn);
		err = 0;
	}

	return err;
}
