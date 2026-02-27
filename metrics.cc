#include "metrics.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sys/stat.h>

uint32_t MetricsCollector::s_rank = 0;
bool MetricsCollector::s_verbose = false;
uint32_t MetricsCollector::s_totalNodes = 0;
bool MetricsCollector::s_hasConfig = false;

SimulationConfig MetricsCollector::s_config{};

std::vector<BlockMinedEvent> MetricsCollector::s_blocksMined;
std::vector<BlockReceivedEvent> MetricsCollector::s_blocksReceived;
std::vector<BlockOrphanedEvent> MetricsCollector::s_blocksOrphaned;
std::vector<BlockUnorphanedEvent> MetricsCollector::s_blocksUnorphaned;
std::vector<BlockColoredEvent> MetricsCollector::s_blocksColored;
std::vector<DagSnapshotEvent> MetricsCollector::s_dagSnapshots;
std::vector<RedundantMsgEvent> MetricsCollector::s_redundantMsgs;
std::vector<MsgEvent> MetricsCollector::s_msgs;
std::vector<TxGeneratedEvent> MetricsCollector::s_txsGenerated;
std::vector<TxReceivedEvent> MetricsCollector::s_txsReceived;
std::vector<TxConfirmedEvent> MetricsCollector::s_txsConfirmed;
std::vector<InvSentEvent> MetricsCollector::s_invsSent;
std::vector<BlockCoverageEvent> MetricsCollector::s_blockCoverage;

std::map<uint64_t, std::set<uint32_t>> MetricsCollector::s_blockReceivers;
std::map<uint64_t, double> MetricsCollector::s_orphanedAt;

void MetricsCollector::SetRank(uint32_t rank) { s_rank = rank; }
void MetricsCollector::SetVerbose(bool verbose) { s_verbose = verbose; }
void MetricsCollector::SetTotalNodes(uint32_t total) { s_totalNodes = total; }

void MetricsCollector::SetConfig(const SimulationConfig &config) {
  s_config = config;
  s_hasConfig = true;
}

void MetricsCollector::RecordBlockMined(uint32_t miner_id, uint64_t block_id,
                                        uint32_t num_parents,
                                        uint64_t dag_width, uint32_t num_txs,
                                        uint32_t block_size_bytes,
                                        double sim_time) {
  s_blocksMined.push_back({sim_time, miner_id, block_id, num_parents, dag_width,
                           num_txs, block_size_bytes});
  s_blockReceivers[block_id].insert(miner_id);
  if (s_verbose)
    std::cout << "[METRIC] BLOCK_MINED     t=" << sim_time
              << " miner=" << miner_id << " block=" << block_id
              << " parents=" << num_parents << " width=" << dag_width
              << " txs=" << num_txs << "\n";
}

void MetricsCollector::RecordBlockReceived(uint32_t node_id, uint64_t block_id,
                                           uint32_t miner_id,
                                           double creation_time,
                                           double sim_time, std::string from_ip,
                                           bool was_requested) {
  double delay = sim_time - creation_time;
  s_blocksReceived.push_back({sim_time, node_id, block_id, miner_id,
                              creation_time, delay, from_ip, was_requested});
  s_blockReceivers[block_id].insert(node_id);
  if (s_verbose)
    std::cout << "[METRIC] BLOCK_RECEIVED  t=" << sim_time
              << " node=" << node_id << " block=" << block_id
              << " delay=" << delay << "s from=" << from_ip << "\n";
}

void MetricsCollector::RecordBlockOrphaned(
    uint32_t node_id, uint64_t block_id,
    const std::vector<uint64_t> &missing_parent_ids, double sim_time) {
  s_blocksOrphaned.push_back({sim_time, node_id, block_id, missing_parent_ids});
  s_orphanedAt[block_id] = sim_time;
  if (s_verbose) {
    std::cout << "[METRIC] BLOCK_ORPHANED  t=" << sim_time
              << " node=" << node_id << " block=" << block_id
              << " missing=" << missing_parent_ids.size() << " parents\n";
  }
}

