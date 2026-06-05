/**
 * @file graphene.cc
 * @brief Graphene block propagation protocol implementation
 *
 * @author Eduardo Lechinski Ramos <lechinski@univali.br>
 * @date 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "graphene.h"
#include "dag.h"
#include "thirdparty/bloom_filter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#define LN2SQUARED 0.4804530139182014246671025263266649717305529515945455

const uint8_t FILTER_CELL_SIZE = 1;
const uint8_t IBLT_CELL_SIZE = GrapheneProtocol::IBLT_VALUE_SIZE;
const uint32_t LARGE_MEM_POOL_SIZE = 10000000;
const float FILTER_FPR_MAX = 0.999;
const uint8_t IBLT_CELL_MINIMUM = 2;

namespace {

std::vector<uint64_t> TxIdsFromBlock(const std::set<Transaction> &txs) {
  std::vector<uint64_t> ids;
  ids.reserve(txs.size());
  for (const auto &tx : txs)
    ids.push_back(tx.tx_id);
  return ids;
}

std::set<uint64_t>
BuildCandidateSet(const bloom_filter &bf,
                  const std::vector<std::pair<uint32_t, uint64_t>> &mempool) {
  std::set<uint64_t> candidates;
  for (const auto &entry : mempool) {
    uint64_t tx_id = entry.second;
    if (bf.contains(tx_id))
      candidates.insert(tx_id);
  }
  return candidates;
}

void BuildIBLTFromIds(IBLT &iblt, const std::vector<uint64_t> &ids) {
  for (uint64_t id : ids)
    iblt.insert(id, GrapheneProtocol::U64ToVec(id));
}

} // anonymous namespace
//

// --- Graphene parameter helpers ---

static double ComputeDelta(int z, int x, int m, double fpr) {
  double temp = (m - x) * fpr;
  temp = (z - x) / temp;
  return temp - 1.0;
}

static double RHS(double delta, int m, int x, double fpr) {
  double num = std::exp(delta);
  double denom = std::pow(1.0 + delta, 1.0 + delta);
  double base = num / denom;
  double exponent = (m - x) * fpr;
  return std::pow(base, exponent);
}

static int SearchX(int z, int mempool_size, double fpr, double bound,
                   int blk_size) {
  double total = 0.0;
  int x_star = 0;
  int val = std::min(z, blk_size);

  for (int x = 0; x <= val; ++x) {
    double delta = ComputeDelta(z, x, mempool_size, fpr);
    double result = RHS(delta, mempool_size, x, fpr);

    if (total + result > bound) {
      if (x == 0) {
        x_star = x;
      } else {
        x_star = x - 1;
      }
      break;
    } else {
      total += result;
    }
  }

  return x_star;
}

static double CBbound(double a, double fpr, double bound) {
  if (a <= 0.0)
    return 1.0;
  double s = (-1.0 * std::log(bound)) / a;
  double temp = std::sqrt(s * (s + 8.0));
  double delta_1 = 0.5 * (s + temp);

  return (1.0 + delta_1) * a;
}

double OptimalSymDiff(uint64_t nBlockTxs, uint64_t nReceiverPoolTx) {
  /* Optimal symmetric difference between block txs and receiver mempool txs
   * passing though filter to use for IBLT.
   *
   * Let a be defined as the size of the symmetric difference between items in
   * the sender and receiver IBLTs.
   *
   * The total size in bytes of a graphene block is given by T(a) = F(a) +
   * L(a) as defined in the code below. (Note that meta parameters for the
   * Bloom Filter and IBLT are ignored).
   */
  __ASSERT__(nReceiverPoolTx >= nBlockTxs - 1,
             "Assume reciever is missing only one tx");

  if (nReceiverPoolTx > LARGE_MEM_POOL_SIZE)
    throw std::runtime_error("Receiver mempool is too large for optimization");

  // Because we assumed the receiver is only missing only one tx
  uint64_t nBlockAndReceiverPoolTx = nBlockTxs - 1;

  // Techinically there should be no symdiff here, but we need to have at
  // least one entry in the IBLT, otherwise the Bloom filter must have fpr = 0
  if (nReceiverPoolTx == nBlockAndReceiverPoolTx) {
    return 1;
  }

  auto fpr = [nReceiverPoolTx, nBlockAndReceiverPoolTx](uint64_t a) {
    float fpr = a / float(nReceiverPoolTx - nBlockAndReceiverPoolTx);

    return fpr < 1.0 ? fpr : FILTER_FPR_MAX;
  };

  auto F = [nBlockTxs, fpr](uint64_t a) {
    return floor(FILTER_CELL_SIZE *
                 (-1 / LN2SQUARED * nBlockTxs * log(fpr(a)) / 8));
  };

  auto L = [](uint64_t a) {
    uint8_t n_iblt_hash = OptimalNHash(a);
    float iblt_overhead = OptimalOverhead(a);
    uint64_t padded_cells = (int)(iblt_overhead * a);
    uint64_t cells = n_iblt_hash * int(ceil(padded_cells / float(n_iblt_hash)));

    return IBLT_CELL_SIZE * cells;
  };

  uint64_t optSymDiff = 1;
  double optT = std::numeric_limits<double>::max();
  for (uint64_t a = 1; a < nReceiverPoolTx; a++) {
    double T = F(a) + L(a);

    if (T < optT) {
      optSymDiff = a;
      optT = T;
    }
  }

  return optSymDiff;
}

