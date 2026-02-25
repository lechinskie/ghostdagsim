#include "metrics.h"

#include "ns3/simulator.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace ns3 {

MetricsCollector::~MetricsCollector() {
  if (m_initialized) {
    Flush();
  }
}

MetricsCollector &MetricsCollector::GetInstance() {
  static MetricsCollector instance;
  return instance;
}

void MetricsCollector::Initialize(MetricBackend backend,
                                  const std::string &output_path,
                                  uint16_t prometheus_port) {
  std::lock_guard<std::mutex> lock(m_mutex);

  m_backend = backend;

  if (backend == MetricBackend::CSV) {
    InitCSVBackend(output_path);
  } else if (backend == MetricBackend::PROMETHEUS) {
#ifdef METRICS_PROMETHEUS
    InitPrometheusBackend(prometheus_port);
#else
    std::cerr << "Prometheus support not compiled. Using CSV backend."
              << std::endl;
    m_backend = MetricBackend::CSV;
    InitCSVBackend(output_path);
#endif
  }

  m_initialized = true;
}

void MetricsCollector::SetSimulationParams(uint32_t num_nodes,
                                           uint32_t num_miners,
                                           uint8_t ghostdag_k, double lambda) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_num_nodes = num_nodes;
  m_num_miners = num_miners;
  m_ghostdag_k = ghostdag_k;
  m_lambda = lambda;
}

void MetricsCollector::InitCSVBackend(const std::string &output_path) {
  m_csv_output_path = output_path.empty() ? "./" : output_path;

  if (!std::filesystem::exists(m_csv_output_path)) {
    std::filesystem::create_directories(m_csv_output_path);
  }

  std::string block_csv_path = m_csv_output_path + "/block_metrics.csv";
  std::string node_csv_path = m_csv_output_path + "/node_metrics.csv";
  std::string network_csv_path = m_csv_output_path + "/network_metrics.csv";
  std::string miner_revenue_path = m_csv_output_path + "/miner_revenue.csv";

  m_block_csv.open(block_csv_path, std::ios::out | std::ios::trunc);
  m_block_csv
      << "block_id,miner_id,time_created,time_first_received,"
         "time_last_received,first_receiver,total_receivers,total_bytes,"
         "redundant_bytes,num_txs,total_fees,num_parents,is_orphan,"
         "is_resolved,propagation_latency\n";

  m_node_csv.open(node_csv_path, std::ios::out | std::ios::trunc);
  m_node_csv << "node_id,blocks_mined,blocks_received,blocks_orphaned,"
                "blocks_resolved,txs_generated,txs_received,dag_size,dag_tips,"
                "mempool_size,bytes_sent,bytes_received,redundant_sent,"
                "redundant_recv,fees_earned,inv_sent,req_sent,block_sent,"
                "inv_recv,req_recv,block_recv\n";

  m_network_csv.open(network_csv_path, std::ios::out | std::ios::trunc);
  m_network_csv
      << "sim_time,total_blocks,total_orphans,orphan_resolved,"
         "total_resolved,total_txs,unique_txs,duplicate_txs,orphan_rate,"
         "orphan_resolved_rate,red_block_rate,blue_block_rate,collision_rate,"
         "throughput,avg_latency,"
         "p50_latency,p95_latency,p99_latency,avg_dag_size,avg_tips,"
         "total_fees,avg_block_size,bytes_sent,bytes_received,"
         "redundant_sent,redundant_recv,msgs_sent,msgs_recv\n";

  m_miner_revenue_csv.open(miner_revenue_path, std::ios::out | std::ios::trunc);
  m_miner_revenue_csv
      << "miner_id,strategy,blocks_mined,total_fees,revenue_ratio\n";

  std::cout << "Metrics: CSV output initialized at " << m_csv_output_path
            << std::endl;
}

