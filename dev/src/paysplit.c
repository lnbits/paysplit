#include "lnbits_host.h"
#include "paysplit.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SOURCE_TABLE "sources"
#define TARGET_TABLE "targets"
#define SPLIT_TABLE "splits"
#define EXT_ID "paysplit"

typedef struct {
  char *id;
  char *wallet_id;
  char *wallet_name;
  int64_t max_amount;
  bool enabled;
  uint64_t created_at;
  uint64_t updated_at;
} Source;

typedef struct {
  char *id;
  char *source_wallet_id;
  char *alias;
  char *lnurl;
  double percent;
  uint64_t created_at;
  uint64_t updated_at;
} Target;

typedef struct {
  Target *items;
  size_t len;
} TargetList;

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} Buffer;

static char *xstrdup(const char *value) {
  if (!value) {
    value = "";
  }
  size_t len = strlen(value);
  char *copy = malloc(len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, value, len + 1);
  return copy;
}

static char *paysplit_string_to_c(const paysplit_string_t *value) {
  char *copy = malloc(value->len + 1);
  if (!copy) {
    return xstrdup("");
  }
  memcpy(copy, value->ptr, value->len);
  copy[value->len] = '\0';
  return copy;
}

static char *copy_paysplit_string(const paysplit_string_t *value) {
  char *copy = paysplit_string_to_c(value);
  return copy ? copy : xstrdup("");
}

static char *copy_optional_paysplit_string(paysplit_option_string_t *value) {
  return value->is_some ? copy_paysplit_string(&value->val) : xstrdup("");
}

static void paysplit_return_string(paysplit_string_t *ret, char *value) {
  if (!value) {
    value = xstrdup("");
  }
  ret->ptr = (uint8_t *)value;
  ret->len = strlen(value);
}

static void buffer_init(Buffer *buffer) {
  buffer->cap = 1024;
  buffer->len = 0;
  buffer->data = malloc(buffer->cap);
  if (buffer->data) {
    buffer->data[0] = '\0';
  }
}

static void buffer_grow(Buffer *buffer, size_t needed) {
  if (needed <= buffer->cap) {
    return;
  }
  while (buffer->cap < needed) {
    buffer->cap *= 2;
  }
  buffer->data = realloc(buffer->data, buffer->cap);
}

static void buffer_append(Buffer *buffer, const char *format, ...) {
  if (!buffer->data) {
    return;
  }
  while (true) {
    va_list args;
    va_start(args, format);
    int written = vsnprintf(
      buffer->data + buffer->len, buffer->cap - buffer->len, format, args);
    va_end(args);
    if (written < 0) {
      return;
    }
    size_t next_len = buffer->len + (size_t)written;
    if (next_len < buffer->cap) {
      buffer->len = next_len;
      return;
    }
    buffer_grow(buffer, next_len + 1);
  }
}

static char *json_escape(const char *value) {
  Buffer buffer;
  buffer_init(&buffer);
  for (const char *cursor = value ? value : ""; *cursor; cursor++) {
    unsigned char ch = (unsigned char)*cursor;
    switch (ch) {
      case '\\':
        buffer_append(&buffer, "\\\\");
        break;
      case '"':
        buffer_append(&buffer, "\\\"");
        break;
      case '\n':
        buffer_append(&buffer, "\\n");
        break;
      case '\r':
        buffer_append(&buffer, "\\r");
        break;
      case '\t':
        buffer_append(&buffer, "\\t");
        break;
      default:
        if (ch < 0x20) {
          buffer_append(&buffer, "\\u%04x", ch);
        } else {
          buffer_append(&buffer, "%c", ch);
        }
    }
  }
  return buffer.data;
}