//

std::vector<uint8_t> GrapheneProtocol::U64ToVec(uint64_t v) {
  std::vector<uint8_t> out(sizeof(uint64_t));
  for (size_t i = 0; i < sizeof(uint64_t); i++)
    out[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xff);
  return out;
}

//  Sender — Protocol 1

size_t
GrapheneProtocol::BuildSenderComponents(const std::set<Transaction> &block_txs,
                                        size_t receiver_mempool_count,
                                        bloom_filter &out_bf, IBLT &out_iblt) {

  size_t n = block_txs.size();
  size_t m = receiver_mempool_count;

  // Optimal symmetric differences between receiver and sender IBLTs
  // This is the parameter "a" from the graphene paper
  double optSymDiff = 1;
  try {
    if (n < m + 1)
      optSymDiff = OptimalSymDiff(n, m);
  } catch (const std::runtime_error &e) {
    // EVENT
  }

  // Sender's estimate of number of items in both block and receiver mempool
  // This is the parameter "mu" from the graphene paper
  uint64_t nItemIntersect = std::min(n, (uint64_t)m);

  // Set false positive rate for Bloom filter based on optSymDiff
  double fpr;
  uint64_t nReceiverExcessItems = m - nItemIntersect;
  if (optSymDiff >= nReceiverExcessItems)
    fpr = FILTER_FPR_MAX;
  else
    fpr = optSymDiff / float(nReceiverExcessItems);

  bloom_parameters params;
  params.projected_element_count = std::max((int)n, (int)10);
  params.false_positive_probability = fpr;
  params.compute_optimal_parameters();
  out_bf = bloom_filter(params);

  auto ids = TxIdsFromBlock(block_txs);
  for (uint64_t id : ids)
    out_bf.insert(id);

  double bound = 1.0 / 240.0;
  double a_star = CBbound(optSymDiff, fpr, bound);
  uint64_t nIbltCells = std::max((int)IBLT_CELL_MINIMUM, (int)ceil(a_star));

  auto params_iblt = CIbltParams::Lookup(nIbltCells);
  out_iblt = IBLT(nIbltCells, IBLT_VALUE_SIZE, params_iblt.overhead,
                  params_iblt.numhashes);
  BuildIBLTFromIds(out_iblt, ids);

  return n;
}

//  Receiver — Protocol 1

