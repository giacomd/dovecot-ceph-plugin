/* Copyright (c) 2007-2017 Dovecot authors, see the included COPYING file */

#include "lib.h"
#include "ioloop.h"
#include "istream.h"
#include "str.h"
#include "index-mail.h"
#include "dbox-mail.h"
#include "compat.h"
#include <sys/stat.h>

#include "debug-helper.h"
#include "rbox-file.h"
#include "rbox-storage.h"

struct mail *
rbox_mail_alloc(struct mailbox_transaction_context *t, enum mail_fetch_field wanted_fields,
		struct mailbox_header_lookup_ctx *wanted_headers) {

	FUNC_START();
	i_debug("rbox_mail_alloc: wanted_fields = %d", wanted_fields);

	struct dbox_mail *mail;
	pool_t pool;

	pool = pool_alloconly_create("mail", 2048);
	mail = p_new(pool, struct dbox_mail, 1);
	mail->imail.mail.pool = pool;

	index_mail_init(&mail->imail, t, wanted_fields, wanted_headers);

	debug_print_mail(&mail->imail.mail.mail, "rbox_mail_alloc", NULL);
	FUNC_END();

	return &mail->imail.mail.mail;
}

static void rbox_mail_close(struct mail *_mail) {
	struct dbox_mail *mail = (struct dbox_mail *) _mail;

	index_mail_close(_mail);
	/* close the rbox file only after index is closed, since it may still
	 try to read from it. */
	if (mail->open_file != NULL)
		dbox_file_unref(&mail->open_file);
}

static void rbox_mail_set_expunged(struct dbox_mail *mail) {
	struct mail *_mail = &mail->imail.mail.mail;

	mail_index_refresh(_mail->box->index);
	if (mail_index_is_expunged(_mail->transaction->view, _mail->seq)) {
		mail_set_expunged(_mail);
		return;
	}

	mail_storage_set_critical(_mail->box->storage, "rbox %s: Unexpectedly lost uid=%u", mailbox_get_path(_mail->box), _mail->uid);
	rbox_set_mailbox_corrupted(_mail->box);
}

static int rbox_mail_file_set(struct dbox_mail *mail) {
	struct mail *_mail = &mail->imail.mail.mail;
	struct rbox_mailbox *mbox = (struct rbox_mailbox *) _mail->box;
	bool deleted;
	int ret;

	if (mail->open_file != NULL) {
		/* already set */
		return 0;
	} else if (!_mail->saving) {
		mail->open_file = rbox_file_init(mbox, _mail->uid);
		return 0;
	} else {
		/* mail is being saved in this transaction */
		mail->open_file = rbox_save_file_get_file(_mail->transaction, _mail->seq);
		mail->open_file->refcount++;

		/* it doesn't have input stream yet */
		ret = dbox_file_open(mail->open_file, &deleted);
		if (ret <= 0) {
			mail_storage_set_critical(_mail->box->storage, "rbox %s: Unexpectedly lost mail being saved",
					mailbox_get_path(_mail->box));
			rbox_set_mailbox_corrupted(_mail->box);
			return -1;
		}
		return 1;
	}
}

static int rbox_mail_get_special(struct mail *_mail, enum mail_fetch_field field, const char **value_r) {

	FUNC_START();

	struct rbox_mailbox *mbox = (struct rbox_mailbox *) _mail->box;
	struct dbox_mail *mail = (struct dbox_mail *) _mail;
	struct stat st;

	i_debug("rbox_mail_get_special: field = %d", field);

	switch (field) {
	case MAIL_FETCH_REFCOUNT:
		if (rbox_mail_file_set(mail) < 0)
			return -1;

		_mail->transaction->stats.fstat_lookup_count++;
		if (dbox_file_stat(mail->open_file, &st) < 0) {
			if (errno == ENOENT)
				mail_set_expunged(_mail);
			return -1;
		}
		*value_r = p_strdup_printf(mail->imail.mail.data_pool, "%lu", (unsigned long) st.st_nlink);
		return 0;
	case MAIL_FETCH_UIDL_BACKEND:
		if (!dbox_header_have_flag(&mbox->box, mbox->hdr_ext_id, offsetof(struct rbox_index_header, flags),
				DBOX_INDEX_HEADER_FLAG_HAVE_POP3_UIDLS)) {
			*value_r = "";
			return 0;
		}
		break;
	case MAIL_FETCH_POP3_ORDER:
		if (!dbox_header_have_flag(&mbox->box, mbox->hdr_ext_id, offsetof(struct rbox_index_header, flags),
				DBOX_INDEX_HEADER_FLAG_HAVE_POP3_ORDERS)) {
			*value_r = "";
			return 0;
		}
		break;
	default:
		break;
	}

	debug_print_mail(_mail, "rbox_mail_get_special", NULL);
	FUNC_END();

	return dbox_mail_get_special(_mail, field, value_r);
}

