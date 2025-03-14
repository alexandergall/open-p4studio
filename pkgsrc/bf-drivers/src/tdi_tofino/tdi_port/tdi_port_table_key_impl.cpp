/*******************************************************************************
 *  Copyright (C) 2024 Intel Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions
 *  and limitations under the License.
 *
 *
 *  SPDX-License-Identifier: Apache-2.0
 ******************************************************************************/


#include <bitset>
#include <arpa/inet.h>
#include <inttypes.h>

#include "tdi_port_table_key_impl.hpp"
#include <tdi/common/tdi_utils.hpp>

namespace tdi {

namespace {

inline tdi_status_t getKeyFieldSafe(
    const tdi_id_t &field_id,
    const KeyFieldInfo **key_field,
    const tdi_match_type_core_e &key_field_type_req,
    const Table *table) {
  auto tableInfo = table->tableInfoGet();
  *key_field = tableInfo->keyFieldGet(field_id);

  if (!key_field) {
    return TDI_INVALID_ARG;
  }

  // If the key_field type is not what is required here, then just return
  // error
  // wdai: temp fix for compile
  if ((*key_field)->matchTypeGet() != (tdi_match_type_e &)key_field_type_req) {
    LOG_ERROR(
        "%s:%d %s Wrong API called for this field type,"
        " field_id %d expected type: %d received type: %d",
        __func__,
        __LINE__,
        tableInfo->nameGet().c_str(),
        field_id,
        static_cast<int>((*key_field)->matchTypeGet()),
        static_cast<int>(key_field_type_req));
    return TDI_INVALID_ARG;
  }
  return TDI_SUCCESS;
}

template <typename Key>
tdi_status_t setValueHelper(Key *key,
                            const tdi_id_t &field_id,
                            const tdi::KeyFieldValue &field_value) {
  const tdi::Table *table;
  auto status = key->tableGet(&table);
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  status = utils::TableFieldUtils::keyFieldSafeGet(
      field_id, &key_field, &field_value, table);
  if (status != TDI_SUCCESS) {
    return status;
  }

  if (field_value.matchTypeGet() !=
      static_cast<tdi_match_type_e>(TDI_MATCH_TYPE_EXACT)) {
    return TDI_INVALID_ARG;
  }

  if (field_value.is_pointer()) {
    auto e_fv = static_cast<const tdi::KeyFieldValueExact<const uint8_t *> *>(
        &field_value);
    return key->setValue(key_field, e_fv->value_, e_fv->size_);
  } else {
    auto e_fv = static_cast<const tdi::KeyFieldValueExact<const uint64_t> *>(
        &field_value);
    return key->setValue(key_field, e_fv->value_);
  }

  return TDI_UNEXPECTED;
}

template <typename Key>
tdi_status_t getValueHelper(const Key *key,
                            const tdi_id_t &field_id,
                            tdi::KeyFieldValue *field_value) {
  const tdi::Table *table;
  auto status = key->tableGet(&table);

  if (!field_value) {
    LOG_ERROR("%s:%d %s input param passed null for key field_id %d, ",
              __func__,
              __LINE__,
              table->tableInfoGet()->nameGet().c_str(),
              field_id);
    return TDI_OBJECT_NOT_FOUND;
  }

  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  status = utils::TableFieldUtils::keyFieldSafeGet(
      field_id, &key_field, field_value, table);
  if (status != TDI_SUCCESS) {
    return status;
  }

  if (field_value->matchTypeGet() !=
      static_cast<tdi_match_type_e>(TDI_MATCH_TYPE_EXACT)) {
    return TDI_INVALID_ARG;
  }

  if (field_value->is_pointer()) {
    auto e_fv = static_cast<tdi::KeyFieldValueExact<uint8_t *> *>(field_value);
    return key->getValue(key_field, e_fv->size_, e_fv->value_);
  } else {
    auto e_fv = static_cast<tdi::KeyFieldValueExact<uint64_t> *>(field_value);
    return key->getValue(key_field, &e_fv->value_);
  }

  return TDI_UNEXPECTED;
}

template <class T>
tdi_status_t getKeyIdxValue(const T &key,
                            const tdi::Table &table,
                            const tdi_id_t &field_id,
                            uint64_t *value
                            /*tdi::KeyFieldValue *field_value*/) {
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  auto status =
      getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, &table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  *value = key.getId();
  return TDI_SUCCESS;
}

template <class T>
tdi_status_t getKeyIdxValue(const T &key,
                            const tdi::Table &table,
                            const tdi_id_t &field_id,
                            uint8_t *value,
                            const size_t &size) {
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  auto status =
      getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, &table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  if (size != sizeof(uint32_t)) {
    LOG_ERROR(
        "%s:%d %s ERROR Array size of %zd is not equal to the field size %zd "
        "for field id %d",
        __func__,
        __LINE__,
        table.tableInfoGet()->nameGet().c_str(),
        size,
        sizeof(uint32_t),
        field_id);
    return TDI_INVALID_ARG;
  }
  uint32_t local_val = htobe32(key.getId());
  std::memcpy(value, &local_val, sizeof(uint32_t));
  return TDI_SUCCESS;
}
}  // anonymous namespace

tdi_status_t PortCfgTableKey::reset() {
  dev_port_ = 0;
  return TDI_SUCCESS;
}

tdi_status_t PortCfgTableKey::setValue(const tdi_id_t &field_id,
                                       const tdi::KeyFieldValue &field_value) {
  // Get the key_field from the table
  auto port_value =
      reinterpret_cast<const tdi::KeyFieldValueExact<const uint64_t> &>(
          field_value);

  const KeyFieldInfo *key_field;
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  status = getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  dev_port_ = port_value.value_;
  return TDI_SUCCESS;
}

tdi_status_t PortCfgTableKey::setValue(const tdi_id_t &field_id,
                                       const uint8_t *value,
                                       const size_t &size) {
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  status = getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  if (size != sizeof(uint32_t)) {
    LOG_ERROR(
        "%s:%d %s ERROR Array size of %zd is not equal to the field size %zd "
        "for field id %d",
        __func__,
        __LINE__,
        table->tableInfoGet()->nameGet().c_str(),
        size,
        sizeof(uint32_t),
        field_id);
    return TDI_INVALID_ARG;
  }
  uint32_t val = *(reinterpret_cast<const uint32_t *>(value));
  val = be32toh(val);
  dev_port_ = val;
  return TDI_SUCCESS;
}

tdi_status_t PortCfgTableKey::getValue(const tdi_id_t &field_id,
                                       tdi::KeyFieldValue *field_value) const {
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  uint64_t value = 0;
  status = getKeyIdxValue<PortCfgTableKey>(*this, *table, field_id, &value);
  auto keyFieldValue =
      reinterpret_cast<tdi::KeyFieldValueExact<uint64_t> *>(field_value);
  keyFieldValue->value_ = value;

  if (status != TDI_SUCCESS) {
    return status;
  }
  return status;
}

tdi_status_t PortCfgTableKey::getValue(const tdi_id_t &field_id,
                                       const size_t &size,
                                       uint8_t *value) const {
  return getKeyIdxValue<PortCfgTableKey>(*this, *table_, field_id, value, size);
}

// PortStatTableKey
tdi_status_t PortStatTableKey::reset() {
  dev_port_ = 0;
  return TDI_SUCCESS;
}

tdi_status_t PortStatTableKey::setValue(const tdi_id_t &field_id,
                                        const tdi::KeyFieldValue &field_value) {
  auto port_value =
      reinterpret_cast<const tdi::KeyFieldValueExact<const uint64_t> &>(
          field_value);
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  status = getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  dev_port_ = port_value.value_;
  return TDI_SUCCESS;
}

/*tdi_status_t BfRtPortStatTableKey::setValue(const tdi_id_t &field_id,
                                           const uint64_t &value) {
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  auto status = utils::BfRtTableFieldUtils::getKeyFieldSafe(
      field_id, &key_field, KeyFieldType::EXACT, table_);
  if (status != TDI_SUCCESS) {
    return status;
  }
  dev_port_ = value;
  return TDI_SUCCESS;
}*/

tdi_status_t PortStatTableKey::setValue(const tdi_id_t &field_id,
                                        const uint8_t *value,
                                        const size_t &size) {
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  status = getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  if (size != sizeof(uint32_t)) {
    LOG_ERROR(
        "%s:%d %s ERROR Array size of %zd is not equal to the field size %zd "
        "for field id %d",
        __func__,
        __LINE__,
        table->tableInfoGet()->nameGet().c_str(),
        size,
        sizeof(uint32_t),
        field_id);
    return TDI_INVALID_ARG;
  }
  uint32_t val = *(reinterpret_cast<const uint32_t *>(value));
  val = be32toh(val);
  dev_port_ = val;
  return TDI_SUCCESS;
}

tdi_status_t PortStatTableKey::getValue(const tdi_id_t &field_id,
                                        KeyFieldValue *field_value) const {
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  uint64_t value = 0;
  status = getKeyIdxValue<PortStatTableKey>(*this, *table, field_id, &value);
  auto keyFieldValue =
      reinterpret_cast<tdi::KeyFieldValueExact<uint64_t> *>(field_value);
  keyFieldValue->value_ = value;

  if (status != TDI_SUCCESS) {
    return status;
  }
  return status;
}

tdi_status_t PortStatTableKey::getValue(const tdi_id_t &field_id,
                                        uint64_t *value) const {
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  return getKeyIdxValue<PortStatTableKey>(*this, *table, field_id, value);
}

tdi_status_t PortStatTableKey::getValue(const tdi_id_t &field_id,
                                        const size_t &size,
                                        uint8_t *value) const {
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  return getKeyIdxValue<PortStatTableKey>(*this, *table, field_id, value, size);
}

tdi_status_t PortStrInfoTableKey::setValue(
    const tdi_id_t &field_id, const tdi::KeyFieldValue &field_value) {
  auto port_str =
      reinterpret_cast<const tdi::KeyFieldValueExact<const char *> &>(
          field_value);
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  status = getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, table);
  if (status != TDI_SUCCESS) {
    return status;
  }
  port_str_ = std::string(port_str.value_);
  return TDI_SUCCESS;
}