void MetricsCollector::RecordBlockUnorphaned(uint32_t node_id,
                                             uint64_t block_id,
                                             double sim_time) {
  double duration = 0.0;
  auto it = s_orphanedAt.find(block_id);
  if (it != s_orphanedAt.end()) {
    duration = sim_time - it->second;
    s_orphanedAt.erase(it);
  }
  s_blocksUnorphaned.push_back({sim_time, node_id, block_id, duration});
  if (s_verbose)
    std::cout << "[METRIC] BLOCK_UNORPHANED t=" << sim_time
              << " node=" << node_id << " block=" << block_id
              << " duration=" << duration << "s\n";
}

void MetricsCollector::RecordBlockColored(uint32_t node_id, uint64_t block_id,
                                          bool is_blue, uint64_t blue_score,
                                          uint64_t blue_set_size,
                                          uint64_t selected_parent,
                                          double sim_time) {
  s_blocksColored.push_back({sim_time, node_id, block_id, is_blue, blue_score,
                             blue_set_size, selected_parent});
  if (s_verbose)
    std::cout << "[METRIC] BLOCK_COLORED   t=" << sim_time
              << " node=" << node_id << " block=" << block_id
              << " blue=" << is_blue << " score=" << blue_score << "\n";
}

void MetricsCollector::RecordDagSnapshot(uint32_t node_id,
                                         uint64_t total_blocks,
                                         uint64_t blue_blocks,
                                         uint64_t red_blocks,
                                         uint64_t orphan_blocks,
                                         uint64_t dag_width, double sim_time) {
  double ratio = total_blocks > 0 ? static_cast<double>(blue_blocks) /
                                        static_cast<double>(total_blocks)
                                  : 0.0;
  s_dagSnapshots.push_back({sim_time, node_id, total_blocks, blue_blocks,
                            red_blocks, orphan_blocks, dag_width, ratio});
  if (s_verbose)
    std::cout << "[METRIC] DAG_SNAPSHOT    t=" << sim_time
              << " node=" << node_id << " total=" << total_blocks
              << " blue=" << blue_blocks << " ratio=" << ratio
              << " width=" << dag_width << "\n";
}

void MetricsCollector::RecordRedundantMsg(uint32_t node_id, uint64_t block_id,
                                          std::string msg_type,
                                          std::string from_ip, uint64_t bytes,
                                          double sim_time) {
  s_redundantMsgs.push_back(
      {sim_time, node_id, block_id, msg_type, from_ip, bytes});
  if (s_verbose)
    std::cout << "[METRIC] REDUNDANT_MSG   t=" << sim_time
              << " node=" << node_id << " block=" << block_id
              << " type=" << msg_type << " bytes=" << bytes
              << " from=" << from_ip << "\n";
}

void MetricsCollector::RecordMsg(uint32_t node_id, uint64_t item_id,
                                 std::string msg_type, std::string peer_ip,
                                 uint64_t bytes, bool is_send,
                                 double sim_time) {
  s_msgs.push_back(
      {sim_time, node_id, item_id, msg_type, peer_ip, bytes, is_send});
  if (s_verbose)
    std::cout << "[METRIC] MSG             t=" << sim_time
              << " node=" << node_id << " " << (is_send ? "SEND" : "RECV")
              << " type=" << msg_type << " bytes=" << bytes
              << " peer=" << peer_ip << "\n";
}

void MetricsCollector::RecordTxGenerated(uint32_t node_id, uint64_t tx_id,
                                         uint32_t fee, double sim_time) {
  s_txsGenerated.push_back({sim_time, node_id, tx_id, fee});
  if (s_verbose)
    std::cout << "[METRIC] TX_GENERATED    t=" << sim_time
              << " node=" << node_id << " tx=" << tx_id << " fee=" << fee
              << "\n";
}