char *lnbits_storage_get(const char *table, const char *id) {
  lnbits_extension_host_storage_get_request_t request = {0};
  lnbits_extension_host_storage_get_response_t response = {0};
  paysplit_string_dup(&request.table, table);
  paysplit_string_dup(&request.id, id);
  lnbits_extension_host_storage_get(&request, &response);
  lnbits_extension_host_storage_get_request_free(&request);
  if (!response.data_json.is_some) {
    return NULL;
  }
  char *data = copy_paysplit_string(&response.data_json.val);
  lnbits_extension_host_storage_get_response_free(&response);
  return data;
}

bool lnbits_storage_set(const char *table, const char *data_json) {
  lnbits_extension_host_storage_set_request_t request = {0};
  lnbits_extension_host_storage_set_response_t response = {0};
  paysplit_string_dup(&request.table, table);
  paysplit_string_dup(&request.data_json, data_json);
  lnbits_extension_host_storage_set(&request, &response);
  lnbits_extension_host_storage_set_request_free(&request);
  return response.ok;
}

char *lnbits_storage_get_paginated(
  const char *table,
  const char *filters_json,
  const char *search,
  const char *search_fields_json,
  const char *sort_by,
  bool descending,
  uint32_t limit,
  uint32_t offset
) {
  (void)search_fields_json;
  lnbits_extension_host_storage_paginated_request_t request = {0};
  lnbits_extension_host_storage_paginated_response_t response = {0};
  paysplit_string_dup(&request.table, table);
  paysplit_string_dup(&request.filters_json, filters_json);
  paysplit_string_dup(&request.search, search);
  request.search_fields.ptr = NULL;
  request.search_fields.len = 0;
  paysplit_string_dup(&request.sort_by, sort_by);
  request.descending = descending;
  request.limit = limit;
  request.offset = offset;
  lnbits_extension_host_storage_get_paginated(&request, &response);
  lnbits_extension_host_storage_paginated_request_free(&request);
  char *rows = copy_paysplit_string(&response.rows_json);
  lnbits_extension_host_storage_paginated_response_free(&response);
  return rows;
}

bool lnbits_storage_delete(const char *table, const char *id) {
  lnbits_extension_host_storage_delete_request_t request = {0};
  lnbits_extension_host_storage_delete_response_t response = {0};
  paysplit_string_dup(&request.table, table);
  paysplit_string_dup(&request.id, id);
  lnbits_extension_host_storage_delete(&request, &response);
  lnbits_extension_host_storage_delete_request_free(&request);
  return response.ok;
}

char *lnbits_random_id(const char *prefix) {
  lnbits_extension_host_random_id_request_t request = {0};
  lnbits_extension_host_random_id_response_t response = {0};
  paysplit_string_dup(&request.prefix, prefix);
  lnbits_extension_host_random_id(&request, &response);
  lnbits_extension_host_random_id_request_free(&request);
  char *id = copy_paysplit_string(&response.id);
  lnbits_extension_host_random_id_response_free(&response);
  return id;
}

uint64_t lnbits_now(void) {
  lnbits_extension_host_now_response_t response = {0};
  lnbits_extension_host_now(&response);
  return response.timestamp;
}

void lnbits_log(const char *level, const char *message) {
  lnbits_extension_host_log_request_t request = {0};
  lnbits_extension_host_log_response_t response = {0};
  paysplit_string_dup(&request.level, level);
  paysplit_string_dup(&request.message, message);
  lnbits_extension_host_log(&request, &response);
  lnbits_extension_host_log_request_free(&request);
}