tdi_status_t PortStrInfoTableKey::getValue(const tdi_id_t &field_id,
                                           KeyFieldValue *field_value) const {
  // Get the key_field from the table
  const KeyFieldInfo *key_field;
  const Table *table = nullptr;
  auto status = this->tableGet(&table);
  if (status != TDI_SUCCESS) {
    return status;
  }

  status = getKeyFieldSafe(field_id, &key_field, TDI_MATCH_TYPE_EXACT, table);
  if (status != TDI_SUCCESS) {
    return status;
  }

  auto keyFieldValue =
      reinterpret_cast<tdi::KeyFieldValueExact<std::string> *>(field_value);
  keyFieldValue->value_ = port_str_;
  return status;
}

tdi_status_t PortHdlInfoTableKey::reset() {
  conn_id_ = 0;
  chnl_id_ = 0;
  return TDI_SUCCESS;
}

tdi_status_t PortHdlInfoTableKey::setValue(
    const tdi_id_t &field_id, const tdi::KeyFieldValue &field_value) {
  return setValueHelper<PortHdlInfoTableKey>(this, field_id, field_value);
}

tdi_status_t PortHdlInfoTableKey::setValue(const tdi::KeyFieldInfo *key_field,
                                           const uint64_t &value) {
  if (key_field->nameGet() == "conn_id") {
    conn_id_ = value;
  } else if (key_field->nameGet() == "chnl_id") {
    chnl_id_ = value;
  }
  return TDI_SUCCESS;
}