GrapheneProtocol::DecodeStatus GrapheneProtocol::ReconstructBlock(
    const bloom_filter &bf, const IBLT &sender_iblt, size_t tx_count,
    const std::vector<std::pair<uint32_t, uint64_t>> &mempool_entries,
    std::set<Transaction> &out_txs) {

  std::set<uint64_t> candidates = BuildCandidateSet(bf, mempool_entries);

  IBLT receiver_iblt(sender_iblt.hashTableSize(), IBLT_VALUE_SIZE, 1,
                     sender_iblt.numHashes);

  for (uint64_t id : candidates)
    receiver_iblt.insert(id, U64ToVec(id));

  IBLT diff = sender_iblt - receiver_iblt;

  std::set<std::pair<uint64_t, std::vector<uint8_t>>> positive;
  std::set<std::pair<uint64_t, std::vector<uint8_t>>> negative;

  if (!diff.listEntries(positive, negative)) {
    return DecodeStatus::FAIL_RECOVERABLE;
  }

  out_txs.clear();

  std::set<uint64_t> block_ids;
  for (uint64_t c : candidates)
    block_ids.insert(c);

  for (const auto &neg : negative) {
    uint64_t neg_id = neg.first;
    block_ids.erase(neg_id);
  }

  for (const auto &pos : positive) {
    uint64_t pos_id = pos.first;
    block_ids.insert(pos_id);
  }

  for (uint64_t id : block_ids) {
    Transaction tx;
    tx.tx_id = id;
    tx.size_bytes = 522;
    out_txs.insert(tx);
  }

  if (out_txs.size() != tx_count) {
    return DecodeStatus::FAIL_RECOVERABLE;
  }

  return DecodeStatus::SUCCESS;
}

size_t GrapheneProtocol::BuildRecoveryBloom(const std::vector<uint64_t> &Z,
                                            const size_t m, const size_t n,
                                            double sender_fpr,
                                            bloom_filter &out_bf, int &out_b,
                                            int &out_y_star) {
  size_t z = Z.size();
  double bound = 1.0 / 240.0;

  int x_star = SearchX(z, m, sender_fpr, bound, n);

  double temp = (m - x_star) * sender_fpr;

  out_y_star = std::ceil(CBbound(temp, sender_fpr, bound));

  // Compute optimal b (symmetric diff) and fpr for Protocol 2 recovery IBLT
  // fR = b / (n - x_star) where x_star is the lower bound on true positives
  int64_t nMissingFromBlock = static_cast<int64_t>(n) - x_star;
  if (nMissingFromBlock <= 0)
    nMissingFromBlock = 1;

  int best_b = 1;
  double best_fpr = 0;
  double best_total = std::numeric_limits<double>::max();

  for (int b = 1; b <= static_cast<int>(std::max(z, (size_t)1)); ++b) {
    double fpr_r = static_cast<double>(b) / nMissingFromBlock;
    if (fpr_r > FILTER_FPR_MAX)
      fpr_r = FILTER_FPR_MAX;
    if (fpr_r <= 0)
      fpr_r = 1e-6;

    double bf_size = std::ceil(FILTER_CELL_SIZE *
                               (-1.0 / LN2SQUARED * z * std::log(fpr_r) / 8));

    uint64_t total_items = static_cast<uint64_t>(b) + out_y_star;
    uint8_t n_iblt_hash = OptimalNHash(total_items);
    float iblt_overhead = OptimalOverhead(total_items);
    uint64_t padded_cells = static_cast<int>(iblt_overhead * total_items);
    uint64_t cells =
        n_iblt_hash * static_cast<int>(std::ceil(
                          padded_cells / static_cast<float>(n_iblt_hash)));
    double iblt_size = IBLT_CELL_SIZE * cells;

    double total = bf_size + iblt_size;
    if (total < best_total) {
      best_total = total;
      best_b = b;
      best_fpr = fpr_r;
    }
  }

  bloom_parameters p;
  p.projected_element_count = std::max(static_cast<int>(z), 10);
  p.false_positive_probability = best_fpr;
  p.compute_optimal_parameters();

  out_bf = bloom_filter(p);

  out_b = std::max(static_cast<int>(IBLT_CELL_MINIMUM),
                   static_cast<int>(ceil(best_b)));
  for (auto txid : Z) {
    out_bf.insert(txid);
  }

  return z;
}

