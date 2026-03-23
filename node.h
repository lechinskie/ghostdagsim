/**
 * @file node.cc
 * @brief GhostDAG node and network handling ns3 application definition
 *
 * Architecture inspired by Bitcoin-Simulator by Arthur Gervais et al.
 *   https://github.com/arthurgervais/Bitcoin-Simulator
 *   "On the Security and Performance of Proof of Work Blockchains", CCS'16
 *
 * @author Eduardo Ramos <eduardo_ramos@edu.univali.br>
 * @date 2026
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#pragma once

#include "dag.h"
#include "mempool.h"

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/socket.h"
#include "ns3/traced-callback.h"

#include <map>
#include <unordered_set>

namespace ns3 {
class GhostDagNode : public Application {
public:
  static TypeId GetTypeId();
  GhostDagNode();
  ~GhostDagNode() override;

  // --- Standard NS3 Getters/Setters ---
  Ptr<Socket> GetListeningSocket() const;
  std::vector<Ipv4Address> GetPeersAddresses() const;
  void SetPeersAddresses(const std::vector<Ipv4Address> &peers);
  void SetPeersDownloadSpeeds(
      const std::map<Ipv4Address, double> &peers_download_speeds);
  void SetPeersUploadSpeeds(
      const std::map<Ipv4Address, double> &peers_upload_speeds);
  void SetNodeInternetSpeeds(const NodeInternetSpeeds &internet_speeds);

protected:
  // --- Application Lifecycle ---
  void DoDispose() override;
  void StartApplication() override;
  void StopApplication() override;

  // --- Socket & Connection Handling ---
  void HandleRead(Ptr<Socket> socket);

  void HandleAccept(Ptr<Socket> socket, const Address &from);
  void HandlePeerClose(Ptr<Socket> socket);
  void HandlePeerError(Ptr<Socket> socket);
  bool HandleConnectionRequest(Ptr<Socket> s, const Address &from);
  void HandleConnectionSucceeded(Ptr<Socket> socket);
  void HandleConnectionFailed(Ptr<Socket> socket);
  Ptr<Socket> CreatePeerSocket(Ipv4Address peer_addr);

  // --- Message Dispatcher ---
  void ProcessMessage(enum Messages msg_type, std::string payload,
                      Address &from);

  // --- 1. Real-Time Propagation Handlers  ---
  void HandleInvRelayBlock(const std::string &block_hash, Address &from);
  void HandleReqRelayBlock(const std::string &block_hash, Address &from);
  void HandleBlock(const Block &new_block, Address &from);

  // --- 2. Mempool management ---
  void HandleInvTransactions(const std::vector<std::string> &tx_hashes,
                             Address &from);
  void HandleReqTransactions(const std::vector<std::string> &tx_hashes,
                             Address &from);
  void HandleTransactions(const std::vector<Transaction> &txs, Address &from);

  // --- Sending Helpers ---
  void SendMessage(enum Messages recv, enum Messages type, std::string payload,
                   Address &to);
  void BroadcastInvBlock(const std::string &block_hash,
                         Ipv4Address exclude = Ipv4Address());

  void BroadcastInvTransactions(const std::vector<std::string> &,
                                Ipv4Address = Ipv4Address());
  void FlushInvBatch(Ipv4Address exclude);
  void InvTxTimeoutExpired(std::string tx_hash);

  // --- Transaction generation ---
  void StartTransactionGeneration();
  void StopTransactionGeneration();
  void ScheduleNextTxGeneration();
  void GenerateTransaction();

  // --- Timeout & Queue Management ---
  void InvTimeoutExpired(std::string block_hash);

  Ptr<Socket> m_socket;
  Address m_local;
  TypeId m_tid;

  // Core Structures
  Blockchain m_blockchain;
  Mempool m_mempool;
  Time m_inv_timeout_minutes;
  double m_fixed_block_interval;

  // Network Params
  double m_download_speed;
  double m_upload_speed;
  double m_average_transaction_size;
  int m_transaction_index_size;

  // Connectivity Maps
  std::vector<Ipv4Address> m_peers_addresses;
  std::map<Ipv4Address, double> m_peers_download_speeds;
  std::map<Ipv4Address, double> m_peers_upload_speeds;
  std::map<Ipv4Address, Ptr<Socket>> m_peers_sockets;
  std::map<Ptr<Socket>, Ipv4Address> m_socket_to_peer;

  // State Maps
  std::map<std::string, std::vector<Address>> m_queue_inv;
  std::map<std::string, EventId> m_inv_timeouts;
  std::map<Address, std::string> m_buffered_data;
  std::map<std::string, Block> m_only_headers_received;
  std::map<Ipv4Address, std::vector<std::string>> m_pending_messages;

  int m_ghostdag_port;
  uint8_t m_ghostdag_k;
  int m_seconds_per_min;
  int m_message_header_size;
  int m_inventory_size;
  int m_headers_size;

  // Transaction generation
  bool m_generateTransactions;
  double m_txGenInterval;
  double m_txFeeLambda;
  int m_mempoolSize;
  EventId m_nextTxGenerationEvent;
  std::mt19937 m_generator;
  std::exponential_distribution<double> m_txFeeDistribution;
  int m_txsGenerated;
  std::vector<std::string> m_pending_inv_tx;
  EventId m_invBatchEvent;
  std::unordered_set<uint64_t> m_known_txs;
  std::unordered_map<std::string, std::vector<Address>> m_queue_inv_tx;
  std::unordered_map<std::string, EventId> m_inv_tx_timeouts;
  static constexpr double INV_BATCH_INTERVAL_S = 0.1;

  static inline uint32_t IdFromTxId(uint64_t txId) {
    return static_cast<uint32_t>(txId >> 32);
  }

  TracedCallback<Ptr<const Packet>, const Address &> m_rx_trace;
};

} // namespace ns3
