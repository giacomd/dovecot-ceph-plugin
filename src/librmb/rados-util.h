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

#ifndef SRC_LIBRMB_RADOS_UTIL_H_
#define SRC_LIBRMB_RADOS_UTIL_H_

#include <string.h>
#include <time.h>
#include <stdlib.h>

#include <string>
#include <map>
#include <rados/librados.hpp>
#include "rados-storage.h"
#include "rados-metadata-storage.h"
#include "rados-types.h"

namespace librmb {

class RadosUtils {
 public:
  RadosUtils();
  virtual ~RadosUtils();

  static bool convert_str_to_time_t(const std::string &date, time_t *val);
  static bool is_numeric(const std::string &s);
  static bool is_numeric_optional(std::string &text);
  static bool is_date_attribute(const rbox_metadata_key &key);

  static bool convert_string_to_date(const std::string &date_string, std::string *date);
  static int convert_time_t_to_str(const time_t &t, std::string *ret_val);
  static bool flags_to_string(const uint8_t &flags, std::string *flags_str);
  static bool string_to_flags(const std::string &flags_str, uint8_t *flags);

  static void find_and_replace(std::string *source, std::string const &find, std::string const &replace);

  static int get_all_keys_and_values(librados::IoCtx *io_ctx, const std::string &oid,
                                     std::map<std::string, librados::bufferlist> *kv_map);
  static void resolve_flags(const uint8_t &flags, std::string *flat);
  static int copy_to_alt(std::string &src_oid, std::string &dest_oid, RadosStorage *primary, RadosStorage *alt_storage,
                         RadosMetadataStorage *metadata, bool inverse);
  static int move_to_alt(std::string &oid, RadosStorage *primary, RadosStorage *alt_storage,
                         RadosMetadataStorage *metadata, bool inverse);
  static int osd_add(librados::IoCtx *ioctx, const std::string &oid, const std::string &key, long long value_to_add);
  static int osd_sub(librados::IoCtx *ioctx, const std::string &oid, const std::string &key,
                     long long value_to_subtract);

  static bool validate_metadata(
      std::map<std::string, ceph::bufferlist>* metadata);

  static std::string get_metadata(
      librmb::rbox_metadata_key key,
      std::map<std::string, ceph::bufferlist>* metadata);
  static std::string get_metadata(
      const string& key, std::map<std::string, ceph::bufferlist>* metadata);
  static bool is_numeric(std::string &text);
  };

}  // namespace librmb

#endif  // SRC_LIBRMB_RADOS_UTIL_H_