IBLT GrapheneProtocol::SecondIBLT(const Block &blk,
                                  const bloom_filter &receiver_bloom,
                                  int y_star, int b,
                                  std::vector<uint64_t> &missing) {

  auto params_iblt = CIbltParams::Lookup(b + y_star);
  IBLT J(b + y_star, GrapheneProtocol::IBLT_VALUE_SIZE, params_iblt.overhead,
         params_iblt.numhashes);

  for (const auto &tx : blk.transactions) {

    J.insert(tx.tx_id, U64ToVec(tx.tx_id));

    if (!receiver_bloom.contains(tx.tx_id)) {
      missing.push_back(tx.tx_id);
    }
  }
  return J;
}

GrapheneProtocol::DecodeStatus
GrapheneProtocol::ReconstructRecoveryBlock(const nlohmann::json &data,
                                           std::set<uint64_t> &Z) {

  IBLT J = GrapheneProtocol::DeserializeIBLT(data["iblt"]);

  for (uint64_t txid : data["missing"]) {
    Z.insert(txid);
  }
  IBLT Jl(J.hashTableSize(), GrapheneProtocol::IBLT_VALUE_SIZE, 1, J.numHashes);

  for (uint64_t txid : Z) {
    Jl.insert(txid, GrapheneProtocol::U64ToVec(txid));
  }

  IBLT diff = J - Jl;

  std::set<std::pair<uint64_t, std::vector<uint8_t>>> positive;
  std::set<std::pair<uint64_t, std::vector<uint8_t>>> negative;

  if (!diff.listEntries(positive, negative)) {
    return DecodeStatus::FAIL_FATAL;
  }

  for (const auto &[txid, _] : negative) {
    Z.erase(txid);
  }

  for (const auto &[txid, _] : positive) {
    Z.insert(txid);
  }

  return DecodeStatus::SUCCESS;
}

GrapheneProtocol::DecodeStatus
GrapheneProtocol::TryPingPong(IBLT &first, IBLT &second,
                              std::set<uint64_t> &in_block,
                              std::set<uint64_t> &not_in_block) {

  std::set<std::pair<uint64_t, std::vector<uint8_t>>> positive;
  std::set<std::pair<uint64_t, std::vector<uint8_t>>> negative;

  bool second_ok = second.listEntries(positive, negative);
  bool flag = false;

  if (positive.empty() && negative.empty()) {
    bool first_ok = first.listEntries(positive, negative);
    flag = true;

    if (positive.empty() && negative.empty()) {
      return (first_ok && second_ok) ? DecodeStatus::SUCCESS
                                     : DecodeStatus::FAIL_RECOVERABLE;
    }
  }

  // Our IBLT subtraction convention (sender - receiver):
  //   +1 entries: in sender not receiver = block txs
  //   -1 entries: in receiver not sender = false positives
  for (const auto &[key, val] : positive) {
    in_block.insert(key);
    second.erase(key, val);
    first.erase(key, val);
  }
  for (const auto &[key, val] : negative) {
    not_in_block.insert(key);
    second.insert(key, val);
    first.insert(key, val);
  }

  if (flag)
    return TryPingPong(first, second, in_block, not_in_block);
  else
    return TryPingPong(second, first, in_block, not_in_block);
}

nlohmann::json GrapheneProtocol::SerializeBloomFilter(const bloom_filter &bf) {
  nlohmann::json j;
  j["table_size"] = static_cast<uint64_t>(bf.size());
  j["salt_count"] = static_cast<uint32_t>(bf.hash_count());
  j["random_seed"] = static_cast<uint64_t>(bf.random_seed());

  const auto &tbl = bf.bit_table();
  std::string hex;
  hex.reserve(tbl.size() * 2);
  static const char *hex_chars = "0123456789abcdef";
  for (unsigned char c : tbl) {
    hex.push_back(hex_chars[c >> 4]);
    hex.push_back(hex_chars[c & 0x0f]);
  }
  j["bit_table"] = hex;

  return j;
}

