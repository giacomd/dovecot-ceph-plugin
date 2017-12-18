/*
 * rados-ceph-Json-config.cpp
 *
 *  Created on: Nov 28, 2017
 *      Author: jan
 */

#include "rados-ceph-json-config.h"
#include <jansson.h>
#include <string>
#include <sstream>
namespace librmb {
RadosCephJsonConfig::RadosCephJsonConfig() {
  // set defaults.
  cfg_object_name = "rbox_cfg";

  key_generated_namespace = "generated_namespace";
  key_ns_cfg = "ns_cfg";
  key_ns_suffix = "ns_suffix";
  key_public_namespace = "public_namespace";

  generated_namespace = "false";
  ns_cfg = "rbox_ns_cfg";
  ns_suffix = "_namespace";
  public_namespace = "public";
  valid = false;
}

bool RadosCephJsonConfig::from_json(librados::bufferlist *buffer) {
  json_t *root;
  json_error_t error;
  bool ret = false;
  root = json_loads(buffer->to_str().c_str(), 0, &error);

  if (root) {
    json_t *ns = json_object_get(root, key_generated_namespace.c_str());
    generated_namespace = json_string_value(ns);

    json_t *ns_cfg_ = json_object_get(root, key_ns_cfg.c_str());
    ns_cfg = json_string_value(ns_cfg_);

    json_t *ns_suffix_ = json_object_get(root, key_ns_suffix.c_str());
    ns_suffix = json_string_value(ns_suffix_);

    json_t *public_namespace_ = json_object_get(root, key_public_namespace.c_str());
    public_namespace = json_string_value(public_namespace_);

    ret = valid = true;
    json_decref(root);
  }

  return ret;
}
bool RadosCephJsonConfig::to_json(librados::bufferlist *buffer) {
  char *s = NULL;
  json_t *root = json_object();

  json_object_set_new(root, key_generated_namespace.c_str(), json_string(generated_namespace.c_str()));
  json_object_set_new(root, key_ns_cfg.c_str(), json_string(ns_cfg.c_str()));
  json_object_set_new(root, key_ns_suffix.c_str(), json_string(ns_suffix.c_str()));
  json_object_set_new(root, key_public_namespace.c_str(), json_string(public_namespace.c_str()));
  s = json_dumps(root, 0);
  buffer->append(s);
  json_decref(root);
  free(s);
  return true;
}

std::string RadosCephJsonConfig::to_string() {
  std::ostringstream ss;
  ss << "Configuration : " << cfg_object_name << std::endl;
  ss << "  " << key_generated_namespace << "=" << generated_namespace << std::endl;
  ss << "  " << key_ns_cfg << "=" << ns_cfg << std::endl;
  ss << "  " << key_ns_suffix << "=" << ns_suffix << std::endl;
  ss << "  " << key_public_namespace << "=" << public_namespace << std::endl;
  return ss.str();
}
} /* namespace librmb */
