#pragma once

#include "ns3/nstime.h"
#include "ns3/simulator.h"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#ifdef METRICS_PROMETHEUS
#include "prometheus/counter.h"
#include "prometheus/exposer.h"
#include "prometheus/family.h"
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"
#include "prometheus/registry.h"
#endif

namespace ns3 {

enum class MetricBackend { CSV, PROMETHEUS };

enum class TxSelectionStrategy { RANDOM = 0, RATIONAL = 1 };

struct BlockMetric {
  uint32_t block_id;
  uint32_t miner_id;
  double time_created;
  double time_first_received;
  double time_last_received;
  uint32_t first_receiver_node;
  uint32_t total_receivers;
  uint32_t total_bytes;
  uint32_t redundant_bytes;
  uint32_t num_txs;
  uint32_t total_fees;
  uint8_t num_parents;
  uint32_t blue_score;
  bool is_orphan;
  bool is_resolved;
  bool is_blue;
};

struct NodeMetric {
  uint32_t node_id;
  uint32_t blocks_mined;
  uint32_t blocks_received;
  uint32_t blocks_orphaned;
  uint32_t blocks_resolved;
  uint32_t txs_generated;
  uint32_t txs_received;
  uint32_t mempool_size;
  uint32_t dag_size;
  uint32_t dag_tips;
  double total_bytes_sent;
  double total_bytes_received;
  double redundant_bytes_sent;
  double redundant_bytes_received;
  uint32_t total_fees_earned;
  uint32_t inv_messages_sent;
  uint32_t req_messages_sent;
  uint32_t block_messages_sent;
  uint32_t inv_messages_recv;
  uint32_t req_messages_recv;
  uint32_t block_messages_recv;
};

struct MinerRevenueMetric {
  uint32_t miner_id;
  TxSelectionStrategy strategy;
  uint32_t blocks_mined;
  uint32_t total_fees_earned;
  double revenue_ratio;
};

struct NetworkMetric {
  double simulation_time;
  uint32_t total_blocks;
  uint32_t total_orphans;
  uint32_t total_resolved;
  uint32_t orphan_resolved_count;
  uint32_t total_txs;
  uint32_t unique_txs;
  uint32_t duplicate_txs;
  double total_bytes_sent;
  double total_bytes_received;
  double redundant_bytes_sent;
  double redundant_bytes_received;
  double avg_propagation_latency;
  double p50_propagation_latency;
  double p95_propagation_latency;
  double p99_propagation_latency;
  double orphan_rate;
  double orphan_resolved_rate;
  double collision_rate;
  double throughput;
  uint32_t avg_dag_size;
  uint32_t avg_tips;
  double total_fees_distributed;
  double avg_block_size;
  uint32_t total_messages_sent;
  uint32_t total_messages_recv;
};

class MetricsCollector {
public:
  static MetricsCollector &GetInstance();

  void Initialize(MetricBackend backend, const std::string &output_path = "",
                  uint16_t prometheus_port = 9090);

  void SetSimulationParams(uint32_t num_nodes, uint32_t num_miners,
                           uint8_t ghostdag_k, double lambda);

  void RecordBlockCreated(uint32_t block_id, uint32_t miner_id,
                          double time_created, uint8_t num_parents,
                          uint32_t num_txs, uint32_t total_fees);

  void RecordBlockReceived(uint32_t block_id, uint32_t node_id,
                           double time_received, uint32_t bytes_received,
                           bool is_redundant);

  void RecordBlockOrphan(uint32_t block_id);

  void RecordBlockResolved(uint32_t block_id);

  void RecordBlockBlue(uint32_t block_id, bool is_blue);

  void RecordTransaction(uint32_t tx_id, uint32_t node_id);

  void RecordTransactionDuplicate(uint32_t tx_id);

