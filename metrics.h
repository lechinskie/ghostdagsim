#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct BlockMinedEvent {
  double sim_time;
  uint32_t miner_id;
  uint64_t block_id;
  uint32_t num_parents;
  uint64_t dag_width;
  uint32_t num_txs;
  uint32_t block_size;
};

struct BlockReceivedEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t block_id;
  uint32_t miner_id;
  double creation_time;
  double propagation_delay;
  std::string from_ip;
  bool was_requested;
};

struct BlockOrphanedEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t block_id;
  std::vector<uint64_t> missing_parent_ids;
};

struct BlockUnorphanedEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t block_id;
  double orphan_duration;
};

struct BlockColoredEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t block_id;
  bool is_blue;
  uint64_t blue_score;
  uint64_t blue_set_size;
  uint64_t selected_parent;
};

struct DagSnapshotEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t total_blocks;
  uint64_t blue_blocks;
  uint64_t red_blocks;
  uint64_t orphan_blocks;
  uint64_t dag_width;
  double blue_ratio;
};

struct RedundantMsgEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t block_id;
  std::string msg_type;
  std::string from_ip;
  uint64_t bytes;
};

struct MsgEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t item_id;
  std::string msg_type;
  std::string peer_ip;
  uint64_t bytes;
  bool is_send;
};

struct TxGeneratedEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t tx_id;
  uint32_t fee;
};

struct TxReceivedEvent {
  double sim_time;
  uint32_t node_id;
  uint64_t tx_id;
  uint32_t fee;
  double propagation_delay;
  std::string from_ip;
};

struct TxConfirmedEvent {
  double sim_time;
  uint32_t miner_id;
  uint64_t tx_id;
  uint64_t block_id;
  uint32_t fee;
};

struct InvSentEvent {
  double sim_time;
  uint32_t node_id;
  std::string to_ip;
  std::string inv_type;
  uint64_t item_id;
  uint64_t bytes;
};

struct BlockCoverageEvent {
  double sim_time;
  uint64_t block_id;
  double block_creation_time;
  double elapsed;
  uint32_t nodes_reached;
  uint32_t total_nodes;
  double coverage_ratio;
};

struct SimulationConfig {
  std::string scenario_name;
  uint32_t ghostdag_k;
  double lambda;
  double tau;
  uint32_t total_nodes;
  uint32_t miners;
  uint32_t txs_per_block;
  uint32_t mempool_size;
  double tx_fee_lambda;
  double tx_gen_interval;
  double sim_duration_minutes;
};

class MetricsCollector {
public:
  static void SetRank(uint32_t rank);
  static void SetVerbose(bool verbose);
  static void SetTotalNodes(uint32_t total);
  static void SetConfig(const SimulationConfig &config);

  static void RecordBlockMined(uint32_t miner_id, uint64_t block_id,
                               uint32_t num_parents, uint64_t dag_width,
                               uint32_t num_txs, uint32_t block_size_bytes,
                               double sim_time);

  static void RecordBlockReceived(uint32_t node_id, uint64_t block_id,
                                  uint32_t miner_id, double creation_time,
                                  double sim_time, std::string from_ip,
                                  bool was_requested = true);

  static void
  RecordBlockOrphaned(uint32_t node_id, uint64_t block_id,
                      const std::vector<uint64_t> &missing_parent_ids,
                      double sim_time);

  static void RecordBlockUnorphaned(uint32_t node_id, uint64_t block_id,
                                    double sim_time);

  static void RecordBlockColored(uint32_t node_id, uint64_t block_id,
                                 bool is_blue, uint64_t blue_score,
                                 uint64_t blue_set_size,
                                 uint64_t selected_parent, double sim_time);

  static void RecordDagSnapshot(uint32_t node_id, uint64_t total_blocks,
                                uint64_t blue_blocks, uint64_t red_blocks,
                                uint64_t orphan_blocks, uint64_t dag_width,
                                double sim_time);

  static void RecordRedundantMsg(uint32_t node_id, uint64_t block_id,
                                 std::string msg_type, std::string from_ip,
                                 uint64_t bytes, double sim_time);

  static void RecordMsg(uint32_t node_id, uint64_t item_id,
                        std::string msg_type, std::string peer_ip,
                        uint64_t bytes, bool is_send, double sim_time);

  static void RecordTxGenerated(uint32_t node_id, uint64_t tx_id, uint32_t fee,
                                double sim_time);

  static void RecordTxReceived(uint32_t node_id, uint64_t tx_id, uint32_t fee,
                               double propagation_delay, std::string from_ip,
                               double sim_time);

  static void RecordTxConfirmed(uint32_t miner_id, uint64_t tx_id,
                                uint64_t block_id, uint32_t fee,
                                double sim_time);

  static void RecordInvSent(uint32_t node_id, std::string to_ip,
                            std::string inv_type, uint64_t item_id,
                            uint64_t bytes, double sim_time);

  static void RecordBlockCoverageSnapshot(uint64_t block_id,
                                          double creation_time,
                                          double sim_time);

  static void Dump(const std::string &output_dir);
  static void PrintSummary();
  static void Reset();

private:
  static void DumpBlocksMined(const std::string &path);
  static void DumpBlocksReceived(const std::string &path);
  static void DumpBlocksOrphaned(const std::string &path);
  static void DumpBlocksUnorphaned(const std::string &path);
  static void DumpBlocksColored(const std::string &path);
  static void DumpDagSnapshots(const std::string &path);
  static void DumpRedundantMsgs(const std::string &path);
  static void DumpMsgs(const std::string &path);
  static void DumpTxsGenerated(const std::string &path);
  static void DumpTxsReceived(const std::string &path);
  static void DumpTxsConfirmed(const std::string &path);
  static void DumpInvsSent(const std::string &path);
  static void DumpBlockCoverage(const std::string &path);
  static void DumpConfig(const std::string &path);