void MetricsCollector::InitPrometheusBackend(uint16_t port) {
#ifdef METRICS_PROMETHEUS
  m_prometheus_registry = std::make_shared<prometheus::Registry>();

  std::string bind_address = "0.0.0.0:" + std::to_string(port);
  m_exposer = std::make_unique<prometheus::Exposer>(bind_address);
  m_exposer->RegisterCollectable(m_prometheus_registry);

  m_family_blocks_mined = &prometheus::BuildGauge()
                               .Name("ghostdag_blocks_mined_total")
                               .Help("Total number of blocks mined by node")
                               .Register(*m_prometheus_registry);

  m_family_blocks_received =
      &prometheus::BuildGauge()
           .Name("ghostdag_blocks_received_total")
           .Help("Total number of blocks received by node")
           .Register(*m_prometheus_registry);

  m_family_blocks_orphaned = &prometheus::BuildGauge()
                                  .Name("ghostdag_blocks_orphaned_total")
                                  .Help("Total number of orphan blocks")
                                  .Register(*m_prometheus_registry);

  m_family_blocks_resolved =
      &prometheus::BuildGauge()
           .Name("ghostdag_blocks_resolved_total")
           .Help("Total number of resolved orphan blocks")
           .Register(*m_prometheus_registry);

  m_family_propagation_latency =
      &prometheus::BuildGauge()
           .Name("ghostdag_propagation_latency_seconds")
           .Help("Average block propagation latency in seconds")
           .Register(*m_prometheus_registry);

  m_family_p50_latency = &prometheus::BuildGauge()
                              .Name("ghostdag_propagation_p50_seconds")
                              .Help("P50 block propagation latency in seconds")
                              .Register(*m_prometheus_registry);

  m_family_p95_latency = &prometheus::BuildGauge()
                              .Name("ghostdag_propagation_p95_seconds")
                              .Help("P95 block propagation latency in seconds")
                              .Register(*m_prometheus_registry);

  m_family_p99_latency = &prometheus::BuildGauge()
                              .Name("ghostdag_propagation_p99_seconds")
                              .Help("P99 block propagation latency in seconds")
                              .Register(*m_prometheus_registry);

  m_family_orphan_rate = &prometheus::BuildGauge()
                              .Name("ghostdag_orphan_rate")
                              .Help("Percentage of orphan blocks")
                              .Register(*m_prometheus_registry);

  m_family_orphan_resolved_rate =
      &prometheus::BuildGauge()
           .Name("ghostdag_orphan_resolved_rate")
           .Help("Percentage of resolved orphan blocks")
           .Register(*m_prometheus_registry);

  m_family_red_block_rate =
      &prometheus::BuildGauge()
           .Name("ghostdag_red_block_rate")
           .Help("Percentage of red blocks (not in blue set)")
           .Register(*m_prometheus_registry);

  m_family_blue_block_rate =
      &prometheus::BuildGauge()
           .Name("ghostdag_blue_block_rate")
           .Help("Percentage of blue blocks (in blue set)")
           .Register(*m_prometheus_registry);

  m_family_collision_rate = &prometheus::BuildGauge()
                                 .Name("ghostdag_transaction_collision_rate")
                                 .Help("Percentage of duplicate transactions")
                                 .Register(*m_prometheus_registry);

  m_family_throughput =
      &prometheus::BuildGauge()
           .Name("ghostdag_throughput")
           .Help("Percentage of unique transactions processed")
           .Register(*m_prometheus_registry);

  m_family_dag_size = &prometheus::BuildGauge()
                           .Name("ghostdag_dag_size")
                           .Help("Current DAG size per node")
                           .Register(*m_prometheus_registry);

  m_family_dag_tips = &prometheus::BuildGauge()
                           .Name("ghostdag_dag_tips")
                           .Help("Current number of DAG tips")
                           .Register(*m_prometheus_registry);

  m_family_mempool_size = &prometheus::BuildGauge()
                               .Name("ghostdag_mempool_size")
                               .Help("Current mempool size per node")
                               .Register(*m_prometheus_registry);

  m_family_fees_earned = &prometheus::BuildGauge()
                              .Name("ghostdag_fees_earned_total")
                              .Help("Total transaction fees earned by node")
                              .Register(*m_prometheus_registry);

  m_family_block_size = &prometheus::BuildGauge()
                             .Name("ghostdag_block_size_bytes")
                             .Help("Average block size in bytes")
                             .Register(*m_prometheus_registry);

  m_family_bytes_sent = &prometheus::BuildCounter()
                             .Name("ghostdag_bytes_sent_total")
                             .Help("Total bytes sent by node")
                             .Register(*m_prometheus_registry);

  m_family_bytes_received = &prometheus::BuildCounter()
                                 .Name("ghostdag_bytes_received_total")
                                 .Help("Total bytes received by node")
                                 .Register(*m_prometheus_registry);

  m_family_redundant_bytes_sent =
      &prometheus::BuildCounter()
           .Name("ghostdag_redundant_bytes_sent_total")
           .Help("Total redundant bytes sent (for Graphene comparison)")
           .Register(*m_prometheus_registry);

  m_family_redundant_bytes_recv =
      &prometheus::BuildCounter()
           .Name("ghostdag_redundant_bytes_received_total")
           .Help("Total redundant bytes received")
           .Register(*m_prometheus_registry);

  m_family_messages_sent = &prometheus::BuildCounter()
                                .Name("ghostdag_messages_sent_total")
                                .Help("Total messages sent by node")
                                .Register(*m_prometheus_registry);

  m_family_messages_recv = &prometheus::BuildCounter()
                                .Name("ghostdag_messages_received_total")
                                .Help("Total messages received by node")
                                .Register(*m_prometheus_registry);

  m_family_propagation_histogram =
      &prometheus::BuildHistogram()
           .Name("ghostdag_propagation_latency_histogram")
           .Help("Block propagation latency histogram")
           .Register(*m_prometheus_registry);

  std::cout << "Metrics: Prometheus exporter listening on http://localhost:"
            << port << "/metrics" << std::endl;
#endif
}