void MetricsCollector::RecordTxReceived(uint32_t node_id, uint64_t tx_id,
                                        uint32_t fee, double propagation_delay,
                                        std::string from_ip, double sim_time) {
  s_txsReceived.push_back(
      {sim_time, node_id, tx_id, fee, propagation_delay, from_ip});
  if (s_verbose)
    std::cout << "[METRIC] TX_RECEIVED     t=" << sim_time
              << " node=" << node_id << " tx=" << tx_id
              << " delay=" << propagation_delay << "s\n";
}

void MetricsCollector::RecordTxConfirmed(uint32_t miner_id, uint64_t tx_id,
                                         uint64_t block_id, uint32_t fee,
                                         double sim_time) {
  s_txsConfirmed.push_back({sim_time, miner_id, tx_id, block_id, fee});
  if (s_verbose)
    std::cout << "[METRIC] TX_CONFIRMED    t=" << sim_time
              << " miner=" << miner_id << " tx=" << tx_id
              << " block=" << block_id << "\n";
}

void MetricsCollector::RecordInvSent(uint32_t node_id, std::string to_ip,
                                     std::string inv_type, uint64_t item_id,
                                     uint64_t bytes, double sim_time) {
  s_invsSent.push_back({sim_time, node_id, to_ip, inv_type, item_id, bytes});
  if (s_verbose)
    std::cout << "[METRIC] INV_SENT        t=" << sim_time
              << " node=" << node_id << " to=" << to_ip << " type=" << inv_type
              << " id=" << item_id << " bytes=" << bytes << "\n";
}

void MetricsCollector::RecordBlockCoverageSnapshot(uint64_t block_id,
                                                   double creation_time,
                                                   double sim_time) {
  auto reached = static_cast<uint32_t>(s_blockReceivers[block_id].size());
  uint32_t total = s_totalNodes > 0 ? s_totalNodes : reached;
  double ratio = total > 0 ? static_cast<double>(reached) / total : 0.0;
  double elapsed = sim_time - creation_time;
  s_blockCoverage.push_back(
      {sim_time, block_id, creation_time, elapsed, reached, total, ratio});
  if (s_verbose)
    std::cout << "[METRIC] BLOCK_COVERAGE  t=" << sim_time
              << " block=" << block_id << " reached=" << reached << "/" << total
              << " (" << ratio * 100.0 << "%)"
              << " elapsed=" << elapsed << "s\n";
}

std::string MetricsCollector::FilePath(const std::string &dir,
                                       const std::string &name) {
  return dir + "/rank" + std::to_string(s_rank) + "_" + name + ".csv";
}

void MetricsCollector::DumpBlocksMined(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,miner_id,block_id,num_parents,dag_width,num_txs,block_size_"
       "bytes\n";
  for (const auto &e : s_blocksMined)
    f << e.sim_time << "," << e.miner_id << "," << e.block_id << ","
      << e.num_parents << "," << e.dag_width << "," << e.num_txs << ","
      << e.block_size << "\n";
}

void MetricsCollector::DumpBlocksReceived(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,block_id,miner_id,creation_time,"
       "propagation_delay_s,from_ip,was_requested\n";
  for (const auto &e : s_blocksReceived)
    f << e.sim_time << "," << e.node_id << "," << e.block_id << ","
      << e.miner_id << "," << e.creation_time << "," << e.propagation_delay
      << "," << e.from_ip << "," << (e.was_requested ? 1 : 0) << "\n";
}

void MetricsCollector::DumpBlocksOrphaned(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,block_id,missing_parent_ids\n";
  for (const auto &e : s_blocksOrphaned) {
    f << e.sim_time << "," << e.node_id << "," << e.block_id << ",\"";
    for (size_t i = 0; i < e.missing_parent_ids.size(); ++i) {
      if (i > 0)
        f << ";";
      f << e.missing_parent_ids[i];
    }
    f << "\"\n";
  }
}