  void RecordNodeStats(uint32_t node_id, uint32_t dag_size, uint32_t dag_tips,
                       uint32_t mempool_size, uint32_t blocks_mined,
                       uint32_t blocks_received, uint32_t blocks_orphaned,
                       uint32_t blocks_resolved, uint32_t txs_generated,
                       uint32_t txs_received, uint32_t total_fees_earned,
                       double bytes_sent, double bytes_received,
                       double redundant_sent, double redundant_recv);

  void RecordMessageSent(uint32_t node_id, const std::string &msg_type);
  void RecordMessageReceived(uint32_t node_id, const std::string &msg_type);

  void Flush();
  void StartPeriodicFlush(double interval_seconds);
  void StopPeriodicFlush();
  void PeriodicFlush();

  double GetAveragePropagationLatency() const;
  double GetP50PropagationLatency() const;
  double GetP95PropagationLatency() const;
  double GetP99PropagationLatency() const;
  double GetOrphanRate() const;
  double GetOrphanResolvedRate() const;
  double GetCollisionRate() const;
  double GetThroughput() const;
  uint32_t GetTotalBlocks() const;
  uint32_t GetTotalOrphans() const;
  uint32_t GetTotalRedBlocks() const;
  uint32_t GetTotalBlueBlocks() const;
  uint32_t GetUniqueTransactions() const;
  uint32_t GetTotalFees() const;
  double GetRedundantBytesSent() const;
  double GetRedundantBytesReceived() const;

private:
  MetricsCollector() = default;
  ~MetricsCollector();

  void InitCSVBackend(const std::string &output_path);
  void InitPrometheusBackend(uint16_t port);

  void WriteBlockMetricsCSV();
  void WriteNodeMetricsCSV();
  void WriteNetworkMetricsCSV();
  void WriteMinerRevenueCSV();
  void UpdatePrometheusMetrics();

  static std::string SanitizeMetricName(const std::string &name);
  static double CalculatePercentile(std::vector<double> &sorted_values,
                                    double percentile);

  MetricBackend m_backend{MetricBackend::CSV};
  bool m_initialized{false};

  uint32_t m_num_nodes{0};
  uint32_t m_num_miners{0};
  uint8_t m_ghostdag_k{10};
  double m_lambda{20.0};

  std::map<uint32_t, BlockMetric> m_block_metrics;
  std::map<uint32_t, NodeMetric> m_node_metrics;
  std::map<uint32_t, MinerRevenueMetric> m_miner_revenue;
  std::set<uint32_t> m_unique_txs;
  std::set<uint32_t> m_duplicate_txs;

  std::atomic<uint32_t> m_total_fees{0};
  std::atomic<uint32_t> m_total_red_blocks{0};
  std::atomic<uint32_t> m_total_blue_blocks{0};
  std::atomic<double> m_total_redundant_bytes_sent{0.0};
  std::atomic<double> m_total_redundant_bytes_recv{0.0};
  std::atomic<uint32_t> m_total_messages_sent{0};
  std::atomic<uint32_t> m_total_messages_recv{0};

  mutable std::mutex m_mutex;

  EventId m_periodicFlushEvent;
  double m_flushIntervalSeconds{60.0};

  std::ofstream m_block_csv;
  std::ofstream m_node_csv;
  std::ofstream m_network_csv;
  std::ofstream m_miner_revenue_csv;
  std::string m_csv_output_path;

#ifdef METRICS_PROMETHEUS
  std::shared_ptr<prometheus::Registry> m_prometheus_registry;
  std::unique_ptr<prometheus::Exposer> m_exposer;