void MetricsCollector::RecordBlockCreated(uint32_t block_id, uint32_t miner_id,
                                          double time_created,
                                          uint8_t num_parents, uint32_t num_txs,
                                          uint32_t total_fees) {
  std::lock_guard<std::mutex> lock(m_mutex);

  BlockMetric bm;
  bm.block_id = block_id;
  bm.miner_id = miner_id;
  bm.time_created = time_created;
  bm.time_first_received = -1.0;
  bm.time_last_received = -1.0;
  bm.first_receiver_node = 0;
  bm.total_receivers = 0;
  bm.total_bytes = 0;
  bm.redundant_bytes = 0;
  bm.num_txs = num_txs;
  bm.total_fees = total_fees;
  bm.num_parents = num_parents;
  bm.blue_score = 0;
  bm.is_orphan = false;
  bm.is_resolved = false;
  bm.is_blue = false;

  m_block_metrics[block_id] = bm;
  m_total_fees += total_fees;

  auto &mrm = m_miner_revenue[miner_id];
  mrm.miner_id = miner_id;
  mrm.blocks_mined++;
  mrm.total_fees_earned += total_fees;
}

void MetricsCollector::RecordBlockReceived(uint32_t block_id, uint32_t node_id,
                                           double time_received,
                                           uint32_t bytes_received,
                                           bool is_redundant) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_block_metrics.find(block_id);
  if (it == m_block_metrics.end()) {
    BlockMetric bm;
    bm.block_id = block_id;
    bm.miner_id = 0;
    bm.time_created = time_received;
    bm.time_first_received = time_received;
    bm.time_last_received = time_received;
    bm.first_receiver_node = node_id;
    bm.total_receivers = 1;
    bm.total_bytes = bytes_received;
    bm.redundant_bytes = is_redundant ? bytes_received : 0;
    bm.num_txs = 0;
    bm.total_fees = 0;
    bm.num_parents = 0;
    bm.blue_score = 0;
    bm.is_orphan = true;
    bm.is_resolved = false;
    m_block_metrics[block_id] = bm;

    if (is_redundant) {
      m_total_redundant_bytes_recv += bytes_received;
    }
  } else {
    BlockMetric &bm = it->second;
    bm.total_receivers++;
    bm.total_bytes += bytes_received;

    if (is_redundant) {
      bm.redundant_bytes += bytes_received;
      m_total_redundant_bytes_recv += bytes_received;
    }

    if (bm.time_first_received < 0) {
      bm.time_first_received = time_received;
      bm.first_receiver_node = node_id;
    }
    if (time_received > bm.time_last_received) {
      bm.time_last_received = time_received;
    }
  }
}

void MetricsCollector::RecordBlockOrphan(uint32_t block_id) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_block_metrics.find(block_id);
  if (it != m_block_metrics.end()) {
    it->second.is_orphan = true;
  }
}

void MetricsCollector::RecordBlockResolved(uint32_t block_id) {
  std::lock_guard<std::mutex> lock(m_mutex);

  auto it = m_block_metrics.find(block_id);
  if (it != m_block_metrics.end()) {
    it->second.is_resolved = true;
  }
}

void MetricsCollector::RecordBlockBlue(uint32_t block_id, bool is_blue) {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (is_blue) {
    m_total_blue_blocks++;
  } else {
    m_total_red_blocks++;
  }

  auto it = m_block_metrics.find(block_id);
  if (it != m_block_metrics.end()) {
    it->second.is_blue = is_blue;
  }
}

void MetricsCollector::RecordTransaction(uint32_t tx_id, uint32_t node_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_unique_txs.insert(tx_id);
}

void MetricsCollector::RecordTransactionDuplicate(uint32_t tx_id) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_duplicate_txs.insert(tx_id);
}

void MetricsCollector::RecordNodeStats(
    uint32_t node_id, uint32_t dag_size, uint32_t dag_tips,
    uint32_t mempool_size, uint32_t blocks_mined, uint32_t blocks_received,
    uint32_t blocks_orphaned, uint32_t blocks_resolved, uint32_t txs_generated,
    uint32_t txs_received, uint32_t total_fees_earned, double bytes_sent,
    double bytes_received, double redundant_sent, double redundant_recv) {
  std::lock_guard<std::mutex> lock(m_mutex);

  NodeMetric &nm = m_node_metrics[node_id];
  nm.node_id = node_id;
  nm.dag_size = dag_size;
  nm.dag_tips = dag_tips;
  nm.mempool_size = mempool_size;
  nm.blocks_mined = blocks_mined;
  nm.blocks_received = blocks_received;
  nm.blocks_orphaned = blocks_orphaned;
  nm.blocks_resolved = blocks_resolved;
  nm.txs_generated = txs_generated;
  nm.txs_received = txs_received;
  nm.total_fees_earned = total_fees_earned;
  nm.total_bytes_sent += bytes_sent;
  nm.total_bytes_received += bytes_received;
  nm.redundant_bytes_sent += redundant_sent;
  nm.redundant_bytes_received += redundant_recv;
}