char *lnbits_list_user_wallets_json(void) {
  lnbits_extension_host_list_user_wallets_response_t response = {0};
  lnbits_extension_host_list_user_wallets(&response);
  Buffer buffer;
  buffer_init(&buffer);
  buffer_append(&buffer, "{\"wallets\":[");
  for (size_t i = 0; i < response.wallets.len; i++) {
    lnbits_extension_host_user_wallet_summary_t *wallet = &response.wallets.ptr[i];
    char *id = copy_paysplit_string(&wallet->id);
    char *name = copy_paysplit_string(&wallet->name);
    char *currency = copy_optional_paysplit_string(&wallet->currency);
    char *escaped_id = json_escape(id);
    char *escaped_name = json_escape(name);
    char *escaped_currency = json_escape(currency);
    buffer_append(
      &buffer,
      "%s{\"id\":\"%s\",\"name\":\"%s\",\"currency\":",
      i ? "," : "",
      escaped_id,
      escaped_name);
    if (wallet->currency.is_some) {
      buffer_append(&buffer, "\"%s\"}", escaped_currency);
    } else {
      buffer_append(&buffer, "null}");
    }
    free(id);
    free(name);
    free(currency);
    free(escaped_id);
    free(escaped_name);
    free(escaped_currency);
  }
  buffer_append(&buffer, "]}");
  lnbits_extension_host_list_user_wallets_response_free(&response);
  return buffer.data;
}

LnbitsPayResponse lnbits_pay_lnurl(
  const char *wallet_id,
  const char *lnurl,
  double amount,
  const char *currency,
  const char *comment,
  const char *description,
  uint64_t max_sat,
  const char *extra_json
) {
  (void)extra_json;
  lnbits_extension_host_pay_lnurl_request_t request = {0};
  lnbits_extension_host_pay_invoice_response_t response = {0};
  paysplit_string_dup(&request.wallet_id, wallet_id);
  paysplit_string_dup(&request.lnurl, lnurl);
  request.amount = amount;
  paysplit_string_dup(&request.currency, currency);
  request.comment.is_some = comment && *comment;
  if (request.comment.is_some) {
    paysplit_string_dup(&request.comment.val, comment);
  }
  paysplit_string_dup(&request.description, description);
  request.max_sat.is_some = max_sat > 0;
  request.max_sat.val = max_sat;
  request.extra.ptr = NULL;
  request.extra.len = 0;
  lnbits_extension_host_pay_lnurl(&request, &response);
  lnbits_extension_host_pay_lnurl_request_free(&request);

  LnbitsPayResponse pay_response = {
    .ok = response.ok,
    .error = copy_optional_paysplit_string(&response.error),
    .checking_id = copy_optional_paysplit_string(&response.checking_id),
    .payment_hash = copy_optional_paysplit_string(&response.payment_hash),
    .status = copy_optional_paysplit_string(&response.status),
    .amount_msat = response.amount_msat,
    .fee_msat = response.fee_msat,
    .pending = response.pending,
    .success = response.success,
  };
  lnbits_extension_host_pay_invoice_response_free(&response);
  return pay_response;
}

static char *ok_json(const char *data_json) {
  Buffer buffer;
  buffer_init(&buffer);
  buffer_append(&buffer, "{\"ok\":true,\"data\":%s}", data_json ? data_json : "{}");
  return buffer.data;
}

static char *error_json(const char *message) {
  char *escaped = json_escape(message);
  Buffer buffer;
  buffer_init(&buffer);
  buffer_append(&buffer, "{\"ok\":false,\"error\":\"%s\"}", escaped ? escaped : "");
  free(escaped);
  return buffer.data;
}

static const char *skip_ws(const char *json) {
  while (json && isspace((unsigned char)*json)) {
    json++;
  }
  return json;
}

static char *json_string_value(const char *json, const char *key) {
  if (!json || !key) {
    return xstrdup("");
  }
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json, pattern);
  if (!pos) {
    return xstrdup("");
  }
  pos = strchr(pos + strlen(pattern), ':');
  if (!pos) {
    return xstrdup("");
  }
  pos = skip_ws(pos + 1);
  if (*pos != '"') {
    return xstrdup("");
  }
  pos++;
  Buffer buffer;
  buffer_init(&buffer);
  while (*pos && *pos != '"') {
    if (*pos == '\\' && pos[1]) {
      pos++;
      switch (*pos) {
        case 'n':
          buffer_append(&buffer, "\n");
          break;
        case 'r':
          buffer_append(&buffer, "\r");
          break;
        case 't':
          buffer_append(&buffer, "\t");
          break;
        default:
          buffer_append(&buffer, "%c", *pos);
      }
    } else {
      buffer_append(&buffer, "%c", *pos);
    }
    pos++;
  }
  return buffer.data;
}