  prometheus::Family<prometheus::Gauge> *m_family_blocks_mined;
  prometheus::Family<prometheus::Gauge> *m_family_blocks_received;
  prometheus::Family<prometheus::Gauge> *m_family_blocks_orphaned;
  prometheus::Family<prometheus::Gauge> *m_family_blocks_resolved;
  prometheus::Family<prometheus::Gauge> *m_family_propagation_latency;
  prometheus::Family<prometheus::Gauge> *m_family_p50_latency;
  prometheus::Family<prometheus::Gauge> *m_family_p95_latency;
  prometheus::Family<prometheus::Gauge> *m_family_p99_latency;
  prometheus::Family<prometheus::Gauge> *m_family_orphan_rate;
  prometheus::Family<prometheus::Gauge> *m_family_orphan_resolved_rate;
  prometheus::Family<prometheus::Gauge> *m_family_red_block_rate;
  prometheus::Family<prometheus::Gauge> *m_family_blue_block_rate;
  prometheus::Family<prometheus::Gauge> *m_family_collision_rate;
  prometheus::Family<prometheus::Gauge> *m_family_throughput;
  prometheus::Family<prometheus::Gauge> *m_family_dag_size;
  prometheus::Family<prometheus::Gauge> *m_family_dag_tips;
  prometheus::Family<prometheus::Gauge> *m_family_mempool_size;
  prometheus::Family<prometheus::Gauge> *m_family_fees_earned;
  prometheus::Family<prometheus::Gauge> *m_family_block_size;
  prometheus::Family<prometheus::Counter> *m_family_bytes_sent;
  prometheus::Family<prometheus::Counter> *m_family_bytes_received;
  prometheus::Family<prometheus::Counter> *m_family_redundant_bytes_sent;
  prometheus::Family<prometheus::Counter> *m_family_redundant_bytes_recv;
  prometheus::Family<prometheus::Counter> *m_family_messages_sent;
  prometheus::Family<prometheus::Counter> *m_family_messages_recv;
  prometheus::Family<prometheus::Histogram> *m_family_propagation_histogram;
#endif
};

class NodeStats {
public:
  NodeStats() = default;

  std::atomic<uint32_t> blocks_mined{0};
  std::atomic<uint32_t> blocks_received{0};
  std::atomic<uint32_t> blocks_orphaned{0};
  std::atomic<uint32_t> blocks_resolved{0};
  std::atomic<uint32_t> txs_generated{0};
  std::atomic<uint32_t> txs_received{0};
  std::atomic<uint32_t> total_fees_earned{0};

  std::atomic<double> total_bytes_sent{0.0};
  std::atomic<double> total_bytes_received{0.0};
  std::atomic<double> redundant_bytes_sent{0.0};
  std::atomic<double> redundant_bytes_received{0.0};

  std::atomic<uint32_t> dag_size{0};
  std::atomic<uint32_t> dag_tips{0};
  std::atomic<uint32_t> mempool_size{0};

  std::atomic<double> last_block_propagation_time{0.0};
  std::atomic<double> total_propagation_time{0.0};

  std::atomic<uint32_t> inv_messages_sent{0};
  std::atomic<uint32_t> req_messages_sent{0};
  std::atomic<uint32_t> block_messages_sent{0};
  std::atomic<uint32_t> inv_messages_recv{0};
  std::atomic<uint32_t> req_messages_recv{0};
  std::atomic<uint32_t> block_messages_recv{0};

  std::vector<double> propagation_latencies;

  void Reset() {
    blocks_mined = 0;
    blocks_received = 0;
    blocks_orphaned = 0;
    blocks_resolved = 0;
    txs_generated = 0;
    txs_received = 0;
    total_fees_earned = 0;
    total_bytes_sent = 0.0;
    total_bytes_received = 0.0;
    redundant_bytes_sent = 0.0;
    redundant_bytes_received = 0.0;
    dag_size = 0;
    dag_tips = 0;
    mempool_size = 0;
    last_block_propagation_time = 0.0;
    total_propagation_time = 0.0;
    inv_messages_sent = 0;
    req_messages_sent = 0;
    block_messages_sent = 0;
    inv_messages_recv = 0;
    req_messages_recv = 0;
    block_messages_recv = 0;
    propagation_latencies.clear();
  }
};

inline MetricsCollector &GetMetricsCollector() {
  return MetricsCollector::GetInstance();
}

} // namespace ns3