void MetricsCollector::RecordMessageSent(uint32_t node_id,
                                         const std::string &msg_type) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_total_messages_sent++;

  NodeMetric &nm = m_node_metrics[node_id];
  if (msg_type == "INV") {
    nm.inv_messages_sent++;
  } else if (msg_type == "REQ") {
    nm.req_messages_sent++;
  } else if (msg_type == "BLOCK") {
    nm.block_messages_sent++;
  }
}

void MetricsCollector::RecordMessageReceived(uint32_t node_id,
                                             const std::string &msg_type) {
  std::lock_guard<std::mutex> lock(m_mutex);
  m_total_messages_recv++;

  NodeMetric &nm = m_node_metrics[node_id];
  if (msg_type == "INV") {
    nm.inv_messages_recv++;
  } else if (msg_type == "REQ") {
    nm.req_messages_recv++;
  } else if (msg_type == "BLOCK") {
    nm.block_messages_recv++;
  }
}

void MetricsCollector::Flush() {
  std::lock_guard<std::mutex> lock(m_mutex);

  if (m_backend == MetricBackend::CSV) {
    WriteBlockMetricsCSV();
    WriteNodeMetricsCSV();
    WriteNetworkMetricsCSV();
    WriteMinerRevenueCSV();
  } else {
#ifdef METRICS_PROMETHEUS
    UpdatePrometheusMetrics();
#endif
  }
}

void MetricsCollector::WriteBlockMetricsCSV() {
  for (const auto &[block_id, bm] : m_block_metrics) {
    double latency = bm.time_last_received > 0 && bm.time_first_received > 0
                         ? bm.time_last_received - bm.time_created
                         : 0.0;

    m_block_csv << bm.block_id << "," << bm.miner_id << "," << std::fixed
                << std::setprecision(6) << bm.time_created << ","
                << bm.time_first_received << "," << bm.time_last_received << ","
                << bm.first_receiver_node << "," << bm.total_receivers << ","
                << bm.total_bytes << "," << bm.redundant_bytes << ","
                << bm.num_txs << "," << bm.total_fees << ","
                << static_cast<int>(bm.num_parents) << ","
                << (bm.is_orphan ? 1 : 0) << "," << (bm.is_resolved ? 1 : 0)
                << "," << latency << "\n";
  }
  m_block_csv.flush();
}

void MetricsCollector::WriteNodeMetricsCSV() {
  for (const auto &[node_id, nm] : m_node_metrics) {
    m_node_csv << nm.node_id << "," << nm.blocks_mined << ","
               << nm.blocks_received << "," << nm.blocks_orphaned << ","
               << nm.blocks_resolved << "," << nm.txs_generated << ","
               << nm.txs_received << "," << nm.dag_size << "," << nm.dag_tips
               << "," << nm.mempool_size << "," << std::fixed
               << std::setprecision(2) << nm.total_bytes_sent << ","
               << nm.total_bytes_received << "," << nm.redundant_bytes_sent
               << "," << nm.redundant_bytes_received << ","
               << nm.total_fees_earned << "," << nm.inv_messages_sent << ","
               << nm.req_messages_sent << "," << nm.block_messages_sent << ","
               << nm.inv_messages_recv << "," << nm.req_messages_recv << ","
               << nm.block_messages_recv << "\n";
  }
  m_node_csv.flush();
}

