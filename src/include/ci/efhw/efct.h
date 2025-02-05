/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/* X-SPDX-Copyright-Text: (c) Copyright 2019-2020 Xilinx, Inc. */

#ifndef CI_EFHW_EFCT_H
#define CI_EFHW_EFCT_H
#include <etherfabric/internal/efct_uk_api.h>
#include <ci/driver/ci_efct.h>
#include <ci/tools/sysdep.h>

extern struct efhw_func_ops efct_char_functional_units;

struct efhw_efct_rxq;
struct xlnx_efct_hugepage;
struct xlnx_efct_rxq_params;
typedef void efhw_efct_rxq_free_func_t(struct efhw_efct_rxq*);
typedef void efhw_efct_rxq_int_wake_func_t(struct efhw_efct_rxq*);

/* Packet sequences are defined as (superbuf_seqno << 16) | pkt_index_in_sb,
 * therefore -1 is an impossible value because we'll never have 65535 packets
 * in a single superbuf */
#define EFCT_INVALID_PKT_SEQNO (~0u)

struct efhw_efct_rxq {
  struct efhw_efct_rxq *next;
  struct efab_efct_rxq_uk_shm_q *shm;
  unsigned qid;
  bool destroy;
  uint32_t next_sbuf_seq;
  size_t n_hugepages;
  uint32_t wake_at_seqno;
  uint32_t current_owned_superbufs;
  uint32_t max_allowed_superbufs;
  CI_BITS_DECLARE(owns_superbuf, CI_EFCT_MAX_SUPERBUFS);
  efhw_efct_rxq_free_func_t *freer;
  unsigned wakeup_instance;
};

/* TODO EFCT find somewhere better to put this */
#define CI_EFCT_MAX_RXQS  8
#define CI_EFCT_MAX_EVQS 24

struct efhw_nic_efct_rxq_superbuf {
  /* Each value is (sentinel << 15) | sbid, i.e. identical to
   * efab_efct_rx_superbuf_queue::q */
  uint16_t value;
  uint32_t global_seqno;
};

struct efhw_nic_efct_rxq {
  struct efhw_efct_rxq *new_apps;  /* Owned by process context */
  struct efhw_efct_rxq *live_apps; /* Owned by NAPI context */
  struct efhw_efct_rxq *destroy_apps; /* Owned by NAPI context */
  uint32_t superbuf_refcount[CI_EFCT_MAX_SUPERBUFS];
  /* Tracks buffers passed to us from the driver in order they are going
   * to be filled by HW. We need to do this to:
   *  * progressively refill client app superbuf queues,
   *    as x3net can refill RX ring with more superbufs than an app can hold
   *    (or if queues are equal there is a race)
   *  * resume a stopped app (subset of the above really),
   *  * start new app (without rollover)
   */
  struct {
    struct efhw_nic_efct_rxq_superbuf q[16];
    uint32_t added;
    uint32_t removed;
  } sbufs;
  struct work_struct destruct_wq;
  uint32_t now;
  uint32_t awaiters;
};

struct efhw_nic_efct_evq {
  struct efhw_nic *nic;
  atomic_t queues_flushing;
  struct delayed_work check_flushes;
  void *base;
  unsigned capacity;
};

struct efct_hw_filter {
  uint32_t lip;
  uint16_t lport;
  uint16_t proto;
  uint8_t rxq;
  bool exclusive;
  uint32_t refcount;
  int net_driver_id;
};

#define MAX_EFCT_FILTERS  256

struct efhw_nic_efct {
  struct efhw_nic_efct_rxq rxq[CI_EFCT_MAX_RXQS];
  struct efhw_nic_efct_evq evq[CI_EFCT_MAX_EVQS];
  struct xlnx_efct_device *edev;
  struct xlnx_efct_client *client;
  struct efhw_nic *nic;
  struct efct_hw_filter driver_filters[MAX_EFCT_FILTERS];
#ifdef __KERNEL__
  /* ZF emu includes this file from UL */
  struct mutex driver_filters_mtx;
#endif
};

#if CI_HAVE_EFCT_AUX
int efct_nic_rxq_bind(struct efhw_nic *nic, int qid,
                      const struct cpumask *mask, bool timestamp_req,
                      size_t n_hugepages, struct file* memfd, off_t* memfd_off,
                      struct efab_efct_rxq_uk_shm_q *shm,
                      unsigned wakeup_instance, struct efhw_efct_rxq *rxq);
void efct_nic_rxq_free(struct efhw_nic *nic, struct efhw_efct_rxq *rxq,
                       efhw_efct_rxq_free_func_t *freer);
int efct_get_hugepages(struct efhw_nic *nic, int hwqid,
                       struct xlnx_efct_hugepage *pages, size_t n_pages);
int efct_request_wakeup(struct efhw_nic_efct *efct, struct efhw_efct_rxq *app,
                        unsigned sbseq, unsigned pktix);
#endif

static inline void efct_app_list_push(struct efhw_efct_rxq **head,
                                      struct efhw_efct_rxq *app)
{
  struct efhw_efct_rxq *next;
  do {
    app->next = next = *head;
  } while( ci_cas_uintptr_fail(head, (uintptr_t) next, (uintptr_t)app) );
}

#endif /* CI_EFHW_EFCT_H */