void MetricsCollector::DumpBlocksUnorphaned(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,block_id,orphan_duration_s\n";
  for (const auto &e : s_blocksUnorphaned)
    f << e.sim_time << "," << e.node_id << "," << e.block_id << ","
      << e.orphan_duration << "\n";
}

void MetricsCollector::DumpBlocksColored(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,block_id,is_blue,blue_score,blue_set_size,selected_"
       "parent\n";
  for (const auto &e : s_blocksColored)
    f << e.sim_time << "," << e.node_id << "," << e.block_id << ","
      << (e.is_blue ? 1 : 0) << "," << e.blue_score << "," << e.blue_set_size
      << "," << e.selected_parent << "\n";
}

void MetricsCollector::DumpDagSnapshots(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,total_blocks,blue_blocks,red_blocks,"
       "orphan_blocks,dag_width,blue_ratio\n";
  for (const auto &e : s_dagSnapshots)
    f << e.sim_time << "," << e.node_id << "," << e.total_blocks << ","
      << e.blue_blocks << "," << e.red_blocks << "," << e.orphan_blocks << ","
      << e.dag_width << "," << e.blue_ratio << "\n";
}

void MetricsCollector::DumpRedundantMsgs(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,block_id,msg_type,from_ip,bytes\n";
  for (const auto &e : s_redundantMsgs)
    f << e.sim_time << "," << e.node_id << "," << e.block_id << ","
      << e.msg_type << "," << e.from_ip << "," << e.bytes << "\n";
}

void MetricsCollector::DumpMsgs(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,item_id,msg_type,peer_ip,bytes,direction\n";
  for (const auto &e : s_msgs)
    f << e.sim_time << "," << e.node_id << "," << e.item_id << "," << e.msg_type
      << "," << e.peer_ip << "," << e.bytes << ","
      << (e.is_send ? "send" : "recv") << "\n";
}

void MetricsCollector::DumpTxsGenerated(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,tx_id,fee\n";
  for (const auto &e : s_txsGenerated)
    f << e.sim_time << "," << e.node_id << "," << e.tx_id << "," << e.fee
      << "\n";
}

void MetricsCollector::DumpTxsReceived(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,tx_id,fee,propagation_delay_s,from_ip\n";
  for (const auto &e : s_txsReceived)
    f << e.sim_time << "," << e.node_id << "," << e.tx_id << "," << e.fee << ","
      << e.propagation_delay << "," << e.from_ip << "\n";
}

void MetricsCollector::DumpTxsConfirmed(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,miner_id,tx_id,block_id,fee\n";
  for (const auto &e : s_txsConfirmed)
    f << e.sim_time << "," << e.miner_id << "," << e.tx_id << "," << e.block_id
      << "," << e.fee << "\n";
}

void MetricsCollector::DumpInvsSent(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,node_id,to_ip,inv_type,item_id,bytes\n";
  for (const auto &e : s_invsSent)
    f << e.sim_time << "," << e.node_id << "," << e.to_ip << "," << e.inv_type
      << "," << e.item_id << "," << e.bytes << "\n";
}

void MetricsCollector::DumpBlockCoverage(const std::string &path) {
  std::ofstream f(path);
  f << "sim_time,block_id,block_creation_time,elapsed_s,"
       "nodes_reached,total_nodes,coverage_ratio\n";
  for (const auto &e : s_blockCoverage)
    f << e.sim_time << "," << e.block_id << "," << e.block_creation_time << ","
      << e.elapsed << "," << e.nodes_reached << "," << e.total_nodes << ","
      << e.coverage_ratio << "\n";
}