void MetricsCollector::WriteNetworkMetricsCSV() {
  uint32_t total_blocks = m_block_metrics.size();
  uint32_t total_orphans = 0;
  uint32_t orphan_resolved = 0;
  uint32_t total_resolved = 0;
  double total_latency = 0.0;
  uint32_t latency_count = 0;
  std::vector<double> latencies;
  double total_bytes_sent = 0.0;
  double total_bytes_received = 0.0;
  uint32_t total_block_size = 0;

  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.is_orphan) {
      total_orphans++;
    }
    if (bm.is_resolved) {
      total_resolved++;
      if (bm.is_orphan) {
        orphan_resolved++;
      }
    }
    if (bm.time_first_received > 0 && bm.time_last_received > 0) {
      double latency = bm.time_last_received - bm.time_first_received;
      total_latency += latency;
      latencies.push_back(latency);
      latency_count++;
    }
    total_block_size += bm.total_bytes;
  }

  uint32_t avg_dag_size = 0;
  uint32_t avg_tips = 0;
  for (const auto &[node_id, nm] : m_node_metrics) {
    total_bytes_sent += nm.total_bytes_sent;
    total_bytes_received += nm.total_bytes_received;
    avg_dag_size += nm.dag_size;
    avg_tips += nm.dag_tips;
  }

  if (!m_node_metrics.empty()) {
    avg_dag_size /= m_node_metrics.size();
    avg_tips /= m_node_metrics.size();
  }

  uint32_t unique_txs = m_unique_txs.size();
  uint32_t total_txs = unique_txs + m_duplicate_txs.size();
  uint32_t duplicate_txs = m_duplicate_txs.size();

  double orphan_rate = total_blocks > 0
                           ? static_cast<double>(total_orphans) / total_blocks
                           : 0.0;
  double orphan_resolved_rate =
      total_orphans > 0 ? static_cast<double>(orphan_resolved) / total_orphans
                        : 0.0;

  uint32_t total_blue = m_total_blue_blocks.load();
  uint32_t total_red = m_total_red_blocks.load();
  uint32_t total_colored = total_blue + total_red;
  double blue_rate =
      total_colored > 0 ? static_cast<double>(total_blue) / total_colored : 0.0;
  double red_rate =
      total_colored > 0 ? static_cast<double>(total_red) / total_colored : 0.0;

  double collision_rate =
      total_txs > 0 ? static_cast<double>(duplicate_txs) / total_txs : 0.0;
  double throughput =
      total_txs > 0 ? static_cast<double>(unique_txs) / total_txs : 0.0;
  double avg_latency = latency_count > 0 ? total_latency / latency_count : 0.0;

  double p50_latency = CalculatePercentile(latencies, 0.50);
  double p95_latency = CalculatePercentile(latencies, 0.95);
  double p99_latency = CalculatePercentile(latencies, 0.99);

  double avg_block_size =
      total_blocks > 0 ? static_cast<double>(total_block_size) / total_blocks
                       : 0.0;

  double sim_time = Simulator::Now().GetSeconds();

  m_network_csv << std::fixed << std::setprecision(6) << sim_time << ","
                << total_blocks << "," << total_orphans << ","
                << orphan_resolved << "," << total_resolved << "," << total_txs
                << "," << unique_txs << "," << duplicate_txs << ","
                << orphan_rate << "," << orphan_resolved_rate << "," << red_rate
                << "," << blue_rate << "," << collision_rate << ","
                << throughput << "," << avg_latency << "," << p50_latency << ","
                << p95_latency << "," << p99_latency << "," << avg_dag_size
                << "," << avg_tips << "," << m_total_fees.load() << ","
                << avg_block_size << "," << total_bytes_sent << ","
                << total_bytes_received << ","
                << m_total_redundant_bytes_sent.load() << ","
                << m_total_redundant_bytes_recv.load() << ","
                << m_total_messages_sent.load() << ","
                << m_total_messages_recv.load() << "\n";
  m_network_csv.flush();

  std::cout << "\n=== Final Network Metrics ===" << std::endl;
  std::cout << "Total Blocks: " << total_blocks << std::endl;
  std::cout << "Total Orphans: " << total_orphans << " (" << orphan_rate * 100
            << "%)" << std::endl;
  std::cout << "Orphans Resolved: " << orphan_resolved << " ("
            << orphan_resolved_rate * 100 << "%)" << std::endl;
  std::cout << "Red Blocks: " << total_red << " (" << red_rate * 100 << "%)"
            << std::endl;
  std::cout << "Blue Blocks: " << total_blue << " (" << blue_rate * 100 << "%)"
            << std::endl;
  std::cout << "Unique Transactions: " << unique_txs << std::endl;
  std::cout << "Transaction Collisions: " << duplicate_txs << " ("
            << collision_rate * 100 << "%)" << std::endl;
  std::cout << "Throughput: " << throughput * 100 << "%" << std::endl;
  std::cout << "Avg Propagation Latency: " << avg_latency << "s" << std::endl;
  std::cout << "P50/P95/P99 Latency: " << p50_latency << "/" << p95_latency
            << "/" << p99_latency << "s" << std::endl;
  std::cout << "Total Fees Distributed: " << m_total_fees.load() << std::endl;
  std::cout << "Avg Block Size: " << avg_block_size << " bytes" << std::endl;
  std::cout << "Total Bytes Sent: " << total_bytes_sent << std::endl;
  std::cout << "Total Bytes Received: " << total_bytes_received << std::endl;
  std::cout << "Redundant Bytes Sent: " << m_total_redundant_bytes_sent.load()
            << std::endl;
  std::cout << "Redundant Bytes Received: "
            << m_total_redundant_bytes_recv.load() << std::endl;
  std::cout << "Total Messages Sent: " << m_total_messages_sent.load()
            << std::endl;
  std::cout << "Total Messages Received: " << m_total_messages_recv.load()
            << std::endl;
  std::cout << "Avg DAG Size: " << avg_dag_size << std::endl;
  std::cout << "Avg Tips: " << avg_tips << std::endl;
}

