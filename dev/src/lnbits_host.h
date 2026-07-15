#ifndef LNBITS_HOST_H
#define LNBITS_HOST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  char *data;
  uint32_t len;
} LnbitsString;

typedef struct {
  char *id;
  char *name;
  char *currency;
} LnbitsWallet;

typedef struct {
  bool ok;
  char *error;
  char *checking_id;
  char *payment_hash;
  char *status;
  uint64_t amount_msat;
  uint64_t fee_msat;
  bool pending;
  bool success;
} LnbitsPayResponse;

char *lnbits_storage_get(const char *table, const char *id);
bool lnbits_storage_set(const char *table, const char *data_json);
char *lnbits_storage_get_paginated(
  const char *table,
  const char *filters_json,
  const char *search,
  const char *search_fields_json,
  const char *sort_by,
  bool descending,
  uint32_t limit,
  uint32_t offset
);
bool lnbits_storage_delete(const char *table, const char *id);
char *lnbits_random_id(const char *prefix);
uint64_t lnbits_now(void);
void lnbits_log(const char *level, const char *message);
char *lnbits_list_user_wallets_json(void);
LnbitsPayResponse lnbits_pay_lnurl(
  const char *wallet_id,
  const char *lnurl,
  double amount,
  const char *currency,
  const char *comment,
  const char *description,
  uint64_t max_sat,
  const char *extra_json
);

#endif