tdi_status_t PortHdlInfoTableKey::setValue(const tdi::KeyFieldInfo *key_field,
                                           const uint8_t *value,
                                           const size_t &size) {
  if (size != sizeof(uint32_t)) {
    LOG_ERROR(
        "%s:%d %s ERROR Array size of %zd is not equal to the field size %zd "
        "for field id %d",
        __func__,
        __LINE__,
        table_->tableInfoGet()->nameGet().c_str(),
        size,
        sizeof(uint32_t),
        key_field->idGet());
    return TDI_INVALID_ARG;
  }
  uint32_t val = *(reinterpret_cast<const uint32_t *>(value));
  val = be32toh(val);
  if (key_field->nameGet() == "conn_id") {
    conn_id_ = val;
  } else if (key_field->nameGet() == "chnl_id") {
    chnl_id_ = val;
  }
  return TDI_SUCCESS;
}

tdi_status_t PortHdlInfoTableKey::getValue(
    const tdi_id_t &field_id, tdi::KeyFieldValue *field_value) const {
  return getValueHelper<PortHdlInfoTableKey>(this, field_id, field_value);
}

tdi_status_t PortHdlInfoTableKey::getValue(const tdi::KeyFieldInfo *key_field,
                                           uint64_t *value) const {
  if (key_field->nameGet() == "conn_id") {
    *value = conn_id_;
  } else if (key_field->nameGet() == "chnl_id") {
    *value = chnl_id_;
  }
  return TDI_SUCCESS;
}