void MetricsCollector::WriteMinerRevenueCSV() {
  uint32_t total_fees = m_total_fees.load();

  for (const auto &[miner_id, mrm] : m_miner_revenue) {
    double revenue_ratio =
        total_fees > 0 ? static_cast<double>(mrm.total_fees_earned) / total_fees
                       : 0.0;

    m_miner_revenue_csv << mrm.miner_id << ","
                        << static_cast<int>(TxSelectionStrategy::RANDOM) << ","
                        << mrm.blocks_mined << "," << mrm.total_fees_earned
                        << "," << std::fixed << std::setprecision(6)
                        << revenue_ratio << "\n";
  }
  m_miner_revenue_csv.flush();

  std::cout << "\n=== Miner Revenue Distribution ===" << std::endl;
  for (const auto &[miner_id, mrm] : m_miner_revenue) {
    double revenue_ratio =
        total_fees > 0 ? static_cast<double>(mrm.total_fees_earned) / total_fees
                       : 0.0;
    std::cout << "Miner " << mrm.miner_id << ": " << mrm.blocks_mined
              << " blocks, " << mrm.total_fees_earned << " fees ("
              << revenue_ratio * 100 << "%)" << std::endl;
  }
}

void MetricsCollector::UpdatePrometheusMetrics() {
#ifdef METRICS_PROMETHEUS
  uint32_t total_blocks = m_block_metrics.size();
  uint32_t total_orphans = 0;
  uint32_t orphan_resolved = 0;
  double total_latency = 0.0;
  uint32_t latency_count = 0;
  std::vector<double> latencies;

  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.is_orphan) {
      total_orphans++;
    }
    if (bm.is_resolved) {
      orphan_resolved++;
    }
    if (bm.time_first_received > 0 && bm.time_last_received > 0) {
      double latency = bm.time_last_received - bm.time_first_received;
      total_latency += latency;
      latencies.push_back(latency);
      latency_count++;
      m_family_propagation_histogram
          ->Add({}, prometheus::Histogram::BucketBoundaries{0.001, 0.005, 0.01,
                                                            0.05, 0.1, 0.5, 1.0,
                                                            5.0, 10.0})
          .Observe(latency);
    }
  }

  uint32_t unique_txs = m_unique_txs.size();
  uint32_t total_txs = unique_txs + m_duplicate_txs.size();

  double orphan_rate = total_blocks > 0
                           ? static_cast<double>(total_orphans) / total_blocks
                           : 0.0;
  double orphan_resolved_rate =
      total_orphans > 0 ? static_cast<double>(orphan_resolved) / total_orphans
                        : 0.0;

  uint32_t total_blue = m_total_blue_blocks.load();
  uint32_t total_red = m_total_red_blocks.load();
  uint32_t total_colored = total_blue + total_red;
  double blue_rate =
      total_colored > 0 ? static_cast<double>(total_blue) / total_colored : 0.0;
  double red_rate =
      total_colored > 0 ? static_cast<double>(total_red) / total_colored : 0.0;

  double collision_rate =
      total_txs > 0 ? static_cast<double>(m_duplicate_txs.size()) / total_txs
                    : 0.0;
  double throughput =
      total_txs > 0 ? static_cast<double>(unique_txs) / total_txs : 0.0;
  double avg_latency = latency_count > 0 ? total_latency / latency_count : 0.0;
  double p50_latency = CalculatePercentile(latencies, 0.50);
  double p95_latency = CalculatePercentile(latencies, 0.95);
  double p99_latency = CalculatePercentile(latencies, 0.99);

  m_family_orphan_rate->Add({}).Set(orphan_rate);
  m_family_orphan_resolved_rate->Add({}).Set(orphan_resolved_rate);
  m_family_red_block_rate->Add({}).Set(red_rate);
  m_family_blue_block_rate->Add({}).Set(blue_rate);
  m_family_collision_rate->Add({}).Set(collision_rate);
  m_family_throughput->Add({}).Set(throughput);
  m_family_propagation_latency->Add({}).Set(avg_latency);
  m_family_p50_latency->Add({}).Set(p50_latency);
  m_family_p95_latency->Add({}).Set(p95_latency);
  m_family_p99_latency->Add({}).Set(p99_latency);

  for (const auto &[node_id, nm] : m_node_metrics) {
    prometheus::Labels labels = {{"node", std::to_string(node_id)}};
    m_family_blocks_mined->Add(labels).Set(nm.blocks_mined);
    m_family_blocks_received->Add(labels).Set(nm.blocks_received);
    m_family_blocks_orphaned->Add(labels).Set(nm.blocks_orphaned);
    m_family_blocks_resolved->Add(labels).Set(nm.blocks_resolved);
    m_family_dag_size->Add(labels).Set(nm.dag_size);
    m_family_dag_tips->Add(labels).Set(nm.dag_tips);
    m_family_mempool_size->Add(labels).Set(nm.mempool_size);
    m_family_fees_earned->Add(labels).Set(nm.total_fees_earned);
    m_family_bytes_sent->Add(labels).Increment(nm.total_bytes_sent);
    m_family_bytes_received->Add(labels).Increment(nm.total_bytes_received);
    m_family_redundant_bytes_sent->Add(labels).Increment(
        nm.redundant_bytes_sent);
    m_family_redundant_bytes_recv->Add(labels).Increment(
        nm.redundant_bytes_received);
  }
#endif
}