static double json_number_value(const char *json, const char *key, double fallback) {
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json ? json : "", pattern);
  if (!pos) {
    return fallback;
  }
  pos = strchr(pos + strlen(pattern), ':');
  if (!pos) {
    return fallback;
  }
  pos = skip_ws(pos + 1);
  char *end = NULL;
  double value = strtod(pos, &end);
  return end == pos ? fallback : value;
}

static bool json_bool_value(const char *json, const char *key, bool fallback) {
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json ? json : "", pattern);
  if (!pos) {
    return fallback;
  }
  pos = strchr(pos + strlen(pattern), ':');
  if (!pos) {
    return fallback;
  }
  pos = skip_ws(pos + 1);
  if (strncmp(pos, "true", 4) == 0) {
    return true;
  }
  if (strncmp(pos, "false", 5) == 0) {
    return false;
  }
  return fallback;
}

static char *json_object_slice(const char *start) {
  if (!start || *start != '{') {
    return xstrdup("{}");
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  const char *cursor = start;
  while (*cursor) {
    char ch = *cursor;
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
    } else if (ch == '"') {
      in_string = true;
    } else if (ch == '{') {
      depth++;
    } else if (ch == '}') {
      depth--;
      if (depth == 0) {
        size_t len = (size_t)(cursor - start + 1);
        char *copy = malloc(len + 1);
        memcpy(copy, start, len);
        copy[len] = '\0';
        return copy;
      }
    }
    cursor++;
  }
  return xstrdup("{}");
}

static char *json_array_slice(const char *json, const char *key) {
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json ? json : "", pattern);
  if (!pos) {
    return xstrdup("[]");
  }
  pos = strchr(pos + strlen(pattern), ':');
  if (!pos) {
    return xstrdup("[]");
  }
  pos = skip_ws(pos + 1);
  if (*pos != '[') {
    return xstrdup("[]");
  }
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  const char *cursor = pos;
  while (*cursor) {
    char ch = *cursor;
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (ch == '\\') {
        escaped = true;
      } else if (ch == '"') {
        in_string = false;
      }
    } else if (ch == '"') {
      in_string = true;
    } else if (ch == '[') {
      depth++;
    } else if (ch == ']') {
      depth--;
      if (depth == 0) {
        size_t len = (size_t)(cursor - pos + 1);
        char *copy = malloc(len + 1);
        memcpy(copy, pos, len);
        copy[len] = '\0';
        return copy;
      }
    }
    cursor++;
  }
  return xstrdup("[]");
}

static TargetList parse_targets_array(const char *array_json) {
  TargetList list = {0};
  const char *cursor = array_json ? array_json : "[]";
  while ((cursor = strchr(cursor, '{'))) {
    char *object = json_object_slice(cursor);
    list.items = realloc(list.items, sizeof(Target) * (list.len + 1));
    Target *target = &list.items[list.len++];
    target->id = json_string_value(object, "id");
    target->source_wallet_id = json_string_value(object, "source_wallet_id");
    target->alias = json_string_value(object, "alias");
    target->lnurl = json_string_value(object, "lnurl");
    target->percent = json_number_value(object, "percent", 0.0);
    target->created_at = (uint64_t)json_number_value(object, "created_at", 0);
    target->updated_at = (uint64_t)json_number_value(object, "updated_at", 0);
    cursor += strlen(object);
    free(object);
  }
  return list;
}

static void free_target_list(TargetList *list) {
  for (size_t i = 0; i < list->len; i++) {
    free(list->items[i].id);
    free(list->items[i].source_wallet_id);
    free(list->items[i].alias);
    free(list->items[i].lnurl);
  }
  free(list->items);
  list->items = NULL;
  list->len = 0;
}