void MetricsCollector::DumpConfig(const std::string &path) {
  std::ofstream f(path);
  f << "key,value\n";
  f << "scenario_name," << s_config.scenario_name << "\n";
  f << "ghostdag_k," << s_config.ghostdag_k << "\n";
  f << "lambda," << s_config.lambda << "\n";
  f << "tau," << s_config.tau << "\n";
  f << "total_nodes," << s_config.total_nodes << "\n";
  f << "miners," << s_config.miners << "\n";
  f << "txs_per_block," << s_config.txs_per_block << "\n";
  f << "mempool_size," << s_config.mempool_size << "\n";
  f << "tx_fee_lambda," << s_config.tx_fee_lambda << "\n";
  f << "tx_gen_interval," << s_config.tx_gen_interval << "\n";
  f << "sim_duration_min," << s_config.sim_duration_minutes << "\n";
}

void MetricsCollector::Dump(const std::string &output_dir) {
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);

  DumpBlocksMined(FilePath(output_dir, "blocks_mined"));
  DumpBlocksReceived(FilePath(output_dir, "blocks_received"));
  DumpBlocksOrphaned(FilePath(output_dir, "blocks_orphaned"));
  DumpBlocksUnorphaned(FilePath(output_dir, "blocks_unorphaned"));
  DumpBlocksColored(FilePath(output_dir, "blocks_colored"));
  DumpDagSnapshots(FilePath(output_dir, "dag_snapshots"));
  DumpRedundantMsgs(FilePath(output_dir, "redundant_msgs"));
  DumpMsgs(FilePath(output_dir, "msgs"));
  DumpTxsGenerated(FilePath(output_dir, "txs_generated"));
  DumpTxsReceived(FilePath(output_dir, "txs_received"));
  DumpTxsConfirmed(FilePath(output_dir, "txs_confirmed"));
  DumpInvsSent(FilePath(output_dir, "invs_sent"));
  DumpBlockCoverage(FilePath(output_dir, "block_coverage"));
  if (s_hasConfig)
    DumpConfig(FilePath(output_dir, "config"));

  std::cout << "[Metrics] rank=" << s_rank << " wrote "
            << (s_hasConfig ? 14 : 13) << " CSV files to " << output_dir
            << "/\n";
}

