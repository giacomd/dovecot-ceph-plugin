// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Copyright (c) 2017-2018 Tallence AG and the authors
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 */

#ifndef SRC_STORAGE_RBOX_RBOX_SYNC_H_
#define SRC_STORAGE_RBOX_RBOX_SYNC_H_

#include "dovecot-all.h"

enum rbox_sync_flags { RBOX_SYNC_FLAG_FORCE = 0x01, RBOX_SYNC_FLAG_FSYNC = 0x02, RBOX_SYNC_FLAG_FORCE_REBUILD = 0x04 };

struct expunged_item {
  uint32_t uid;
  guid_128_t oid;
  bool alt_storage;
};

struct rbox_sync_context {
  struct rbox_mailbox *mbox;
  struct mail_index_sync_ctx *index_sync_ctx;
  struct mail_index_view *sync_view;
  struct mail_index_transaction *trans;

  string_t *path;
  size_t path_dir_prefix_len;
  uint32_t uid_validity;
  ARRAY(struct expunged_item *) expunged_items;
};

struct expunge_callback_data {
  struct mailbox *box;
  struct expunged_item *item;
};

int rbox_sync(struct rbox_mailbox *mbox, enum rbox_sync_flags flags);

int rbox_sync_begin(struct rbox_mailbox *mbox, struct rbox_sync_context **ctx_r, enum rbox_sync_flags flags);
int rbox_sync_finish(struct rbox_sync_context **ctx, bool success);

#endif  // SRC_STORAGE_RBOX_RBOX_SYNC_H_
