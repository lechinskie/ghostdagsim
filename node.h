/**
 * @file node.h
 * @brief GhostDAG node and network handling ns3 application definition
 *
 * Architecture inspired by Bitcoin-Simulator by Arthur Gervais et al.
 *   https://github.com/arthurgervais/Bitcoin-Simulator
 *   "On the Security and Performance of Proof of Work Blockchains", CCS'16
 *
 * @author Eduardo Lechinski Ramos <lechinski@univali.br>
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
#include "graphene.h"
#include "mempool.h"

#include "ns3/application.h"
#include "ns3/ipv4-address.h"
#include "ns3/ptr.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/tag.h"
#include "ns3/traced-callback.h"

#include <deque>

#include <map>
#include <unordered_set>

namespace ns3 {

/**
 * Carries a message's JSON payload alongside its frame. The frame on the wire
 * is sized by the message-size model (see WireSize* in node.cc), not by the
 * JSON encoding, so the payload travels as a byte tag on the frame's last byte
 * and does not count towards the bytes transmitted.
 */
class GhostDagPayloadTag : public Tag {
public:
  static TypeId GetTypeId();
  TypeId GetInstanceTypeId() const override;
  uint32_t GetSerializedSize() const override;
  void Serialize(TagBuffer i) const override;
  void Deserialize(TagBuffer i) override;
  void Print(std::ostream &os) const override;

  std::string payload;
};

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
  void ProcessMessage(enum Messages msg_type, const std::string &payload,
                      Address &from, uint32_t wire_bytes);

  // --- 1. Real-Time Propagation Handlers ---
  void HandleInvRelayBlock(const std::string &block_hash, Address &from);
  void HandleReqRelayBlock(const std::string &block_hash, bool graphene,
                           Address &from, uint64_t receiver_mempool_count = 0);
  void HandleBlock(const Block &new_block, Address &from);

  // --- 1b. Graphene propagation handlers ---
  void HandleGrapheneBlock(const nlohmann::json &data, Address &from);
  void HandleGrapheneRecoveryRequest(const nlohmann::json &data, Address &from);

  void HandleGrapheneRecoveryResponse(const nlohmann::json &data,
                                      Address &from);
  void GrapheneRecoveryTimeout(std::string block_hash);

  std::map<std::string, GrapheneState> m_graphene_state;
  std::map<std::string, EventId> m_graphene_timeouts;
  std::map<std::string, Address> m_graphene_senders;
  Time m_graphene_recovery_timeout;

  // --- 2. Mempool management ---
  void HandleInvTransactions(const std::vector<std::string> &tx_hashes,
                             Address &from);
  void HandleReqTransactions(const std::vector<std::string> &tx_hashes,
                             Address &from);
  void HandleTransactions(const std::vector<Transaction> &txs, Address &from);

  // --- Sending Helpers ---
  void SendMessage(enum Messages recv, enum Messages type, std::string payload,
                   Address &to, uint32_t wire_bytes);
  void EnqueueFrame(Ptr<Socket> socket, const std::string &serialized,
                    uint32_t wire_bytes);
  void HandleSendReady(Ptr<Socket> socket, uint32_t available);

  // --- Message-size model (bytes on the wire) ---
  uint32_t WireSizeInv() const;
  uint32_t WireSizeBlock(const Block &block) const;
  uint32_t WireSizeGrapheneBlock(size_t n_parents, size_t n_txs, double fpr,
                                 const IBLT &iblt) const;
  uint32_t WireSizeRecoveryRequest(size_t z, double fpr_r) const;
  uint32_t WireSizeRecoveryResponse(const IBLT &iblt,
                                    size_t n_missing) const;
  uint32_t WireSizeTxInv(size_t n_txs) const;
  uint32_t WireSizeTxs(size_t n_txs) const;
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

  void EmitDagSnapshot();
  void ScheduleSnapshot();
  EventId m_snapshotEvent;
  double m_snapshotInterval = 30.0;

  // --- Sockets ---
  Ptr<Socket> m_socket;
  Address m_local;
  TypeId m_tid;

  // --- Core Structures ---
  Blockchain m_blockchain;
  Mempool m_mempool;
  Time m_inv_timeout_minutes;

  // --- Network Params ---
  double m_download_speed;
  double m_upload_speed;
  double m_average_transaction_size;
  int m_transaction_index_size;

  // --- Connectivity Maps ---
  std::vector<Ipv4Address> m_peers_addresses;
  std::map<Ipv4Address, double> m_peers_download_speeds;
  std::map<Ipv4Address, double> m_peers_upload_speeds;
  std::map<Ipv4Address, Ptr<Socket>> m_peers_sockets;
  std::map<Ptr<Socket>, Ipv4Address> m_socket_to_peer;

  // --- Block-propagation State ---
  std::map<std::string, std::vector<Address>> m_queue_inv;
  std::map<std::string, EventId> m_inv_timeouts;
  // Per-connection frame reassembly (receive side)
  struct RxFrameState {
    uint8_t prefix[8];
    uint32_t prefix_have = 0;
    uint32_t frame_len = 0;
    uint32_t body_remaining = 0;
  };
  std::map<Ptr<Socket>, RxFrameState> m_rx_state;
  // Frames waiting for TCP send-buffer space (send side)
  std::map<Ptr<Socket>, std::deque<Ptr<Packet>>> m_tx_queue;
  std::map<std::string, Block> m_only_headers_received;
  std::map<Ipv4Address, std::vector<std::pair<std::string, uint32_t>>>
      m_pending_messages;

  bool m_graphene_enabled;

  // --- Port / Sizes ---
  int m_ghostdag_port;
  uint32_t m_ghostdag_k;
  int m_seconds_per_min;
  int m_message_header_size;
  int m_inventory_size;
  int m_headers_size;
  int m_parent_hash_size;
  int m_transaction_size;

  // --- Transaction Generation ---
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