  static std::string FilePath(const std::string &dir, const std::string &name);

  static uint32_t s_rank;
  static bool s_verbose;
  static uint32_t s_totalNodes;
  static SimulationConfig s_config;
  static bool s_hasConfig;

  static std::vector<BlockMinedEvent> s_blocksMined;
  static std::vector<BlockReceivedEvent> s_blocksReceived;
  static std::vector<BlockOrphanedEvent> s_blocksOrphaned;
  static std::vector<BlockUnorphanedEvent> s_blocksUnorphaned;
  static std::vector<BlockColoredEvent> s_blocksColored;
  static std::vector<DagSnapshotEvent> s_dagSnapshots;
  static std::vector<RedundantMsgEvent> s_redundantMsgs;
  static std::vector<MsgEvent> s_msgs;
  static std::vector<TxGeneratedEvent> s_txsGenerated;
  static std::vector<TxReceivedEvent> s_txsReceived;
  static std::vector<TxConfirmedEvent> s_txsConfirmed;
  static std::vector<InvSentEvent> s_invsSent;
  static std::vector<BlockCoverageEvent> s_blockCoverage;

  static std::map<uint64_t, std::set<uint32_t>> s_blockReceivers;
  static std::map<uint64_t, double> s_orphanedAt;
};

#ifdef GHOSTDAGSIM_METRICS

#define METRIC_BLOCK_MINED(miner, bid, nparents, width, ntxs, sz, t)           \
  MetricsCollector::RecordBlockMined(miner, bid, nparents, width, ntxs, sz, t)

#define METRIC_BLOCK_RECEIVED(nid, bid, mid, tcreate, tnow, ip, req)           \
  MetricsCollector::RecordBlockReceived(nid, bid, mid, tcreate, tnow, ip, req)

#define METRIC_BLOCK_ORPHANED(nid, bid, parents_vec, t)                        \
  MetricsCollector::RecordBlockOrphaned(nid, bid, parents_vec, t)

#define METRIC_BLOCK_UNORPHANED(nid, bid, t)                                   \
  MetricsCollector::RecordBlockUnorphaned(nid, bid, t)

#define METRIC_BLOCK_COLORED(nid, bid, blue, score, bsz, sp, t)                \
  MetricsCollector::RecordBlockColored(nid, bid, blue, score, bsz, sp, t)

#define METRIC_DAG_SNAPSHOT(nid, total, blue, red, orph, width, t)             \
  MetricsCollector::RecordDagSnapshot(nid, total, blue, red, orph, width, t)

#define METRIC_REDUNDANT_MSG(nid, bid, mtype, ip, bytes, t)                    \
  MetricsCollector::RecordRedundantMsg(nid, bid, mtype, ip, bytes, t)

#define METRIC_MSG(nid, iid, mtype, ip, bytes, is_send, t)                     \
  MetricsCollector::RecordMsg(nid, iid, mtype, ip, bytes, is_send, t)

#define METRIC_TX_GENERATED(nid, txid, fee, t)                                 \
  MetricsCollector::RecordTxGenerated(nid, txid, fee, t)

#define METRIC_TX_RECEIVED(nid, txid, fee, delay, ip, t)                       \
  MetricsCollector::RecordTxReceived(nid, txid, fee, delay, ip, t)

#define METRIC_TX_CONFIRMED(mid, txid, bid, fee, t)                            \
  MetricsCollector::RecordTxConfirmed(mid, txid, bid, fee, t)

#define METRIC_INV_SENT(nid, ip, itype, iid, bytes, t)                         \
  MetricsCollector::RecordInvSent(nid, ip, itype, iid, bytes, t)

#define METRIC_BLOCK_COVERAGE(bid, tcreate, tnow)                              \
  MetricsCollector::RecordBlockCoverageSnapshot(bid, tcreate, tnow)

#else

#define METRIC_BLOCK_MINED(...)                                                \
  do {                                                                         \
  } while (0)
#define METRIC_BLOCK_RECEIVED(...)                                             \
  do {                                                                         \
  } while (0)
#define METRIC_BLOCK_ORPHANED(...)                                             \
  do {                                                                         \
  } while (0)
#define METRIC_BLOCK_UNORPHANED(...)                                           \
  do {                                                                         \
  } while (0)
#define METRIC_BLOCK_COLORED(...)                                              \
  do {                                                                         \
  } while (0)
#define METRIC_DAG_SNAPSHOT(...)                                               \
  do {                                                                         \
  } while (0)
#define METRIC_REDUNDANT_MSG(...)                                              \
  do {                                                                         \
  } while (0)
#define METRIC_MSG(...)                                                        \
  do {                                                                         \
  } while (0)
#define METRIC_TX_GENERATED(...)                                               \
  do {                                                                         \
  } while (0)
#define METRIC_TX_RECEIVED(...)                                                \
  do {                                                                         \
  } while (0)
#define METRIC_TX_CONFIRMED(...)                                               \
  do {                                                                         \
  } while (0)
#define METRIC_INV_SENT(...)                                                   \
  do {                                                                         \
  } while (0)
#define METRIC_BLOCK_COVERAGE(...)                                             \
  do {                                                                         \
  } while (0)

#endif