bloom_filter GrapheneProtocol::DeserializeBloomFilter(const nlohmann::json &j) {
  uint64_t table_size = j["table_size"].get<uint64_t>();
  uint32_t salt_count = j["salt_count"].get<uint32_t>();
  uint64_t random_seed = j["random_seed"].get<uint64_t>();
  std::string hex = j["bit_table"].get<std::string>();

  std::vector<unsigned char> bit_table;
  bit_table.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    auto h2 = [](char c) -> unsigned char {
      if (c >= '0' && c <= '9')
        return static_cast<unsigned char>(c - '0');
      return static_cast<unsigned char>(10 + (c - 'a'));
    };
    bit_table.push_back(
        static_cast<unsigned char>((h2(hex[i]) << 4) | h2(hex[i + 1])));
  }

  return bloom_filter(table_size, salt_count, random_seed, bit_table);
}

nlohmann::json GrapheneProtocol::SerializeIBLT(const IBLT &iblt) {
  nlohmann::json j;
  j["value_size"] = static_cast<uint64_t>(iblt.valueSize);
  j["num_hashes"] = static_cast<uint64_t>(iblt.numHashes);

  auto entries = iblt.getHashTable();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto &e : entries) {
    nlohmann::json entry;
    entry["count"] = e.count;
    entry["key_sum"] = static_cast<double>(e.keySum);
    entry["key_check"] = static_cast<uint64_t>(e.keyCheck);

    std::string hex;
    hex.reserve(e.valueSum.size() * 2);
    static const char *hex_chars = "0123456789abcdef";
    for (unsigned char c : e.valueSum) {
      hex.push_back(hex_chars[c >> 4]);
      hex.push_back(hex_chars[c & 0x0f]);
    }
    entry["value_sum"] = hex;
    arr.push_back(entry);
  }
  j["entries"] = arr;
  return j;
}

IBLT GrapheneProtocol::DeserializeIBLT(const nlohmann::json &j) {
  size_t value_size = j["value_size"].get<uint64_t>();
  size_t num_hashes = j["num_hashes"].get<uint64_t>();

  std::vector<IBLT::Entry> entries;
  for (const auto &e : j["entries"]) {
    IBLT::Entry entry;
    entry.count = e["count"].get<int32_t>();
    entry.keySum = static_cast<uint64_t>(e["key_sum"].get<double>());
    entry.keyCheck = e["key_check"].get<uint32_t>();

    std::string hex = e["value_sum"].get<std::string>();
    entry.valueSum.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
      auto h2 = [](char c) -> unsigned char {
        if (c >= '0' && c <= '9')
          return static_cast<unsigned char>(c - '0');
        return static_cast<unsigned char>(10 + (c - 'a'));
      };
      entry.valueSum.push_back(
          static_cast<unsigned char>((h2(hex[i]) << 4) | h2(hex[i + 1])));
    }
    entries.push_back(entry);
  }

  return IBLT(value_size, num_hashes, entries);
}

// ============================================================================
//  High-level protocol entry points
// ============================================================================