static Source parse_source(const char *json) {
  Source source = {0};
  source.id = json_string_value(json, "id");
  source.wallet_id = json_string_value(json, "wallet_id");
  source.wallet_name = json_string_value(json, "wallet_name");
  source.max_amount = (int64_t)json_number_value(json, "max_amount", 0);
  source.enabled = json_bool_value(json, "enabled", false);
  source.created_at = (uint64_t)json_number_value(json, "created_at", 0);
  source.updated_at = (uint64_t)json_number_value(json, "updated_at", 0);
  return source;
}

static void free_source(Source *source) {
  free(source->id);
  free(source->wallet_id);
  free(source->wallet_name);
}

static bool looks_like_lnurl_target(const char *value) {
  if (!value || !*value) {
    return false;
  }
  if (strchr(value, '@')) {
    return true;
  }
  if (strncasecmp(value, "lnurl", 5) == 0) {
    return true;
  }
  return strncasecmp(value, "lightning:", 10) == 0;
}

static char *source_to_json(const Source *source) {
  char *id = json_escape(source->id);
  char *wallet_id = json_escape(source->wallet_id);
  char *wallet_name = json_escape(source->wallet_name);
  Buffer buffer;
  buffer_init(&buffer);
  buffer_append(
    &buffer,
    "{\"id\":\"%s\",\"wallet_id\":\"%s\",\"wallet_name\":\"%s\","
    "\"max_amount\":%lld,\"enabled\":%s,\"created_at\":%llu,\"updated_at\":%llu}",
    id,
    wallet_id,
    wallet_name,
    (long long)source->max_amount,
    source->enabled ? "true" : "false",
    (unsigned long long)source->created_at,
    (unsigned long long)source->updated_at);
  free(id);
  free(wallet_id);
  free(wallet_name);
  return buffer.data;
}

static char *target_to_json(const Target *target) {
  char *id = json_escape(target->id);
  char *source_wallet_id = json_escape(target->source_wallet_id);
  char *alias = json_escape(target->alias);
  char *lnurl = json_escape(target->lnurl);
  Buffer buffer;
  buffer_init(&buffer);
  buffer_append(
    &buffer,
    "{\"id\":\"%s\",\"source_wallet_id\":\"%s\",\"alias\":\"%s\","
    "\"lnurl\":\"%s\",\"percent\":%.2f,\"created_at\":%llu,\"updated_at\":%llu}",
    id,
    source_wallet_id,
    alias,
    lnurl,
    target->percent,
    (unsigned long long)target->created_at,
    (unsigned long long)target->updated_at);
  free(id);
  free(source_wallet_id);
  free(alias);
  free(lnurl);
  return buffer.data;
}

static char *targets_to_json(const TargetList *targets) {
  Buffer buffer;
  buffer_init(&buffer);
  buffer_append(&buffer, "[");
  for (size_t i = 0; i < targets->len; i++) {
    char *target_json = target_to_json(&targets->items[i]);
    buffer_append(&buffer, "%s%s", i ? "," : "", target_json);
    free(target_json);
  }
  buffer_append(&buffer, "]");
  return buffer.data;
}

static char *load_targets_for_wallet(const char *wallet_id) {
  char *wallet_id_escaped = json_escape(wallet_id);
  Buffer filters;
  buffer_init(&filters);
  buffer_append(
    &filters,
    "{\"source_wallet_id\":\"%s\"}",
    wallet_id_escaped ? wallet_id_escaped : "");
  free(wallet_id_escaped);

  char *rows = lnbits_storage_get_paginated(
    TARGET_TABLE,
    filters.data,
    "",
    "[]",
    "created_at",
    false,
    200,
    0);
  free(filters.data);
  return rows ? rows : xstrdup("[]");
}

char *list_wallets(const char *request_json) {
  (void)request_json;
  char *wallets_json = lnbits_list_user_wallets_json();
  char *response = ok_json(wallets_json ? wallets_json : "{\"wallets\":[]}");
  free(wallets_json);
  return response;
}