tdi_status_t PortHdlInfoTableKey::getValue(const tdi::KeyFieldInfo *key_field,
                                           const size_t &size,
                                           uint8_t *value) const {
  if (size != sizeof(uint32_t)) {
    LOG_ERROR(
        "%s:%d %s ERROR Array size of %zd is not equal to the field size %zd "
        "for field id %d",
        __func__,
        __LINE__,
        table_->tableInfoGet()->nameGet().c_str(),
        size,
        sizeof(uint32_t),
        key_field->idGet());
    return TDI_INVALID_ARG;
  }

  uint32_t local_val = 0;
  if (key_field->nameGet() == "conn_id") {
    local_val = conn_id_;
  } else if (key_field->nameGet() == "chnl_id") {
    local_val = chnl_id_;
  }
  local_val = htobe32(local_val);
  std::memcpy(value, &local_val, sizeof(uint32_t));
  return TDI_SUCCESS;
}

tdi_status_t PortHdlInfoTableKey::getPortHdl(uint32_t *conn_id,
                                             uint32_t *chnl_id) const {
  if ((conn_id != NULL) && (chnl_id != NULL)) {
    *conn_id = conn_id_;
    *chnl_id = chnl_id_;
    return TDI_SUCCESS;
  }
  return TDI_INVALID_ARG;
}

tdi_status_t PortFpIdxInfoTableKey::reset() {
  fp_idx_ = 0;
  return TDI_SUCCESS;
}

tdi_status_t PortFpIdxInfoTableKey::setValue(
    const tdi_id_t &field_id, const tdi::KeyFieldValue &field_value) {
  return setValueHelper<PortFpIdxInfoTableKey>(this, field_id, field_value);
}

tdi_status_t PortFpIdxInfoTableKey::setValue(
    const tdi::KeyFieldInfo * /*key_field*/, const uint64_t &value) {
  fp_idx_ = value;
  return TDI_SUCCESS;
}

tdi_status_t PortFpIdxInfoTableKey::setValue(const tdi::KeyFieldInfo *key_field,
                                             const uint8_t *value,
                                             const size_t &size) {
  if (size != sizeof(uint32_t)) {
    LOG_ERROR(
        "%s:%d %s ERROR Array size of %zd is not equal to the field size %zd "
        "for field id %d",
        __func__,
        __LINE__,
        table_->tableInfoGet()->nameGet().c_str(),
        size,
        sizeof(uint32_t),
        key_field->idGet());
    return TDI_INVALID_ARG;
  }
  uint32_t val = *(reinterpret_cast<const uint32_t *>(value));
  val = be32toh(val);
  fp_idx_ = val;
  return TDI_SUCCESS;
}

tdi_status_t PortFpIdxInfoTableKey::getValue(
    const tdi_id_t &field_id, tdi::KeyFieldValue *field_value) const {
  return getValueHelper<PortFpIdxInfoTableKey>(this, field_id, field_value);
}

tdi_status_t PortFpIdxInfoTableKey::getValue(const tdi::KeyFieldInfo *key_field,
                                             uint64_t *value) const {
  return getKeyIdxValue<PortFpIdxInfoTableKey>(
      *this, *table_, key_field->idGet(), value);
}

tdi_status_t PortFpIdxInfoTableKey::getValue(const tdi::KeyFieldInfo *key_field,
                                             const size_t &size,
                                             uint8_t *value) const {
  return getKeyIdxValue<PortFpIdxInfoTableKey>(
      *this, *table_, key_field->idGet(), value, size);
}

}  // namespace tdi