IncomingBlockResult GrapheneProtocol::ProcessIncomingBlock(
    const nlohmann::json &data,
    const std::vector<std::pair<uint32_t, uint64_t>> &mempool,
    size_t mempool_size) {

  IncomingBlockResult result;

  bloom_filter bf = DeserializeBloomFilter(data["bloom_filter"]);
  IBLT iblt = DeserializeIBLT(data["iblt"]);
  size_t tx_count = data["tx_count"].get<size_t>();

  // Try Protocol 1
  std::set<Transaction> recovered_txs;
  auto status = ReconstructBlock(bf, iblt, tx_count, mempool, recovered_txs);

  if (status == DecodeStatus::SUCCESS) {
    Block block;
    block.header.block_id = data["block_id"].get<uint64_t>();
    block.header.miner_id = data["miner_id"].get<uint64_t>();
    block.header.time_created = data["time_created"].get<double>();
    if (data.contains("parent_hashes")) {
      for (auto &ph : data["parent_hashes"])
        block.header.parent_hashes.push_back(ph.get<uint64_t>());
    }
    block.transactions = recovered_txs;
    block.size_in_bytes = block.GetTotalSize();

    uint64_t recovered_checksum = 0;
    for (const auto &tx : recovered_txs) {
      recovered_checksum ^= tx.tx_id;
    }

    __ASSERT__(
        recovered_checksum == data["tx_checksum"].get<uint64_t>(),
        "tx_checksum received from peer is different from reconstructed on "
        "graphene");

    result.success = true;
    result.block = std::move(block);
    return result;
  }

  // Protocol 1 failed — build recovery state
  auto &rstate = result.recovery_state;
  rstate.tx_count = tx_count;
  rstate.header.block_id = data["block_id"].get<uint64_t>();
  rstate.header.miner_id = data["miner_id"].get<uint64_t>();
  rstate.header.time_created = data["time_created"].get<double>();
  if (data.contains("parent_hashes")) {
    for (auto &ph : data["parent_hashes"])
      rstate.header.parent_hashes.push_back(ph.get<uint64_t>());
  }

  for (const auto &entry : mempool) {
    uint64_t txid = entry.second;
    if (bf.contains(txid)) {
      rstate.candidate_ids.insert(txid);
    }
  }

  // Store Protocol 1 diff IBLT for potential ping-pong recovery
  {
    IBLT recv_iblt(iblt.hashTableSize(), IBLT_VALUE_SIZE, 1.0f, iblt.numHashes);
    for (uint64_t txid : rstate.candidate_ids) {
      recv_iblt.insert(txid, U64ToVec(txid));
    }
    rstate.diff1 = iblt - recv_iblt;
  }

  // Build recovery bloom and request
  std::vector<uint64_t> Z(rstate.candidate_ids.begin(),
                          rstate.candidate_ids.end());
  bloom_filter rbf;
  int b, y_star;
  BuildRecoveryBloom(Z, mempool_size, tx_count, data["fpr"], rbf, b, y_star);

  nlohmann::json req;
  req["block_hash"] = data["block_hash"];
  req["b"] = b;
  req["y_star"] = y_star;
  req["receiver_bloom"] = SerializeBloomFilter(rbf);
  result.recovery_request = std::move(req);

  rstate.waiting_protocol2 = true;
  result.success = false;
  return result;
}

RecoveryResponseResult
GrapheneProtocol::ProcessRecoveryResponse(const nlohmann::json &data,
                                          const GrapheneState &state) {

  RecoveryResponseResult result;

  size_t tx_count = state.tx_count;
  std::set<uint64_t> Z = state.candidate_ids;

  DecodeStatus status = ReconstructRecoveryBlock(data, Z);

  if (status == DecodeStatus::SUCCESS) {
    // Verify checksum, then return txid set (caller builds Block with fee info)
    uint64_t recovered_checksum = 0;
    for (auto txid : Z) {
      recovered_checksum ^= txid;
    }

    __ASSERT__(
        recovered_checksum == data["tx_checksum"].get<uint64_t>(),
        "tx_checksum received from peer is different from reconstructed on "
        "graphene protocol 2");

    result.success = true;
    result.block_txids = std::move(Z);
    return result;
  }

  // Protocol 2 IBLT decoding failed — try ping-pong recovery
  if (state.diff1.has_value()) {
    // Build diff2 IBLT = J - Jl (same as inside ReconstructRecoveryBlock)
    IBLT J = DeserializeIBLT(data["iblt"]);
    IBLT Jl(J.hashTableSize(), IBLT_VALUE_SIZE, 1.0f, J.numHashes);
    for (uint64_t txid : Z) {
      Jl.insert(txid, U64ToVec(txid));
    }
    IBLT diff2 = J - Jl;

    IBLT diff1_copy = state.diff1.value();
    std::set<uint64_t> in_block, not_in_block;
    auto pp_status = TryPingPong(diff1_copy, diff2, in_block, not_in_block);

    if (pp_status == DecodeStatus::SUCCESS) {
      std::set<uint64_t> block_txids = Z;
      for (auto txid : in_block)
        block_txids.insert(txid);
      for (auto txid : not_in_block)
        block_txids.erase(txid);

      if (block_txids.size() == tx_count) {
        uint64_t recovered_checksum = 0;
        for (auto txid : block_txids)
          recovered_checksum ^= txid;

        if (recovered_checksum == data["tx_checksum"].get<uint64_t>()) {
          result.success = true;
          result.block_txids = std::move(block_txids);
          return result;
        }
      }
    }
  }

  result.success = false;
  return result;
}