char *get_source(const char *request_json) {
  char *wallet_id = json_string_value(request_json, "walletId");
  if (!wallet_id || !*wallet_id) {
    free(wallet_id);
    return error_json("walletId is required.");
  }

  char *source_json = lnbits_storage_get(SOURCE_TABLE, wallet_id);
  char *targets_json = load_targets_for_wallet(wallet_id);
  Buffer data;
  buffer_init(&data);
  buffer_append(
    &data,
    "{\"source\":%s,\"targets\":%s}",
    source_json ? source_json : "null",
    targets_json ? targets_json : "[]");

  free(wallet_id);
  free(source_json);
  free(targets_json);
  return ok_json(data.data);
}

static bool target_id_seen(TargetList *targets, const char *target_id) {
  for (size_t i = 0; i < targets->len; i++) {
    if (strcmp(targets->items[i].id, target_id) == 0) {
      return true;
    }
  }
  return false;
}

char *save_source(const char *request_json) {
  char *wallet_id = json_string_value(request_json, "walletId");
  char *wallet_name = json_string_value(request_json, "walletName");
  char *targets_array = json_array_slice(request_json, "targets");
  TargetList targets = parse_targets_array(targets_array);
  free(targets_array);

  if (!wallet_id || !*wallet_id) {
    free(wallet_id);
    free(wallet_name);
    free_target_list(&targets);
    return error_json("Select a source wallet.");
  }

  double total_percent = 0.0;
  for (size_t i = 0; i < targets.len; i++) {
    if (targets.items[i].percent <= 0 || targets.items[i].percent > 100) {
      free(wallet_id);
      free(wallet_name);
      free_target_list(&targets);
      return error_json("Each target percentage must be between 1 and 100.");
    }
    if (!looks_like_lnurl_target(targets.items[i].lnurl)) {
      free(wallet_id);
      free(wallet_name);
      free_target_list(&targets);
      return error_json("Each target must be a Lightning Address or LNURL-pay.");
    }
    total_percent += targets.items[i].percent;
  }

  if (total_percent > 100.0) {
    free(wallet_id);
    free(wallet_name);
    free_target_list(&targets);
    return error_json("Total split percentage cannot exceed 100.");
  }

  uint64_t now = lnbits_now();
  char *existing_json = lnbits_storage_get(SOURCE_TABLE, wallet_id);
  Source existing = parse_source(existing_json ? existing_json : "{}");
  Source source = {
    .id = wallet_id,
    .wallet_id = wallet_id,
    .wallet_name = wallet_name && *wallet_name ? wallet_name : wallet_id,
    .max_amount = (int64_t)json_number_value(request_json, "maxAmount", 0),
    .enabled = json_bool_value(request_json, "enabled", true),
    .created_at = existing.created_at ? existing.created_at : now,
    .updated_at = now,
  };
  char *source_json = source_to_json(&source);
  lnbits_storage_set(SOURCE_TABLE, source_json);

  char *old_targets_json = load_targets_for_wallet(wallet_id);
  TargetList old_targets = parse_targets_array(old_targets_json);
  for (size_t i = 0; i < targets.len; i++) {
    if (!targets.items[i].id || !*targets.items[i].id) {
      free(targets.items[i].id);
      targets.items[i].id = lnbits_random_id("target");
    }
    free(targets.items[i].source_wallet_id);
    targets.items[i].source_wallet_id = xstrdup(wallet_id);
    if (!targets.items[i].created_at) {
      targets.items[i].created_at = now;
    }
    targets.items[i].updated_at = now;
    char *target_json = target_to_json(&targets.items[i]);
    lnbits_storage_set(TARGET_TABLE, target_json);
    free(target_json);
  }
  for (size_t i = 0; i < old_targets.len; i++) {
    if (!target_id_seen(&targets, old_targets.items[i].id)) {
      lnbits_storage_delete(TARGET_TABLE, old_targets.items[i].id);
    }
  }

  char *targets_json = targets_to_json(&targets);
  Buffer data;
  buffer_init(&data);
  buffer_append(&data, "{\"source\":%s,\"targets\":%s}", source_json, targets_json);
  char *response = ok_json(data.data);

  free(source_json);
  free(targets_json);
  free(existing_json);
  free(old_targets_json);
  free_source(&existing);
  free_target_list(&old_targets);
  free_target_list(&targets);
  free(wallet_id);
  if (wallet_name && wallet_name != source.wallet_name) {
    free(wallet_name);
  }
  free(data.data);
  return response;
}