double MetricsCollector::GetAveragePropagationLatency() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  double total_latency = 0.0;
  uint32_t count = 0;

  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.time_first_received > 0 && bm.time_last_received > 0) {
      total_latency += (bm.time_last_received - bm.time_first_received);
      count++;
    }
  }

  return count > 0 ? total_latency / count : 0.0;
}

double MetricsCollector::GetP50PropagationLatency() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<double> latencies;
  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.time_first_received > 0 && bm.time_last_received > 0) {
      latencies.push_back(bm.time_last_received - bm.time_first_received);
    }
  }

  return CalculatePercentile(latencies, 0.50);
}

double MetricsCollector::GetP95PropagationLatency() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<double> latencies;
  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.time_first_received > 0 && bm.time_last_received > 0) {
      latencies.push_back(bm.time_last_received - bm.time_first_received);
    }
  }

  return CalculatePercentile(latencies, 0.95);
}

double MetricsCollector::GetP99PropagationLatency() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  std::vector<double> latencies;
  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.time_first_received > 0 && bm.time_last_received > 0) {
      latencies.push_back(bm.time_last_received - bm.time_first_received);
    }
  }

  return CalculatePercentile(latencies, 0.99);
}

double MetricsCollector::GetOrphanRate() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  uint32_t total_blocks = m_block_metrics.size();
  uint32_t total_orphans = 0;

  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.is_orphan) {
      total_orphans++;
    }
  }

  return total_blocks > 0 ? static_cast<double>(total_orphans) / total_blocks
                          : 0.0;
}

double MetricsCollector::GetOrphanResolvedRate() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  uint32_t total_orphans = 0;
  uint32_t resolved = 0;

  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.is_orphan) {
      total_orphans++;
      if (bm.is_resolved) {
        resolved++;
      }
    }
  }

  return total_orphans > 0 ? static_cast<double>(resolved) / total_orphans
                           : 0.0;
}

double MetricsCollector::GetCollisionRate() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  uint32_t total_txs = m_unique_txs.size() + m_duplicate_txs.size();
  return total_txs > 0 ? static_cast<double>(m_duplicate_txs.size()) / total_txs
                       : 0.0;
}

double MetricsCollector::GetThroughput() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  uint32_t total_txs = m_unique_txs.size() + m_duplicate_txs.size();
  return total_txs > 0 ? static_cast<double>(m_unique_txs.size()) / total_txs
                       : 0.0;
}

uint32_t MetricsCollector::GetTotalBlocks() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_block_metrics.size();
}

uint32_t MetricsCollector::GetTotalOrphans() const {
  std::lock_guard<std::mutex> lock(m_mutex);

  uint32_t count = 0;
  for (const auto &[block_id, bm] : m_block_metrics) {
    if (bm.is_orphan) {
      count++;
    }
  }
  return count;
}

uint32_t MetricsCollector::GetTotalRedBlocks() const {
  return m_total_red_blocks.load();
}

uint32_t MetricsCollector::GetTotalBlueBlocks() const {
  return m_total_blue_blocks.load();
}

uint32_t MetricsCollector::GetUniqueTransactions() const {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_unique_txs.size();
}

uint32_t MetricsCollector::GetTotalFees() const { return m_total_fees.load(); }

double MetricsCollector::GetRedundantBytesSent() const {
  return m_total_redundant_bytes_sent.load();
}

double MetricsCollector::GetRedundantBytesReceived() const {
  return m_total_redundant_bytes_recv.load();
}

double MetricsCollector::CalculatePercentile(std::vector<double> &sorted_values,
                                             double percentile) {
  if (sorted_values.empty()) {
    return 0.0;
  }

  std::sort(sorted_values.begin(), sorted_values.end());

  double index = percentile * (sorted_values.size() - 1);
  size_t lower = static_cast<size_t>(std::floor(index));
  size_t upper = static_cast<size_t>(std::ceil(index));

  if (lower == upper) {
    return sorted_values[lower];
  }

  double fraction = index - lower;
  return sorted_values[lower] * (1 - fraction) +
         sorted_values[upper] * fraction;
}

std::string MetricsCollector::SanitizeMetricName(const std::string &name) {
  std::string result;
  for (char c : name) {
    if (std::isalnum(c) || c == '_') {
      result += c;
    } else {
      result += '_';
    }
  }
  return result;
}

void MetricsCollector::StartPeriodicFlush(double interval_seconds) {
  if (!m_initialized) {
    return;
  }

  m_flushIntervalSeconds = interval_seconds;

  std::cout << "Metrics: Starting periodic flush every " << interval_seconds
            << " seconds" << std::endl;

  m_periodicFlushEvent = Simulator::Schedule(
      Seconds(interval_seconds), &MetricsCollector::PeriodicFlush, this);
}