void MetricsCollector::PrintSummary() {
  std::cout << "\n";
  std::cout << "╔══════════════════════════════════════════════════════╗\n";
  std::cout << "║        GHOSTDAG Simulation Metrics Summary           ║\n";
  std::cout << "║                     rank " << std::setw(2) << s_rank
            << "                           ║\n";
  if (s_hasConfig) {
    std::cout << "║  scenario: " << std::left << std::setw(42)
              << s_config.scenario_name << "║\n";
  }
  std::cout << "╚══════════════════════════════════════════════════════╝\n";
  std::cout << std::right << std::fixed;

  std::cout << "\n── Block Production ──────────────────────────────────\n";
  std::cout << "  Blocks mined:              " << s_blocksMined.size() << "\n";
  if (!s_blocksMined.empty()) {
    double avg_parents = 0;
    double avg_width = 0;
    double total_bytes = 0;
    for (const auto &e : s_blocksMined) {
      avg_parents += e.num_parents;
      avg_width += e.dag_width;
      total_bytes += e.block_size;
    }
    size_t n = s_blocksMined.size();
    std::cout << "  Avg parents per block:     " << std::setprecision(2)
              << avg_parents / n << "\n";
    std::cout << "  Avg DAG width at mining:   " << avg_width / n << "\n";
    std::cout << "  Avg block size:            " << std::setprecision(0)
              << total_bytes / n << " bytes\n";
  }

  std::cout << "\n── Block Propagation ─────────────────────────────────\n";
  std::cout << "  Block receptions logged:   " << s_blocksReceived.size()
            << "\n";
  if (!s_blocksReceived.empty()) {
    std::vector<double> delays;
    delays.reserve(s_blocksReceived.size());
    for (const auto &e : s_blocksReceived)
      delays.push_back(e.propagation_delay);
    std::sort(delays.begin(), delays.end());
    double sum = std::accumulate(delays.begin(), delays.end(), 0.0);
    double mean = sum / delays.size();
    double sq = 0;
    for (double d : delays)
      sq += (d - mean) * (d - mean);
    std::cout << std::setprecision(4);
    std::cout << "  Mean delay:                " << mean << " s\n";
    std::cout << "  Std dev:                   "
              << std::sqrt(sq / delays.size()) << " s\n";
    std::cout << "  Median (p50):              "
              << delays[delays.size() * 50 / 100] << " s\n";
    std::cout << "  p95:                       "
              << delays[delays.size() * 95 / 100] << " s\n";
    std::cout << "  p99:                       "
              << delays[delays.size() * 99 / 100] << " s\n";
    std::cout << "  Max:                       " << delays.back() << " s\n";
  }

  std::cout << "\n── Block Coverage ────────────────────────────────────\n";
  if (!s_blockCoverage.empty()) {
    std::vector<double> ratios_at_50pct;
    std::vector<double> ratios_at_100pct;
    for (const auto &e : s_blockCoverage) {
      if (e.coverage_ratio >= 0.999)
        ratios_at_100pct.push_back(e.elapsed);
      if (e.coverage_ratio >= 0.499 && e.coverage_ratio < 0.501)
        ratios_at_50pct.push_back(e.elapsed);
    }
    std::cout << "  Coverage snapshots:        " << s_blockCoverage.size()
              << "\n";
    if (!ratios_at_100pct.empty()) {
      double sum = std::accumulate(ratios_at_100pct.begin(),
                                   ratios_at_100pct.end(), 0.0);
      std::cout << "  Avg time to 100% coverage: " << std::setprecision(4)
                << sum / ratios_at_100pct.size() << " s\n";
    }
  } else {
    std::cout << "  (no coverage snapshots — call METRIC_BLOCK_COVERAGE)\n";
  }

  std::cout << "\n── GHOSTDAG Blue/Red Classification ─────────────────\n";
  if (!s_dagSnapshots.empty()) {
    std::map<uint32_t, DagSnapshotEvent> latest;
    for (const auto &e : s_dagSnapshots)
      latest[e.node_id] = e;
    uint64_t tb = 0;
    uint64_t tbl = 0;
    uint64_t tr = 0;
    uint64_t to = 0;
    for (const auto &[nid, e] : latest) {
      tb += e.total_blocks;
      tbl += e.blue_blocks;
      tr += e.red_blocks;
      to += e.orphan_blocks;
    }
    auto n = static_cast<uint32_t>(latest.size());
    if (n > 0 && tb > 0) {
      std::cout << "  Nodes reporting:           " << n << "\n";
      std::cout << "  Avg total blocks:          " << tb / n << "\n";
      std::cout << "  Avg blue blocks:           " << tbl / n << "\n";
      std::cout << "  Avg red blocks:            " << tr / n << "\n";
      std::cout << "  Avg orphan blocks:         " << to / n << "\n";
      std::cout << "  Network blue ratio:        " << std::setprecision(4)
                << static_cast<double>(tbl) / tb << "\n";
      std::cout << "  Network orphan rate:       "
                << static_cast<double>(to) / tb << "\n";
    }
  }

  std::cout << "\n── Orphan Lifecycle ──────────────────────────────────\n";
  std::cout << "  Orphan events:             " << s_blocksOrphaned.size()
            << "\n";
  std::cout << "  Unorphaned events:         " << s_blocksUnorphaned.size()
            << "\n";
  if (!s_blocksUnorphaned.empty()) {
    double sum = 0;
    for (const auto &e : s_blocksUnorphaned)
      sum += e.orphan_duration;
    std::cout << "  Avg orphan duration:       " << std::setprecision(4)
              << sum / s_blocksUnorphaned.size() << " s\n";
  }
  if (!s_blocksOrphaned.empty()) {
    size_t still_orphaned = s_blocksOrphaned.size() - s_blocksUnorphaned.size();
    std::cout << "  Still orphaned at end:     " << still_orphaned << "\n";
  }

  std::cout << "\n── Network Redundancy & Bandwidth ───────────────────\n";
  uint64_t total_bytes_sent = 0;
  uint64_t redundant_bytes = 0;
  for (const auto &e : s_msgs)
    if (e.is_send)
      total_bytes_sent += e.bytes;
  for (const auto &e : s_redundantMsgs)
    redundant_bytes += e.bytes;
  std::cout << "  Total bytes sent:          " << total_bytes_sent << "\n";
  std::cout << "  Redundant messages:        " << s_redundantMsgs.size()
            << "\n";
  std::cout << "  Redundant bytes:           " << redundant_bytes << "\n";
  if (total_bytes_sent > 0) {
    std::cout << "  Redundancy ratio (bytes):  " << std::setprecision(4)
              << static_cast<double>(redundant_bytes) / total_bytes_sent
              << "\n";
  }
  if (!s_redundantMsgs.empty() && !s_blocksReceived.empty()) {
    uint64_t inv_r = 0;
    uint64_t blk_r = 0;
    for (const auto &e : s_redundantMsgs) {
      if (e.msg_type == "INV_RELAY_BLOCK")
        ++inv_r;
      else if (e.msg_type == "BLOCK")
        ++blk_r;
    }
    std::cout << "  Redundant INV msgs:        " << inv_r << "\n";
    std::cout << "  Redundant BLOCK msgs:      " << blk_r << "\n";
  }

  std::cout << "\n── INV Traffic ───────────────────────────────────────\n";
  uint64_t block_inv_count = 0;
  uint64_t tx_inv_count = 0;
  uint64_t block_inv_bytes = 0;
  uint64_t tx_inv_bytes = 0;
  for (const auto &e : s_invsSent) {
    if (e.inv_type == "BLOCK") {
      ++block_inv_count;
      block_inv_bytes += e.bytes;
    } else if (e.inv_type == "TX") {
      ++tx_inv_count;
      tx_inv_bytes += e.bytes;
    }
  }
  std::cout << "  Block INVs sent:           " << block_inv_count << " ("
            << block_inv_bytes << " bytes)\n";
  std::cout << "  Tx INVs sent:              " << tx_inv_count << " ("
            << tx_inv_bytes << " bytes)\n";

  std::cout << "\n── Transactions ──────────────────────────────────────\n";
  std::cout << "  Txs generated:             " << s_txsGenerated.size() << "\n";
  std::cout << "  Txs received (events):     " << s_txsReceived.size() << "\n";
  std::cout << "  Txs confirmed:             " << s_txsConfirmed.size() << "\n";
  if (!s_txsGenerated.empty())
    std::cout << "  Confirmation rate:         " << std::setprecision(4)
              << static_cast<double>(s_txsConfirmed.size()) /
                     s_txsGenerated.size()
              << "\n";
  if (!s_txsReceived.empty()) {
    std::vector<double> td;
    for (const auto &e : s_txsReceived)
      if (e.propagation_delay > 0)
        td.push_back(e.propagation_delay);
    if (!td.empty()) {
      std::sort(td.begin(), td.end());
      double sum = std::accumulate(td.begin(), td.end(), 0.0);
      std::cout << "  Mean tx propagation:       " << sum / td.size() << " s\n";
      std::cout << "  Tx p95 propagation:        " << td[td.size() * 95 / 100]
                << " s\n";
    }
  }

  std::cout << "\n══════════════════════════════════════════════════════\n\n";
}

void MetricsCollector::Reset() {
  s_blocksMined.clear();
  s_blocksReceived.clear();
  s_blocksOrphaned.clear();
  s_blocksUnorphaned.clear();
  s_blocksColored.clear();
  s_dagSnapshots.clear();
  s_redundantMsgs.clear();
  s_msgs.clear();
  s_txsGenerated.clear();
  s_txsReceived.clear();
  s_txsConfirmed.clear();
  s_invsSent.clear();
  s_blockCoverage.clear();
  s_blockReceivers.clear();
  s_orphanedAt.clear();
  s_hasConfig = false;
}
