/**
 * @file graphene.h
 * @brief Graphene block propagation protocol
 *
 * Based on "Graphene: A New Protocol for Block Propagation Using
 * Set Reconciliation" — A. Ozisik et al., ACM SIGCOMM 2019
 * https://people.cs.umass.edu/~gbiss/graphene.sigcomm.pdf
 *
 * - Protocol 1 flow (all txs of the block in mempool)
 *  Sender
 *      1. Sends a inv for a block
 *  Receiver
 *      1. Requests the unknown block, including a count of transactions in
 * mempool, m
 *
 *  Sender  (with n txs in block)
 *    1. Build a Bloom filter (S) of block tx IDs.
 *    2. Build an IBLT (I) of block tx IDs.
 *      > The FPR of S is fs = a/(m - n)
 *      > and the IBLT is parameterized such that a' items can be recoverede
 * where a' > a with _beta_-assurance > We set a so as to minimize the total
 * size of S and I.
 *    3. Send GRAPHENE_BLOCK  ←  {header, parent_hashes, BF, IBLT, tx_count}
 *
 *  Receiver  (on GRAPHENE_BLOCK)
 *    1. Reconstruct the BF from the message.
 *    2. Candidate set  ←  mempool txs that pass the BF.
 *       (All real block txs pass; some non-block txs are false-positives.)
 *    3. Build a receiver IBLT from the candidate set.
 *    4. diff_IBLT  =  sender_IBLT  −  receiver_IBLT (evaluetes to simdiff
 * https://dl.acm.org/doi/pdf/10.1145/2043164.2018462)
 *    5. Based on the result, adjusts the candidate set, validates the Merkle
 * root in the block header, and decodes the block
 *
 * - Protocol 2 flow (init when protocol 1 fails -- cant decode)
 *   Receiver
 *      1. The size of the candidate set is |Z| = z, where z = x + y, a sum of x
 * true positives and y false positives
 *      2. calculates y' and x' such that (y' _or _ x' >= y _or_ x) with
 * _beta_-assurance
 *      3. The receiver creates Bloom filter R and adds all transaction IDs in Z
 * to R > the FPR of the filter is fR = b/(n−x'), where b minimizes the size of
 * R and IBLT J
 *      4. Sends R, y∗ and b
 *
 *   Sender
 *      1. The sender passes all transaction IDs in the block through R and
 * sends all transactions that are not in R directly
 *      2. Creates and sends an IBLT J of all transactions in the block such
 * that b + y' items can be recovered from it > This size accounts for b, the
 * number of transactions that falsely appear to be in R, and y∗, > the number
 * of transactions that falsely appear to be in S
 *
 *   Receiver
 *      1. creates IBLT J′ from the transaction IDs in Z and the new transaction
 * IDs sent by the sender
 *      2. diff_IBLT = J − J'
 *      3. adjusts set Z , validates the Merkle root, and decodes the block
 *
 *
 *
 *
 * @author Eduardo Lechinski Ramos <lechinski@univali.br>
 * @date 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#pragma once

#include "dag.h"
#include "thirdparty/bloom_filter.h"
#include "thirdparty/iblt.h"
#include "thirdparty/json.h"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct GrapheneState {
  BlockHeader header;
  size_t tx_count = 0;

  std::set<uint64_t> candidate_ids;

  std::optional<bloom_filter> sender_bloom;
  std::optional<IBLT> sender_iblt;
  std::optional<IBLT> diff1;

  bool waiting_protocol2 = false;
};

struct IncomingBlockResult {
  bool success = false;
  Block block;
  GrapheneState recovery_state;
  nlohmann::json recovery_request;
};

struct RecoveryResponseResult {
  bool success = false;
  std::set<uint64_t> block_txids;
};

class GrapheneProtocol {
public:
  static constexpr size_t IBLT_VALUE_SIZE = sizeof(uint64_t);

  enum class DecodeStatus { SUCCESS, FAIL_RECOVERABLE, FAIL_FATAL };

  static size_t BuildSenderComponents(const std::set<Transaction> &block_txs,
                                      size_t receiver_mempool_count,
                                      bloom_filter &out_bf, IBLT &out_iblt);

  static DecodeStatus ReconstructBlock(
      const bloom_filter &bf, const IBLT &sender_iblt, size_t tx_count,
      const std::vector<std::pair<uint32_t, uint64_t>> &mempool_entries,
      std::set<Transaction> &out_txs);

  static size_t BuildRecoveryBloom(const std::vector<uint64_t> &Z,
                                   const size_t m, const size_t n,
                                   double sender_fpr, bloom_filter &out_bf,
                                   int &out_b, int &out_y_star);

  static IBLT SecondIBLT(const Block &blk, const bloom_filter &receiver_bloom,
                         int y_star, int b, std::vector<uint64_t> &missing);

  static DecodeStatus ReconstructRecoveryBlock(const nlohmann::json &data,
                                               std::set<uint64_t> &Z);

  static DecodeStatus TryPingPong(IBLT &first, IBLT &second,
                                  std::set<uint64_t> &in_block,
                                  std::set<uint64_t> &not_in_block);

  static std::vector<uint8_t> U64ToVec(uint64_t v);

  static IncomingBlockResult
  ProcessIncomingBlock(const nlohmann::json &data,
                       const std::vector<std::pair<uint32_t, uint64_t>> &mempool,
                       size_t mempool_size);

  static RecoveryResponseResult
  ProcessRecoveryResponse(const nlohmann::json &data,
                          const GrapheneState &state);

  static nlohmann::json SerializeBloomFilter(const bloom_filter &bf);
  static bloom_filter DeserializeBloomFilter(const nlohmann::json &j);

  static nlohmann::json SerializeIBLT(const IBLT &iblt);
  static IBLT DeserializeIBLT(const nlohmann::json &j);
};