char *delete_source(const char *request_json) {
  char *wallet_id = json_string_value(request_json, "walletId");
  if (!wallet_id || !*wallet_id) {
    free(wallet_id);
    return error_json("walletId is required.");
  }

  char *targets_json = load_targets_for_wallet(wallet_id);
  TargetList targets = parse_targets_array(targets_json);
  for (size_t i = 0; i < targets.len; i++) {
    lnbits_storage_delete(TARGET_TABLE, targets.items[i].id);
  }
  lnbits_storage_delete(SOURCE_TABLE, wallet_id);

  free(wallet_id);
  free(targets_json);
  free_target_list(&targets);
  return ok_json("{\"deleted\":true}");
}

char *record_payment(const char *event_json) {
  char *wallet_id = json_string_value(event_json, "wallet_id");
  if (!wallet_id || !*wallet_id) {
    free(wallet_id);
    wallet_id = json_string_value(event_json, "walletId");
  }
  char *payment_hash = json_string_value(event_json, "payment_hash");
  if (!payment_hash || !*payment_hash) {
    free(payment_hash);
    payment_hash = json_string_value(event_json, "paymentHash");
  }

  if (!wallet_id || !*wallet_id) {
    free(wallet_id);
    free(payment_hash);
    return ok_json("{\"skipped\":\"missing wallet\"}");
  }

  if (
    json_bool_value(event_json, "splitted", false) ||
    json_bool_value(event_json, "background_payment", false)
  ) {
    free(wallet_id);
    free(payment_hash);
    return ok_json("{\"skipped\":\"split payment\"}");
  }

  double amount_msat = json_number_value(event_json, "amount_msat", 0);
  if (amount_msat <= 0) {
    amount_msat = json_number_value(event_json, "amountMsat", 0);
  }
  if (amount_msat <= 0) {
    amount_msat = json_number_value(event_json, "amount", 0);
  }
  int64_t amount_sat = (int64_t)floor(amount_msat / 1000.0);
  if (amount_sat <= 0) {
    free(wallet_id);
    free(payment_hash);
    return ok_json("{\"skipped\":\"non-positive amount\"}");
  }

  char *source_json = lnbits_storage_get(SOURCE_TABLE, wallet_id);
  if (!source_json) {
    free(wallet_id);
    free(payment_hash);
    return ok_json("{\"skipped\":\"source not configured\"}");
  }
  Source source = parse_source(source_json);
  if (!source.enabled) {
    free(wallet_id);
    free(payment_hash);
    free(source_json);
    free_source(&source);
    return ok_json("{\"skipped\":\"source disabled\"}");
  }
  if (source.max_amount > 0 && amount_sat > source.max_amount) {
    free(wallet_id);
    free(payment_hash);
    free(source_json);
    free_source(&source);
    return ok_json("{\"skipped\":\"amount above configured maximum\"}");
  }

  char *targets_json = load_targets_for_wallet(wallet_id);
  TargetList targets = parse_targets_array(targets_json);
  uint32_t sent = 0;
  uint32_t failed = 0;
  uint64_t now = lnbits_now();

  for (size_t i = 0; i < targets.len; i++) {
    int64_t split_sat = (int64_t)floor(((double)amount_sat * targets.items[i].percent) / 100.0);
    if (split_sat <= 0) {
      continue;
    }

    char *extra_payment_hash = json_escape(payment_hash);
    char *extra_wallet_id = json_escape(wallet_id);
    char *extra_target_id = json_escape(targets.items[i].id);
    Buffer extra;
    buffer_init(&extra);
    buffer_append(
      &extra,
      "[[\"extension\",\"%s\"],[\"splitted\",\"true\"],[\"source_payment_hash\",\"%s\"],"
      "[\"source_wallet_id\",\"%s\"],[\"target_id\",\"%s\"]]",
      EXT_ID,
      extra_payment_hash,
      extra_wallet_id,
      extra_target_id);
    free(extra_payment_hash);
    free(extra_wallet_id);
    free(extra_target_id);

    LnbitsPayResponse pay_response = lnbits_pay_lnurl(
      wallet_id,
      targets.items[i].lnurl,
      (double)split_sat,
      "sat",
      NULL,
      "PaySplit split payment",
      (uint64_t)split_sat,
      extra.data);

    char *split_id = lnbits_random_id("split");
    char *target_id = json_escape(targets.items[i].id);
    char *source_hash = json_escape(payment_hash);
    char *target_hash = json_escape(pay_response.payment_hash);
    char *status = json_escape(pay_response.success ? "success" : "failed");
    char *error = json_escape(pay_response.error);
    Buffer split;
    buffer_init(&split);
    buffer_append(
      &split,
      "{\"id\":\"%s\",\"source_wallet_id\":\"%s\",\"target_id\":\"%s\","
      "\"source_payment_hash\":\"%s\",\"target_payment_hash\":\"%s\","
      "\"amount_sat\":%lld,\"status\":\"%s\",\"error\":\"%s\",\"created_at\":%llu}",
      split_id,
      wallet_id,
      target_id,
      source_hash,
      target_hash,
      (long long)split_sat,
      status,
      error,
      (unsigned long long)now);
    lnbits_storage_set(SPLIT_TABLE, split.data);

    if (pay_response.success) {
      sent++;
    } else {
      failed++;
    }

    free(extra.data);
    free(split_id);
    free(target_id);
    free(source_hash);
    free(target_hash);
    free(status);
    free(error);
    free(split.data);
    free(pay_response.error);
    free(pay_response.checking_id);
    free(pay_response.payment_hash);
    free(pay_response.status);
  }

  Buffer data;
  buffer_init(&data);
  buffer_append(&data, "{\"sent\":%u,\"failed\":%u}", sent, failed);
  char *response = ok_json(data.data);
  free(data.data);
  free(wallet_id);
  free(payment_hash);
  free(source_json);
  free(targets_json);
  free_source(&source);
  free_target_list(&targets);
  return response;
}

void exports_paysplit_list_wallets(
  paysplit_string_t *request_json, paysplit_string_t *ret
) {
  char *request = paysplit_string_to_c(request_json);
  char *response = list_wallets(request);
  free(request);
  paysplit_return_string(ret, response);
}

void exports_paysplit_get_source(
  paysplit_string_t *request_json, paysplit_string_t *ret
) {
  char *request = paysplit_string_to_c(request_json);
  char *response = get_source(request);
  free(request);
  paysplit_return_string(ret, response);
}

void exports_paysplit_save_source(
  paysplit_string_t *request_json, paysplit_string_t *ret
) {
  char *request = paysplit_string_to_c(request_json);
  char *response = save_source(request);
  free(request);
  paysplit_return_string(ret, response);
}

void exports_paysplit_delete_source(
  paysplit_string_t *request_json, paysplit_string_t *ret
) {
  char *request = paysplit_string_to_c(request_json);
  char *response = delete_source(request);
  free(request);
  paysplit_return_string(ret, response);
}

void exports_paysplit_record_payment(
  paysplit_string_t *event_json, paysplit_string_t *ret
) {
  char *event = paysplit_string_to_c(event_json);
  char *response = record_payment(event);
  free(event);
  paysplit_return_string(ret, response);
}