int rbox_mail_open(struct dbox_mail *mail, uoff_t *offset_r, struct dbox_file **file_r) {
	struct mail *_mail = &mail->imail.mail.mail;
	bool deleted;
	int ret;

	if (_mail->lookup_abort != MAIL_LOOKUP_ABORT_NEVER) {
		mail_set_aborted(_mail);
		return -1;
	}
	_mail->mail_stream_opened = TRUE;

	ret = rbox_mail_file_set(mail);
	if (ret < 0)
		return -1;
	if (ret == 0) {
		if (!dbox_file_is_open(mail->open_file))
			_mail->transaction->stats.open_lookup_count++;
		if (dbox_file_open(mail->open_file, &deleted) <= 0)
			return -1;
		if (deleted) {
			rbox_mail_set_expunged(mail);
			return -1;
		}
	}

	*file_r = mail->open_file;
	*offset_r = 0;
	return 0;
}

static int rbox_mail_metadata_read(struct dbox_mail *mail, struct dbox_file **file_r) {
	struct rbox_storage *storage = (struct rbox_storage *) mail->imail.mail.mail.box->storage;
	uoff_t offset;

	if (storage->storage.v.mail_open(mail, &offset, file_r) < 0)
		return -1;

	if (dbox_file_seek(*file_r, offset) <= 0)
		return -1;
	if (dbox_file_metadata_read(*file_r) <= 0)
		return -1;

	if (mail->imail.data.stream != NULL) {
		/* we just messed up mail's input stream by reading metadata */
		i_stream_seek((*file_r)->input, offset);
		i_stream_sync(mail->imail.data.stream);
	}
	return 0;
}

static int rbox_mail_metadata_get(struct dbox_mail *mail, enum dbox_metadata_key key, const char **value_r) {
	struct dbox_file *file;

	if (rbox_mail_metadata_read(mail, &file) < 0)
		return -1;

	*value_r = dbox_file_metadata_get(file, key);
	return 0;
}

static int rbox_mail_get_physical_size(struct mail *_mail, uoff_t *size_r) {
	struct dbox_mail *mail = (struct dbox_mail *) _mail;
	struct index_mail_data *data = &mail->imail.data;
	struct dbox_file *file;

	if (index_mail_get_physical_size(_mail, size_r) == 0)
		return 0;

	if (dbox_mail_metadata_read(mail, &file) < 0)
		return -1;

	data->physical_size = dbox_file_get_plaintext_size(file);
	*size_r = data->physical_size;
	return 0;
}

static int rbox_mail_get_virtual_size(struct mail *_mail, uoff_t *size_r) {
	struct dbox_mail *mail = (struct dbox_mail *) _mail;
	struct index_mail_data *data = &mail->imail.data;
	const char *value;
	uintmax_t size;

	if (index_mail_get_cached_virtual_size(&mail->imail, size_r))
		return 0;

	if (rbox_mail_metadata_get(mail, DBOX_METADATA_VIRTUAL_SIZE, &value) < 0)
		return -1;
	if (value == NULL)
		return index_mail_get_virtual_size(_mail, size_r);

	if (str_to_uintmax_hex(value, &size) < 0 || size > (uoff_t) - 1)
		return -1;
	data->virtual_size = (uoff_t) size;
	*size_r = data->virtual_size;
	return 0;
}

static int rbox_mail_get_received_date(struct mail *_mail, time_t *date_r) {
	struct dbox_mail *mail = (struct dbox_mail *) _mail;
	struct index_mail_data *data = &mail->imail.data;
	const char *value;
	uintmax_t time;

	if (index_mail_get_received_date(_mail, date_r) == 0)
		return 0;

	if (rbox_mail_metadata_get(mail, DBOX_METADATA_RECEIVED_TIME, &value) < 0)
		return -1;

	time = 0;
	if (value != NULL && str_to_uintmax_hex(value, &time) < 0)
		return -1;

	data->received_date = (time_t) time;
	*date_r = data->received_date;
	return 0;
}