void MetricsCollector::PeriodicFlush() {
  if (!m_initialized) {
    return;
  }

  std::lock_guard<std::mutex> lock(m_mutex);

  double currentTime = Simulator::Now().GetSeconds();

  if (m_backend == MetricBackend::CSV) {
    uint32_t total_blocks = m_block_metrics.size();
    uint32_t total_orphans = 0;
    uint32_t orphan_resolved = 0;
    double total_latency = 0.0;
    uint32_t latency_count = 0;
    std::vector<double> latencies;
    double total_bytes_sent = 0.0;
    double total_bytes_received = 0.0;
    uint32_t total_block_size = 0;

    for (const auto &[block_id, bm] : m_block_metrics) {
      if (bm.is_orphan) {
        total_orphans++;
      }
      if (bm.is_resolved) {
        orphan_resolved++;
      }
      if (bm.time_first_received > 0 && bm.time_last_received > 0) {
        double latency = bm.time_last_received - bm.time_first_received;
        total_latency += latency;
        latencies.push_back(latency);
        latency_count++;
      }
      total_block_size += bm.total_bytes;
    }

    uint32_t avg_dag_size = 0;
    uint32_t avg_tips = 0;
    for (const auto &[node_id, nm] : m_node_metrics) {
      total_bytes_sent += nm.total_bytes_sent;
      total_bytes_received += nm.total_bytes_received;
      avg_dag_size += nm.dag_size;
      avg_tips += nm.dag_tips;
    }

    if (!m_node_metrics.empty()) {
      avg_dag_size /= m_node_metrics.size();
      avg_tips /= m_node_metrics.size();
    }

    uint32_t unique_txs = m_unique_txs.size();
    uint32_t total_txs = unique_txs + m_duplicate_txs.size();
    uint32_t duplicate_txs = m_duplicate_txs.size();

    double orphan_rate = total_blocks > 0
                             ? static_cast<double>(total_orphans) / total_blocks
                             : 0.0;
    double orphan_resolved_rate =
        total_orphans > 0 ? static_cast<double>(orphan_resolved) / total_orphans
                          : 0.0;

    uint32_t total_blue = m_total_blue_blocks.load();
    uint32_t total_red = m_total_red_blocks.load();
    uint32_t total_colored = total_blue + total_red;
    double blue_rate = total_colored > 0
                           ? static_cast<double>(total_blue) / total_colored
                           : 0.0;
    double red_rate = total_colored > 0
                          ? static_cast<double>(total_red) / total_colored
                          : 0.0;

    double collision_rate =
        total_txs > 0 ? static_cast<double>(duplicate_txs) / total_txs : 0.0;
    double throughput =
        total_txs > 0 ? static_cast<double>(unique_txs) / total_txs : 0.0;
    double avg_latency =
        latency_count > 0 ? total_latency / latency_count : 0.0;

    double p50_latency = CalculatePercentile(latencies, 0.50);
    double p95_latency = CalculatePercentile(latencies, 0.95);
    double p99_latency = CalculatePercentile(latencies, 0.99);

    double avg_block_size =
        total_blocks > 0 ? static_cast<double>(total_block_size) / total_blocks
                         : 0.0;

    m_network_csv << std::fixed << std::setprecision(6) << currentTime << ","
                  << total_blocks << "," << total_orphans << ","
                  << orphan_resolved << ","
                  << (total_orphans > 0 ? orphan_resolved : 0) << ","
                  << total_txs << "," << unique_txs << "," << duplicate_txs
                  << "," << orphan_rate << "," << orphan_resolved_rate << ","
                  << red_rate << "," << blue_rate << "," << collision_rate
                  << "," << throughput << "," << avg_latency << ","
                  << p50_latency << "," << p95_latency << "," << p99_latency
                  << "," << avg_dag_size << "," << avg_tips << ","
                  << m_total_fees.load() << "," << avg_block_size << ","
                  << total_bytes_sent << "," << total_bytes_received << ","
                  << m_total_redundant_bytes_sent.load() << ","
                  << m_total_redundant_bytes_recv.load() << ","
                  << m_total_messages_sent.load() << ","
                  << m_total_messages_recv.load() << "\n";
    m_network_csv.flush();

    std::cout << "[" << currentTime
              << "s] Periodic Metrics: blocks=" << total_blocks
              << ", orphans=" << total_orphans << " (" << orphan_rate * 100
              << "%), red=" << total_red << " (" << red_rate * 100
              << "%), resolved=" << orphan_resolved << ", txs=" << unique_txs
              << "/" << total_txs << ", latency=" << avg_latency << "s"
              << std::endl;
  }

#ifdef METRICS_PROMETHEUS
  if (m_backend == MetricBackend::PROMETHEUS) {
    UpdatePrometheusMetrics();
  }
#endif

  m_periodicFlushEvent = Simulator::Schedule(
      Seconds(m_flushIntervalSeconds), &MetricsCollector::PeriodicFlush, this);
}

void MetricsCollector::StopPeriodicFlush() {
  if (m_periodicFlushEvent.IsPending()) {
    Simulator::Cancel(m_periodicFlushEvent);
  }
}

} // namespace ns3