static int rbox_mail_get_save_date(struct mail *_mail, time_t *date_r) {
	struct rbox_storage *storage = (struct rbox_storage *) _mail->box->storage;
	struct dbox_mail *mail = (struct dbox_mail *) _mail;
	struct index_mail_data *data = &mail->imail.data;
	struct dbox_file *file;
	struct stat st;
	uoff_t offset;

	if (index_mail_get_save_date(_mail, date_r) == 0)
		return 0;

	if (storage->storage.v.mail_open(mail, &offset, &file) < 0)
		return -1;

	_mail->transaction->stats.fstat_lookup_count++;
	if (dbox_file_stat(file, &st) < 0) {
		if (errno == ENOENT)
			mail_set_expunged(_mail);
		return -1;
	}
	*date_r = data->save_date = st.st_ctime;
	return 0;
}

static int get_mail_stream(struct dbox_mail *mail, uoff_t offset, struct istream **stream_r) {
	struct mail_private *pmail = &mail->imail.mail;
	struct dbox_file *file = mail->open_file;
	int ret;

	if ((ret = dbox_file_seek(file, offset)) <= 0) {
		*stream_r = NULL;
		return ret;
	}

	*stream_r = i_stream_create_limit(file->input, file->cur_physical_size);
	if (pmail->v.istream_opened != NULL) {
		if (pmail->v.istream_opened(&pmail->mail, stream_r) < 0)
			return -1;
	}
	if (file->storage->attachment_dir == NULL)
		return 1;
	else
		return dbox_attachment_file_get_stream(file, stream_r);
}

static int rbox_mail_get_stream(struct mail *_mail, bool get_body ATTR_UNUSED, struct message_size *hdr_size,
		struct message_size *body_size, struct istream **stream_r) {
	struct rbox_storage *storage = (struct rbox_storage *) _mail->box->storage;
	struct dbox_mail *mail = (struct dbox_mail *) _mail;
	struct index_mail_data *data = &mail->imail.data;
	struct istream *input;
	uoff_t offset;
	int ret;

	if (data->stream == NULL) {
		if (storage->storage.v.mail_open(mail, &offset, &mail->open_file) < 0)
			return -1;

		ret = get_mail_stream(mail, offset, &input);
		if (ret <= 0) {
			if (ret < 0)
				return -1;
			dbox_file_set_corrupted(mail->open_file,
					"uid=%u points to broken data at offset="
					"%"PRIuUOFF_T, _mail->uid, offset);
			if (input != NULL)
				i_stream_unref(&input);
			return -1;
		}
		data->stream = input;
		index_mail_set_read_buffer_size(_mail, input);
	}

	return index_mail_init_stream(&mail->imail, hdr_size, body_size, stream_r);
}

struct mail_vfuncs rbox_mail_vfuncs = {
		rbox_mail_close,
		index_mail_free,
		index_mail_set_seq,
		index_mail_set_uid,
		index_mail_set_uid_cache_updates,
		index_mail_prefetch,
		index_mail_precache,
		index_mail_add_temp_wanted_fields,

		index_mail_get_flags,
		index_mail_get_keywords,
		index_mail_get_keyword_indexes,
		index_mail_get_modseq,
		index_mail_get_pvt_modseq,
		index_mail_get_parts,
		index_mail_get_date,
		rbox_mail_get_received_date,
		rbox_mail_get_save_date,
		rbox_mail_get_virtual_size,
		rbox_mail_get_physical_size,
		index_mail_get_first_header,
		index_mail_get_headers,
		index_mail_get_header_stream,
		rbox_mail_get_stream,
		index_mail_get_binary_stream,
		rbox_mail_get_special,
		index_mail_get_real_mail,
		index_mail_update_flags,
		index_mail_update_keywords,
		index_mail_update_modseq,
		index_mail_update_pvt_modseq,
		NULL,
		index_mail_expunge,
		index_mail_set_cache_corrupted,
		index_mail_opened,
		index_mail_set_cache_corrupted_reason };