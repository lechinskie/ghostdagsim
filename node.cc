/**
 * @file node.cc
 * @brief GhostDAG node and network handling implementation
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

#define IBLT_IMPL
#include "node.h"

#include "dag.h"
#include "graphene.h"
#include "metrics.h"
#include "thirdparty/json.h"

#include "ns3/address-utils.h"
#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/mpi-interface.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GhostDagNode");

NS_OBJECT_ENSURE_REGISTERED(GhostDagNode);
NS_OBJECT_ENSURE_REGISTERED(GhostDagPayloadTag);

/*
 * Framing
 *
 * Every message is one frame of exactly its modelled wire size: an 8-byte
 * prefix (magic, frame length) followed by zero padding. The JSON payload is
 * attached as a byte tag on the frame's last byte, so the link carries the
 * modelled number of bytes regardless of how the payload is encoded.
 * */

static constexpr uint32_t FRAME_MAGIC = 0x47444147; // "GDAG"
static constexpr uint32_t FRAME_PREFIX_SIZE = 8;
static constexpr double LN2_SQUARED = 0.4804530139182014;
// Must stay below the MPI receive buffer, which ns-3 hard-codes and which the
// Dockerfile raises to 2 MB (MAX_MPI_MSG_SIZE), minus room for packet headers.
static constexpr size_t MPI_PAYLOAD_LIMIT = 2097152 - 65536;

TypeId GhostDagPayloadTag::GetTypeId() {
  static TypeId tid = TypeId("ns3::GhostDagPayloadTag")
                          .SetParent<Tag>()
                          .SetGroupName("Applications")
                          .AddConstructor<GhostDagPayloadTag>();
  return tid;
}

TypeId GhostDagPayloadTag::GetInstanceTypeId() const { return GetTypeId(); }

uint32_t GhostDagPayloadTag::GetSerializedSize() const {
  return 4 + static_cast<uint32_t>(payload.size());
}

void GhostDagPayloadTag::Serialize(TagBuffer i) const {
  i.WriteU32(static_cast<uint32_t>(payload.size()));
  i.Write(reinterpret_cast<const uint8_t *>(payload.data()),
          static_cast<uint32_t>(payload.size()));
}

void GhostDagPayloadTag::Deserialize(TagBuffer i) {
  uint32_t len = i.ReadU32();
  payload.resize(len);
  i.Read(reinterpret_cast<uint8_t *>(payload.data()), len);
}

void GhostDagPayloadTag::Print(std::ostream &os) const {
  os << "payload=" << payload.size() << "B";
}

TypeId GhostDagNode::GetTypeId() {
  static TypeId tid =
      TypeId("ns3::GhostDagNode")
          .SetParent<Application>()
          .SetGroupName("Applications")
          .AddConstructor<GhostDagNode>()
          .AddAttribute("Kghostdag",
                        "The K value for dreedy algorithm ghostdag",
                        UintegerValue(10),
                        MakeUintegerAccessor(&GhostDagNode::m_ghostdag_k),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute("Local", "The Address on which to Bind the rx socket.",
                        AddressValue(),
                        MakeAddressAccessor(&GhostDagNode::m_local),
                        MakeAddressChecker())
          .AddAttribute("InvTimeoutMinutes",
                        "The timeout of inv messages in minutes",
                        TimeValue(Minutes(20)),
                        MakeTimeAccessor(&GhostDagNode::m_inv_timeout_minutes),
                        MakeTimeChecker())
          .AddAttribute("DownloadSpeed",
                        "The download speed of the node in Bytes/s.",
                        DoubleValue(1000000.0),
                        MakeDoubleAccessor(&GhostDagNode::m_download_speed),
                        MakeDoubleChecker<double>())
          .AddAttribute("UploadSpeed",
                        "The upload speed of the node in Bytes/s.",
                        DoubleValue(1000000.0),
                        MakeDoubleAccessor(&GhostDagNode::m_upload_speed),
                        MakeDoubleChecker<double>())
          .AddAttribute(
              "GenerateTransactions", "Whether to generate transactions",
              BooleanValue(true),
              MakeBooleanAccessor(&GhostDagNode::m_generateTransactions),
              MakeBooleanChecker())
          .AddAttribute("TxFeeLambda",
                        "Exponential distribution lambda for tx fees",
                        DoubleValue(150.0),
                        MakeDoubleAccessor(&GhostDagNode::m_txFeeLambda),
                        MakeDoubleChecker<double>())
          .AddAttribute("MempoolSize", "Maximum mempool size",
                        UintegerValue(10000),
                        MakeUintegerAccessor(&GhostDagNode::m_mempoolSize),
                        MakeUintegerChecker<uint32_t>())
          .AddAttribute(
              "TxGenInterval",
              "Mean interval between transaction generations (seconds)",
              DoubleValue(0.1),
              MakeDoubleAccessor(&GhostDagNode::m_txGenInterval),
              MakeDoubleChecker<double>())
          .AddAttribute("SnapshotInterval",
                        "Seconds between DAG snapshot events (0 disables)",
                        DoubleValue(30.0),
                        MakeDoubleAccessor(&GhostDagNode::m_snapshotInterval),
                        MakeDoubleChecker<double>(0.0))
          .AddAttribute(
              "GrapheneEnabled",
              "Use Graphene compact block relay instead of full block relay",
              BooleanValue(false),
              MakeBooleanAccessor(&GhostDagNode::m_graphene_enabled),
              MakeBooleanChecker())
          .AddTraceSource("Rx", "A packet has been received",
                          MakeTraceSourceAccessor(&GhostDagNode::m_rx_trace),
                          "ns3::Packet::AddressTracedCallback");
  return tid;
}

GhostDagNode::GhostDagNode()
    : m_mempool(10000), m_average_transaction_size(522.4),
      m_transaction_index_size(2), m_graphene_enabled(false),
      m_generateTransactions(true), m_txGenInterval(0.1), m_txFeeLambda(150.0),
      m_mempoolSize(10000), m_txFeeDistribution(1 / 150.0), m_txsGenerated(0) {
  NS_LOG_FUNCTION(this);
  m_socket = nullptr;

  std::random_device rd;
  m_generator.seed(rd() + 1);

  m_ghostdag_port = 16433;
  m_ghostdag_k = 10;
  m_seconds_per_min = 60;
  m_message_header_size = 90;
  m_inventory_size = 36;
  m_headers_size = 81;
  m_parent_hash_size = 32;
  m_transaction_size = 522;

  m_graphene_recovery_timeout = Seconds(10);

  m_tid = TcpSocketFactory::GetTypeId();
}

GhostDagNode::~GhostDagNode() { NS_LOG_FUNCTION(this); }

Ptr<Socket> GhostDagNode::GetListeningSocket() const {
  NS_LOG_FUNCTION(this);
  return m_socket;
}

std::vector<Ipv4Address> GhostDagNode::GetPeersAddresses() const {
  NS_LOG_FUNCTION(this);
  return m_peers_addresses;
}

void GhostDagNode::SetPeersAddresses(const std::vector<Ipv4Address> &peers) {
  NS_LOG_FUNCTION(this);
  m_peers_addresses = peers;
}

void GhostDagNode::SetPeersDownloadSpeeds(
    const std::map<Ipv4Address, double> &peers_download_speeds) {
  NS_LOG_FUNCTION(this);
  m_peers_download_speeds = peers_download_speeds;
}

void GhostDagNode::SetPeersUploadSpeeds(
    const std::map<Ipv4Address, double> &peers_upload_speeds) {
  NS_LOG_FUNCTION(this);
  m_peers_upload_speeds = peers_upload_speeds;
}

void GhostDagNode::SetNodeInternetSpeeds(
    const NodeInternetSpeeds &internet_speeds) {
  NS_LOG_FUNCTION(this);
  m_download_speed = internet_speeds.download_speed * 1000000 / 8;
  m_upload_speed = internet_speeds.upload_speed * 1000000 / 8;
}

/*
 * App life cycle
 * */

void GhostDagNode::DoDispose() {
  NS_LOG_FUNCTION(this);
  m_socket = nullptr;
  Application::DoDispose();
}

void GhostDagNode::StartApplication() {
  NS_LOG_FUNCTION(this);
  m_blockchain.ghostdag_k = static_cast<int>(m_ghostdag_k);
  m_blockchain.node_id_metric = GetNode()->GetId();
  m_txFeeDistribution =
      std::exponential_distribution<double>(1.0 / m_txFeeLambda);

  m_generator.seed(static_cast<uint32_t>(GetNode()->GetId()) * 2654435761u);

  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": download speed = " << m_download_speed << " B/s");
  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": upload speed = " << m_upload_speed << " B/s");
  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": GHOSTDAG K = " << static_cast<int>(m_ghostdag_k));
  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << ": peers count = " << m_peers_addresses.size());

  if (!m_socket) {
    m_socket = Socket::CreateSocket(GetNode(), m_tid);
    InetSocketAddress localAddr = InetSocketAddress::ConvertFrom(m_local);
    m_ghostdag_port = localAddr.GetPort();

    m_socket->Bind(m_local);
    m_socket->Listen();

    if (addressUtils::IsMulticast(m_local)) {
      Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);
      if (udpSocket) {
        udpSocket->MulticastJoinGroup(0, m_local);
      } else {
        NS_FATAL_ERROR("Error: joining multicast on a non-UDP socket");
      }
    }
  }

  m_socket->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
  m_socket->SetAcceptCallback(
      MakeCallback(&GhostDagNode::HandleConnectionRequest, this),
      MakeCallback(&GhostDagNode::HandleAccept, this));

  m_socket->SetCloseCallbacks(
      MakeCallback(&GhostDagNode::HandlePeerClose, this),
      MakeCallback(&GhostDagNode::HandlePeerError, this));

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << ": Creating peer sockets");
  for (const auto &peer_addr : m_peers_addresses) {
    CreatePeerSocket(peer_addr);
  }

  m_mempool = Mempool(static_cast<size_t>(m_mempoolSize));
  StartTransactionGeneration();
  ScheduleSnapshot();
}

void GhostDagNode::StopApplication() {
  NS_LOG_FUNCTION(this);

  for (auto &[addr, sock] : m_peers_sockets) {
    sock->Close();
    m_socket_to_peer.erase(sock);
  }
  m_pending_messages.clear();
  m_tx_queue.clear();

  if (m_socket) {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  NS_LOG_WARN("\n\nGHOSTDAG NODE " << GetNode()->GetId() << ":");
  NS_LOG_WARN("Total Blocks in DAG = " << m_blockchain.blocks.size());

  if (m_snapshotEvent.IsPending())
    Simulator::Cancel(m_snapshotEvent);
  StopTransactionGeneration();
}

void GhostDagNode::HandleRead(Ptr<Socket> socket) {
  Address from;
  Ptr<Packet> packet;

  while ((packet = socket->RecvFrom(from))) {
    uint32_t size = packet->GetSize();
    if (size == 0) {
      break;
    }

    // Payload tags in this chunk, keyed by the offset just past their byte.
    std::map<uint32_t, std::string> payloads;
    GhostDagPayloadTag tag;
    ByteTagIterator tags = packet->GetByteTagIterator();
    while (tags.HasNext()) {
      ByteTagIterator::Item item = tags.Next();
      if (item.GetTypeId() == GhostDagPayloadTag::GetTypeId()) {
        item.GetTag(tag);
        payloads[item.GetEnd()] = tag.payload;
      }
    }

    RxFrameState &st = m_rx_state[socket];
    uint32_t pos = 0;
    while (pos < size) {
      if (st.prefix_have < FRAME_PREFIX_SIZE) {
        uint32_t take = std::min(FRAME_PREFIX_SIZE - st.prefix_have, size - pos);
        packet->CreateFragment(pos, take)->CopyData(st.prefix + st.prefix_have,
                                                    take);
        st.prefix_have += take;
        pos += take;
        if (st.prefix_have == FRAME_PREFIX_SIZE) {
          uint32_t magic, len;
          std::memcpy(&magic, st.prefix, 4);
          std::memcpy(&len, st.prefix + 4, 4);
          NS_ABORT_MSG_IF(magic != FRAME_MAGIC || len <= FRAME_PREFIX_SIZE,
                          "Node " << GetNode()->GetId()
                                  << ": corrupt frame prefix");
          st.frame_len = len;
          st.body_remaining = len - FRAME_PREFIX_SIZE;
        }
        continue;
      }

      uint32_t take = std::min(st.body_remaining, size - pos);
      pos += take;
      st.body_remaining -= take;
      if (st.body_remaining > 0) {
        continue;
      }

      uint32_t wire_bytes = st.frame_len;
      st = RxFrameState{};

      auto pit = payloads.find(pos);
      if (pit == payloads.end()) {
        NS_LOG_ERROR("Node " << GetNode()->GetId()
                             << " received a frame without payload");
        continue;
      }

      auto data = nlohmann::json::parse(pit->second, nullptr, false);
      if (data.is_discarded()) {
        continue;
      }

      uint64_t msg_data = data.value("msg", 0);
      if (msg_data != (uint64_t)NO_MESSAGE) {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds()
                               << "s ghostdag node " << GetNode()->GetId()
                               << " received " << wire_bytes << " bytes from "
                               << InetSocketAddress::ConvertFrom(from).GetIpv4()
                               << " with info = " << data.dump(4));

        ProcessMessage((enum Messages)msg_data, pit->second, from, wire_bytes);
      }
    }
  }
}

void GhostDagNode::SendMessage(enum Messages recv, enum Messages type,
                               std::string payload, Address &to,
                               uint32_t wire_bytes) {
  nlohmann::json d = nlohmann::json::parse(payload);
  d["msg"] = type;
  std::string serialized = d.dump();

  InetSocketAddress peer = InetSocketAddress::ConvertFrom(to);
  Ipv4Address ip = peer.GetIpv4();

  auto pending_it = m_pending_messages.find(ip);
  if (pending_it != m_pending_messages.end()) {
    pending_it->second.emplace_back(serialized, wire_bytes);
    return;
  }

  auto it = m_peers_sockets.find(ip);
  if (it == m_peers_sockets.end()) {
    m_pending_messages[ip].emplace_back(serialized, wire_bytes);
    CreatePeerSocket(ip);
    return;
  }

  EnqueueFrame(it->second, serialized, wire_bytes);
}

void GhostDagNode::EnqueueFrame(Ptr<Socket> socket,
                                const std::string &serialized,
                                uint32_t wire_bytes) {
  wire_bytes = std::max(wire_bytes, FRAME_PREFIX_SIZE + 1);

  uint8_t prefix[FRAME_PREFIX_SIZE];
  std::memcpy(prefix, &FRAME_MAGIC, 4);
  std::memcpy(prefix + 4, &wire_bytes, 4);

  Ptr<Packet> frame = Create<Packet>(prefix, FRAME_PREFIX_SIZE);
  frame->AddAtEnd(Create<Packet>(wire_bytes - FRAME_PREFIX_SIZE));

  NS_ABORT_MSG_IF(MpiInterface::GetSize() > 1 &&
                      serialized.size() > MPI_PAYLOAD_LIMIT,
                  "Message payload of " << serialized.size()
                                        << " bytes exceeds the MPI buffer; "
                                           "patch MAX_MPI_MSG_SIZE in ns-3");

  GhostDagPayloadTag tag;
  tag.payload = serialized;
  frame->AddByteTag(tag, wire_bytes - 1, wire_bytes);

  m_tx_queue[socket].push_back(frame);
  HandleSendReady(socket, socket->GetTxAvailable());
}

// Writes queued frames into the TCP send buffer as space frees up; a frame
// larger than the free space is split and its remainder stays queued.
void GhostDagNode::HandleSendReady(Ptr<Socket> socket, uint32_t) {
  auto qit = m_tx_queue.find(socket);
  if (qit == m_tx_queue.end()) {
    return;
  }
  auto &queue = qit->second;

  while (!queue.empty()) {
    uint32_t available = socket->GetTxAvailable();
    if (available == 0) {
      return;
    }

    Ptr<Packet> head = queue.front();
    uint32_t size = head->GetSize();
    if (size <= available) {
      if (socket->Send(head) < 0) {
        return;
      }
      queue.pop_front();
    } else {
      if (socket->Send(head->CreateFragment(0, available)) < 0) {
        return;
      }
      queue.front() = head->CreateFragment(available, size - available);
    }
  }
}

/*
 * Message-size model
 *
 * Bitcoin message layout: 90-byte message header, 36-byte inventory entry,
 * 81-byte block header plus 32 bytes per parent reference, 522-byte
 * transaction. Graphene structures are sized by Graphene's size formula: a
 * Bloom filter of -n ln(f) / (8 ln^2 2) bytes and an IBLT of IBLT_CELL_SIZE
 * bytes per cell.
 * */

static uint32_t BloomWireSize(size_t n_items, double fpr) {
  if (n_items == 0 || fpr <= 0.0 || fpr >= 1.0) {
    return 0;
  }
  return static_cast<uint32_t>(
      std::ceil(-static_cast<double>(n_items) * std::log(fpr) /
                (8.0 * LN2_SQUARED)));
}

static uint32_t IbltWireSize(const IBLT &iblt) {
  return static_cast<uint32_t>(GrapheneProtocol::IBLT_VALUE_SIZE *
                               iblt.hashTableSize());
}

uint32_t GhostDagNode::WireSizeInv() const {
  return m_message_header_size + m_inventory_size;
}

uint32_t GhostDagNode::WireSizeBlock(const Block &block) const {
  return m_message_header_size + m_headers_size +
         m_parent_hash_size * block.header.parent_hashes.size() +
         m_transaction_size * block.transactions.size();
}

uint32_t GhostDagNode::WireSizeGrapheneBlock(size_t n_parents, size_t n_txs,
                                             double fpr,
                                             const IBLT &iblt) const {
  return m_message_header_size + m_headers_size +
         m_parent_hash_size * n_parents + BloomWireSize(n_txs, fpr) +
         IbltWireSize(iblt);
}

uint32_t GhostDagNode::WireSizeRecoveryRequest(size_t z, double fpr_r) const {
  return m_message_header_size + BloomWireSize(z, fpr_r);
}

uint32_t GhostDagNode::WireSizeRecoveryResponse(const IBLT &iblt,
                                                size_t n_missing) const {
  return m_message_header_size + IbltWireSize(iblt) +
         m_transaction_size * n_missing;
}

uint32_t GhostDagNode::WireSizeTxInv(size_t n_txs) const {
  return m_message_header_size + m_inventory_size * n_txs;
}

uint32_t GhostDagNode::WireSizeTxs(size_t n_txs) const {
  return m_message_header_size + m_transaction_size * n_txs;
}

void GhostDagNode::ProcessMessage(enum Messages msg_type,
                                  const std::string &payload, Address &from,
                                  uint32_t wire_bytes) {
  auto data = nlohmann::json::parse(payload, nullptr, false);
  if (data.is_discarded()) {
    NS_LOG_ERROR("Node " << GetNode()->GetId()
                         << " failed to parse JSON payload");
    return;
  }

  switch (msg_type) {
  case INV_RELAY_BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received INV_RELAY_BLOCK");
    if (data.contains("block_hash")) {
      std::string block_hash = data["block_hash"];
      HandleInvRelayBlock(block_hash, from);
    }
    break;
  }
  case REQ_RELAY_BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received REQ_RELAY_BLOCK");
    if (data.contains("block_hash")) {
      std::string block_hash = data["block_hash"];
      bool graphene_failed = data.value("graphene_failed", false);
      uint64_t mempool_count = data.value("mempool_count", uint64_t{0});
      HandleReqRelayBlock(block_hash, graphene_failed, from, mempool_count);
    }
    break;
  }
  case BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received BLOCK");
    if (data.contains("block")) {
      Block newBlock;
      auto &blockData = data["block"];
      newBlock.header.block_id = blockData.value("block_id", uint64_t{0});
      newBlock.header.miner_id = blockData.value("miner_id", uint64_t{0});
      newBlock.header.time_created = blockData.value("time_created", 0.0);

      if (blockData.contains("parent_hashes")) {
        for (auto &ph : blockData["parent_hashes"]) {
          newBlock.header.parent_hashes.push_back(ph.get<uint64_t>());
        }
      }

      if (blockData.contains("transactions")) {
        for (auto &txData : blockData["transactions"]) {
          Transaction tx;
          tx.tx_id = txData.value("tx_id", uint64_t{0});
          tx.size_bytes = txData.value("size_bytes", uint32_t{522});
          tx.fee = txData.value("fee", uint32_t{0});
          newBlock.transactions.insert(tx);
        }
      }

      EVENT_MSG_RECV(NID, IPV4_STR(from), "block", newBlock.header.block_id,
                     wire_bytes);

      HandleBlock(newBlock, from);
    }
    break;
  }
  case GRAPHENE_BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received GRAPHENE_BLOCK");
    uint64_t block_id = data.value("block_id", uint64_t{0});
    EVENT_MSG_RECV(NID, IPV4_STR(from), "graphene_block", block_id,
                   wire_bytes);
    HandleGrapheneBlock(data, from);
    break;
  }
  case GRAPHENE_RECOVERY_REQUEST: {
    NS_LOG_INFO("Node " << GetNode()->GetId()
                        << " received GRAPHENE_RECOVERY_REQUEST");
    HandleGrapheneRecoveryRequest(data, from);
    break;
  }
  case GRAPHENE_RECOVERY_RESPONSE: {
    NS_LOG_INFO("Node " << GetNode()->GetId()
                        << " received GRAPHENE_RECOVERY_RESPONSE");

    std::string block_hash = data["block_hash"];
    uint64_t block_id = std::stoull(block_hash);
    EVENT_MSG_RECV(NID, IPV4_STR(from), "graphene_recovery_response", block_id,
                   wire_bytes);

    HandleGrapheneRecoveryResponse(data, from);
    break;
  }
  case INV_TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received INV_TRANSACTIONS");
    std::vector<std::string> tx_hashes;
    if (data.contains("tx_hashes")) {
      for (auto &tx_hash : data["tx_hashes"]) {
        tx_hashes.emplace_back(tx_hash);
      }
    }
    HandleInvTransactions(tx_hashes, from);
    break;
  }
  case REQ_TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received REQ_TRANSACTIONS");
    std::vector<std::string> tx_hashes;
    if (data.contains("tx_hashes")) {
      for (const auto &h : data["tx_hashes"]) {
        tx_hashes.emplace_back(h.get<std::string>());
      }
    }
    HandleReqTransactions(tx_hashes, from);
    break;
  }
  case TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received TRANSACTIONS");
    std::vector<Transaction> txs;
    if (data.contains("transactions")) {
      for (const auto &txData : data["transactions"]) {
        Transaction tx;
        tx.tx_id = txData.value("tx_id", uint64_t{0});
        tx.size_bytes = txData.value("size_bytes", uint32_t{522});
        tx.fee = txData.value("fee", uint32_t{0});
        txs.push_back(tx);
      }
    }
    HandleTransactions(txs, from);
    break;
  }
  default:
    NS_LOG_ERROR("Node: " << GetNode()->GetId()
                          << " Received unknown message: " << payload);
    break;
  }
}
/*
 * Block Handling Logic
 * */

void GhostDagNode::HandleInvRelayBlock(const std::string &block_hash,
                                       Address &from) {
  uint64_t block_id = std::stoul(block_hash);
  if (m_blockchain.HasBlock(block_id) || m_blockchain.IsOrphan(block_id)) {
    return;
  }

  bool first_announcement = m_queue_inv[block_hash].empty();
  m_queue_inv[block_hash].push_back(from);

  if (first_announcement) {
    EVENT_MSG_RECV(NID, IPV4_STR(from), "inv_block", block_id, WireSizeInv());

    nlohmann::json req;
    req["block_hash"] = block_hash;
    if (m_graphene_enabled) {
      req["mempool_count"] = static_cast<uint64_t>(m_mempool.size());
    }
    SendMessage(NO_MESSAGE, REQ_RELAY_BLOCK, req.dump(), from, WireSizeInv());
    EVENT_MSG_SENT(NID, IPV4_STR(from), "getdata", block_id, WireSizeInv());

    m_inv_timeouts[block_hash] =
        Simulator::Schedule(m_inv_timeout_minutes,
                            &GhostDagNode::InvTimeoutExpired, this, block_hash);
  }
}

void GhostDagNode::HandleReqRelayBlock(const std::string &block_hash,
                                       bool graphene_failed, Address &from,
                                       uint64_t receiver_mempool_count) {
  NS_LOG_FUNCTION(this << block_hash);

  uint64_t block_id = std::stoul(block_hash);

  const Block *block_ptr = nullptr;
  if (m_blockchain.HasBlock(block_id)) {
    block_ptr = &m_blockchain.blocks.at(block_id);
  } else if (m_blockchain.IsOrphan(block_id)) {
    auto oit = m_blockchain.orphans.find(block_id);
    if (oit != m_blockchain.orphans.end()) {
      block_ptr = &oit->second;
    }
  }
  if (block_ptr == nullptr) {
    return;
  }
  const Block &block = *block_ptr;

  // If graphene is enabled, send a compact graphene block instead of full.
  if (m_graphene_enabled && !graphene_failed) {
    bloom_filter bf{bloom_parameters()};
    IBLT iblt(1, GrapheneProtocol::IBLT_VALUE_SIZE, 1.0f, 2);
    uint64_t mc = receiver_mempool_count;
    double fpr = 0.0;
    if (GrapheneProtocol::BuildSenderComponents(block.transactions, mc, bf,
                                                iblt, fpr)) {
      uint64_t tx_checksum = 0;
      for (const auto &tx : block.transactions) {
        tx_checksum ^= tx.tx_id;
      }

      nlohmann::json gm;
      gm["block_hash"] = block_hash;
      gm["block_id"] = block.header.block_id;
      gm["miner_id"] = block.header.miner_id;
      gm["time_created"] = block.header.time_created;
      gm["parent_hashes"] = nlohmann::json::array();
      for (uint64_t parent : block.header.parent_hashes) {
        gm["parent_hashes"].push_back(parent);
      }
      gm["tx_count"] = block.transactions.size();
      gm["bloom_filter"] = GrapheneProtocol::SerializeBloomFilter(bf);
      gm["iblt"] = GrapheneProtocol::SerializeIBLT(iblt);
      gm["tx_checksum"] = tx_checksum;
      gm["fpr"] = bf.effective_fpp();

      SendMessage(NO_MESSAGE, GRAPHENE_BLOCK, gm.dump(), from,
                  WireSizeGrapheneBlock(block.header.parent_hashes.size(),
                                        block.transactions.size(), fpr, iblt));
      return;
    }
    // Receiver mempool too small for Graphene to pay off: fall through to the
    // full block relay below.
  }

  // Fallback: send full block
  nlohmann::json blockMsg;
  blockMsg["block"]["block_id"] = block.header.block_id;
  blockMsg["block"]["miner_id"] = block.header.miner_id;
  blockMsg["block"]["time_created"] = block.header.time_created;
  blockMsg["block"]["parent_hashes"] = nlohmann::json::array();
  for (uint64_t parent : block.header.parent_hashes) {
    blockMsg["block"]["parent_hashes"].push_back(parent);
  }

  blockMsg["block"]["transactions"] = nlohmann::json::array();
  for (const auto &tx : block.transactions) {
    nlohmann::json txJson;
    txJson["tx_id"] = tx.tx_id;
    txJson["size_bytes"] = tx.size_bytes;
    txJson["fee"] = tx.fee;
    blockMsg["block"]["transactions"].push_back(txJson);
  }

  SendMessage(NO_MESSAGE, BLOCK, blockMsg.dump(), from, WireSizeBlock(block));
}

void GhostDagNode::HandleBlock(const Block &new_block, Address &from) {
  NS_LOG_FUNCTION(this << new_block.header.block_id);

  double currentTime = Simulator::Now().GetSeconds();
  Block block = new_block;
  block.time_received = currentTime;
  InetSocketAddress peer = InetSocketAddress::ConvertFrom(from);
  block.received_from = peer.GetIpv4();

  if (m_blockchain.HasBlock(new_block.header.block_id) ||
      m_blockchain.IsOrphan(new_block.header.block_id)) {
    return; // should never happen for first annouce rule
  }

  uint32_t already_known_txs = 0;
  for (const auto &tx : block.transactions) {
    uint32_t miner_id = IdFromTxId(tx.tx_id);
    HtabIterator it = m_mempool.find(miner_id, tx.tx_id);
    if (it.isValid()) {
      already_known_txs++;
    }
    m_known_txs.insert(tx.tx_id);
  }

  EVENT_BLOCK_RECEIVED(NID, new_block.header.block_id, IPV4_STR(from),
                       WireSizeBlock(new_block), new_block.transactions.size(),
                       already_known_txs, new_block.header.parent_hashes.size(),
                       new_block.header.time_created);

  std::string blockHash = std::to_string(new_block.header.block_id);
  auto timeout_it = m_inv_timeouts.find(blockHash);
  if (timeout_it != m_inv_timeouts.end()) {
    Simulator::Cancel(timeout_it->second);
    m_inv_timeouts.erase(timeout_it);
  }
  m_queue_inv.erase(blockHash);

  auto before_orphans = m_blockchain.orphans;
  std::map<uint64_t, bool> was_blue;
  for (auto &[id, blk] : m_blockchain.blocks)
    was_blue[id] = blk.is_blue;

  m_blockchain.AddBlock(block);

  {
    uint64_t sibling_count = 0;
    for (uint64_t tip : m_blockchain.tips)
      if (tip != block.header.block_id)
        ++sibling_count;

    double sibling_overlap = 0.0;
    if (!block.transactions.empty() && sibling_count > 0) {
      std::set<uint64_t> sibling_tx_ids;
      for (uint64_t tip : m_blockchain.tips)
        if (tip != block.header.block_id)
          for (const auto &tx : m_blockchain.blocks[tip].transactions)
            sibling_tx_ids.insert(tx.tx_id);
      uint64_t overlap = 0;
      for (const auto &tx : block.transactions)
        if (sibling_tx_ids.count(tx.tx_id))
          ++overlap;
      sibling_overlap = (double)overlap / block.transactions.size();
    }

    EVENT_BLOCK_TX_COMPETITION(NID, block.header.block_id, sibling_overlap,
                               sibling_count);
  }

  if (m_blockchain.IsOrphan(block.header.block_id)) {
    std::vector<uint64_t> missing_parents;
    for (uint64_t p : block.header.parent_hashes)
      if (!m_blockchain.HasBlock(p))
        missing_parents.push_back(p);
    EVENT_BLOCK_ORPHANED(NID, block.header.block_id, missing_parents);
  }

  for (auto &[oid, _] : before_orphans)
    if (!m_blockchain.IsOrphan(oid))
      EVENT_BLOCK_UNORPHANED(NID, oid);

  if (m_blockchain.HasBlock(block.header.block_id)) {
    uint64_t bid = block.header.block_id;
    EVENT_BLOCK_COLORED(NID, bid, m_blockchain.blocks[bid].is_blue,
                        m_blockchain.blocks[bid].blue_score,
                        m_blockchain.GetDagWidth());
  }

  for (auto &[id, blk] : m_blockchain.blocks) {
    auto prev = was_blue.find(id);
    bool prev_blue = (prev != was_blue.end()) && prev->second;

    if (blk.is_blue && !prev_blue) {
      EVENT_BLOCK_COLORED(NID, id, blk.is_blue, blk.blue_score,
                          m_blockchain.GetDagWidth());
      for (const auto &tx : blk.transactions) {
        uint32_t miner_id = IdFromTxId(tx.tx_id);
        HtabIterator it = m_mempool.find(miner_id, tx.tx_id);
        if (it.isValid())
          m_mempool.eraseTransaction(it);
      }
    }
  }

  BroadcastInvBlock(blockHash, peer.GetIpv4());
}

// =========================================================================
//  Graphene block propagation
// =========================================================================

void GhostDagNode::HandleGrapheneBlock(const nlohmann::json &data,
                                       Address &from) {
  std::string block_hash = data["block_hash"].get<std::string>();
  NS_LOG_FUNCTION(this << block_hash);

  uint64_t block_id = data["block_id"].get<uint64_t>();

  if (m_blockchain.HasBlock(block_id) || m_blockchain.IsOrphan(block_id))
    return;

  if (m_graphene_state.count(block_hash) > 0)
    return;

  auto result = GrapheneProtocol::ProcessIncomingBlock(
      data, m_mempool.getAllEntries(), m_mempool.size());

  if (result.success) {
    HandleBlock(result.block, from);
    EVENT_BLOCK_GRAPHENE_SUCCESS(NID, result.block.header.block_id,
                                 IPV4_STR(from));
    return;
  }

  m_graphene_state[block_hash] = std::move(result.recovery_state);
  m_graphene_senders[block_hash] = from;
  m_graphene_timeouts[block_hash] = Simulator::Schedule(
      m_graphene_recovery_timeout, &GhostDagNode::GrapheneRecoveryTimeout, this,
      block_hash);
  uint32_t request_bytes =
      WireSizeRecoveryRequest(result.recovery_z, result.recovery_fpr);
  SendMessage(GRAPHENE_RECOVERY_REQUEST, GRAPHENE_RECOVERY_REQUEST,
              result.recovery_request.dump(), from, request_bytes);
  EVENT_MSG_SENT(NID, IPV4_STR(from), "graphene_recovery_request", block_id,
                 request_bytes);
}

void GhostDagNode::HandleGrapheneRecoveryRequest(const nlohmann::json &data,
                                                 Address &from) {

  std::string block_hash = data["block_hash"];

  int b = data["b"];
  int y_star = data["y_star"];

  bloom_filter receiver_bloom =
      GrapheneProtocol::DeserializeBloomFilter(data["receiver_bloom"]);

  uint64_t block_id = std::stoull(block_hash);

  const Block *blk_ptr = nullptr;
  if (m_blockchain.HasBlock(block_id)) {
    blk_ptr = &m_blockchain.blocks.at(block_id);
  } else if (m_blockchain.IsOrphan(block_id)) {
    auto oit = m_blockchain.orphans.find(block_id);
    if (oit != m_blockchain.orphans.end()) {
      blk_ptr = &oit->second;
    }
  }
  if (blk_ptr == nullptr) {
    return;
  }

  const Block &blk = *blk_ptr;

  std::vector<uint64_t> missing;

  IBLT sender_second_iblt =
      GrapheneProtocol::SecondIBLT(blk, receiver_bloom, y_star, b, missing);

  nlohmann::json resp;
  uint64_t tx_checksum = 0;
  for (const auto &tx : blk.transactions) {
    tx_checksum ^= tx.tx_id;
  }

  resp["block_hash"] = block_hash;
  resp["tx_checksum"] = tx_checksum;
  resp["iblt"] = GrapheneProtocol::SerializeIBLT(sender_second_iblt);

  resp["missing"] = missing;

  SendMessage(GRAPHENE_RECOVERY_RESPONSE, GRAPHENE_RECOVERY_RESPONSE,
              resp.dump(), from,
              WireSizeRecoveryResponse(sender_second_iblt, missing.size()));
}

void GhostDagNode::HandleGrapheneRecoveryResponse(const nlohmann::json &data,
                                                  Address &from) {

  std::string block_hash = data["block_hash"];

  auto it = m_graphene_state.find(block_hash);

  if (it == m_graphene_state.end()) {
    return;
  }
  auto &state = it->second;

  auto result = GrapheneProtocol::ProcessRecoveryResponse(data, state);

  if (result.success) {
    Block recovered;
    recovered.header = state.header;
    for (auto txid : result.block_txids) {
      Transaction tx;
      tx.tx_id = txid;
      tx.size_bytes = 522;

      HtabIterator txit = m_mempool.find(IdFromTxId(txid), txid);
      if (txit.isValid()) {
        tx.fee = txit.iterator->fee;
      }

      recovered.transactions.insert(tx);
      m_known_txs.insert(txid);
    }

    HandleBlock(recovered, from);
    m_graphene_state.erase(it);
    auto te = m_graphene_timeouts.find(block_hash);
    if (te != m_graphene_timeouts.end()) {
      Simulator::Cancel(te->second);
      m_graphene_timeouts.erase(te);
    }
    m_graphene_senders.erase(block_hash);
    EVENT_BLOCK_GRAPHENE_SUCCESS2(NID, recovered.header.block_id,
                                  IPV4_STR(from));
    return;
  }

  EVENT_BLOCK_GRAPHENE_FALLBACK(NID, state.header.block_id, IPV4_STR(from),
                                "p2_failed");

  m_graphene_state.erase(it);
  auto te = m_graphene_timeouts.find(block_hash);
  if (te != m_graphene_timeouts.end()) {
    Simulator::Cancel(te->second);
    m_graphene_timeouts.erase(te);
  }
  m_graphene_senders.erase(block_hash);

  nlohmann::json msg;
  msg["block_hash"] = block_hash;
  msg["graphene_failed"] = true;
  SendMessage(NO_MESSAGE, REQ_RELAY_BLOCK, msg.dump(), from, WireSizeInv());
  EVENT_MSG_SENT(NID, IPV4_STR(from), "getdata", std::stoull(block_hash),
                 WireSizeInv());
}

void GhostDagNode::GrapheneRecoveryTimeout(std::string block_hash) {
  auto it = m_graphene_state.find(block_hash);
  if (it == m_graphene_state.end()) {
    return;
  }

  uint64_t block_id = std::stoull(block_hash);
  if (m_blockchain.HasBlock(block_id) || m_blockchain.IsOrphan(block_id)) {
    m_graphene_state.erase(it);
    m_graphene_timeouts.erase(block_hash);
    m_graphene_senders.erase(block_hash);
    return;
  }

  auto sit = m_graphene_senders.find(block_hash);
  if (sit != m_graphene_senders.end()) {
    EVENT_BLOCK_GRAPHENE_FALLBACK(NID, it->second.header.block_id,
                                  IPV4_STR(sit->second), "timeout");

    nlohmann::json msg;
    msg["block_hash"] = block_hash;
    msg["graphene_failed"] = true;
    SendMessage(NO_MESSAGE, REQ_RELAY_BLOCK, msg.dump(), sit->second,
                WireSizeInv());
    EVENT_MSG_SENT(NID, IPV4_STR(sit->second), "getdata", block_id,
                   WireSizeInv());
    m_graphene_senders.erase(sit);
  }

  m_graphene_state.erase(it);
  m_graphene_timeouts.erase(block_hash);
}
// =========================================================================

void GhostDagNode::InvTimeoutExpired(std::string block_hash) {
  NS_LOG_FUNCTION(this << block_hash);

  auto it = m_queue_inv.find(block_hash);
  if (it == m_queue_inv.end())
    return;

  it->second.erase(it->second.begin());

  if (!it->second.empty()) {
    std::uniform_int_distribution<size_t> pick(0, it->second.size() - 1);
    size_t idx = pick(m_generator);
    if (idx != 0)
      std::swap(it->second[0], it->second[idx]);

    Address next = it->second.front();

    nlohmann::json req;
    req["block_hash"] = block_hash;
    if (m_graphene_enabled) {
      req["mempool_count"] = static_cast<uint64_t>(m_mempool.size());
    }
    SendMessage(NO_MESSAGE, REQ_RELAY_BLOCK, req.dump(), next, WireSizeInv());
    EVENT_MSG_SENT(NID, IPV4_STR(next), "getdata", std::stoull(block_hash),
                   WireSizeInv());

    m_inv_timeouts[block_hash] =
        Simulator::Schedule(m_inv_timeout_minutes,
                            &GhostDagNode::InvTimeoutExpired, this, block_hash);

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " retrying block "
                         << block_hash << " from next peer");
  } else {
    m_queue_inv.erase(it);
    m_inv_timeouts.erase(block_hash);
    NS_LOG_WARN("Node " << GetNode()->GetId() << " gave up fetching block "
                        << block_hash);
  }
}

void GhostDagNode::BroadcastInvBlock(const std::string &block_hash,
                                     Ipv4Address exclude) {
  NS_LOG_FUNCTION(this << block_hash);

  nlohmann::json inv;
  inv["block_hash"] = block_hash;

  for (const auto &peer_addr : m_peers_addresses) {
    if (peer_addr == exclude)
      continue;
    InetSocketAddress peer = InetSocketAddress(peer_addr, m_ghostdag_port);
    auto addr = Address(peer);
    SendMessage(NO_MESSAGE, INV_RELAY_BLOCK, inv.dump(), addr, WireSizeInv());
  }
}

/*
 * Transaction Logic
 * */

void GhostDagNode::HandleInvTransactions(
    const std::vector<std::string> &tx_hashes, Address &from) {

  NS_LOG_FUNCTION(this);

  std::vector<std::string> wanted;

  for (const auto &hash : tx_hashes) {
    uint64_t tx_id = std::stoull(hash);

    if (m_known_txs.count(tx_id)) {
      continue;
    }

    bool first_announcement = m_queue_inv_tx[hash].empty();
    m_queue_inv_tx[hash].push_back(from);

    if (first_announcement) {
      m_inv_tx_timeouts[hash] =
          Simulator::Schedule(m_inv_timeout_minutes,
                              &GhostDagNode::InvTxTimeoutExpired, this, hash);

      wanted.push_back(hash);
    }
  }

  if (wanted.empty()) {
    return;
  }

  nlohmann::json req;
  req["tx_hashes"] = wanted;
  SendMessage(NO_MESSAGE, REQ_TRANSACTIONS, req.dump(), from,
              WireSizeTxInv(wanted.size()));

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " requested " << wanted.size()
                       << " txs in one REQ");
}

void GhostDagNode::HandleReqTransactions(
    const std::vector<std::string> &tx_hashes, Address &from) {

  NS_LOG_FUNCTION(this);

  nlohmann::json msg;
  msg["transactions"] = nlohmann::json::array();
  uint64_t found_count = 0;

  for (const auto &hash : tx_hashes) {
    uint64_t tx_id = std::stoull(hash);
    uint32_t node_id = IdFromTxId(tx_id);

    HtabIterator it = m_mempool.find(node_id, tx_id);
    if (!it.isValid()) {
      continue; // Evicted or never had it
    }

    nlohmann::json txJson;
    txJson["tx_id"] = it.iterator->txId;
    txJson["size_bytes"] = static_cast<uint32_t>(m_average_transaction_size);
    txJson["fee"] = it.iterator->fee;
    msg["transactions"].push_back(txJson);
    found_count++;
  }

  if (found_count == 0) {
    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " had none of the "
                         << tx_hashes.size() << " requested txs");
    return;
  }

  SendMessage(NO_MESSAGE, TRANSACTIONS, msg.dump(), from,
              WireSizeTxs(found_count));

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " replied with " << found_count
                       << "/" << tx_hashes.size()
                       << " txs in one TRANSACTION message");
}

void GhostDagNode::HandleTransactions(const std::vector<Transaction> &txs,
                                      Address &from) {
  NS_LOG_FUNCTION(this);

  InetSocketAddress sender = InetSocketAddress::ConvertFrom(from);

  for (const auto &tx : txs) {
    std::string hash = std::to_string(tx.tx_id);

    auto t_it = m_inv_tx_timeouts.find(hash);
    if (t_it != m_inv_tx_timeouts.end()) {
      Simulator::Cancel(t_it->second);
      m_inv_tx_timeouts.erase(t_it);
    }
    m_queue_inv_tx.erase(hash);

    if (m_known_txs.count(tx.tx_id)) {
      continue;
    }

    m_known_txs.insert(tx.tx_id);

    if (m_mempool.size() >= static_cast<size_t>(m_mempoolSize)) {
      NS_LOG_DEBUG("Node " << GetNode()->GetId()
                           << " mempool full, dropping tx " << tx.tx_id);
      continue;
    }

    uint32_t node_id = IdFromTxId(tx.tx_id);
    uint32_t fee =
        tx.fee > 0 ? tx.fee
                   : static_cast<uint32_t>(m_txFeeDistribution(m_generator));
    m_mempool.insert(node_id, tx.tx_id, fee);

    m_pending_inv_tx.push_back(hash);

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " accepted tx " << tx.tx_id
                         << " fee=" << fee);
  }

  if (!m_pending_inv_tx.empty() && !m_invBatchEvent.IsPending()) {
    m_invBatchEvent = Simulator::Schedule(Seconds(INV_BATCH_INTERVAL_S),
                                          &GhostDagNode::FlushInvBatch, this,
                                          sender.GetIpv4());
  }
}

void GhostDagNode::FlushInvBatch(Ipv4Address exclude) {
  NS_LOG_FUNCTION(this);

  if (m_pending_inv_tx.empty()) {
    return;
  }

  std::vector<std::string> batch;
  batch.swap(m_pending_inv_tx);

  BroadcastInvTransactions(batch, exclude);

  NS_LOG_DEBUG("Node " << GetNode()->GetId()
                       << " flushed INV batch: " << batch.size() << " txs");
}

void GhostDagNode::InvTxTimeoutExpired(std::string tx_hash) {
  NS_LOG_FUNCTION(this << tx_hash);

  auto it = m_queue_inv_tx.find(tx_hash);
  if (it == m_queue_inv_tx.end()) {
    return;
  }

  it->second.erase(it->second.begin());

  if (!it->second.empty()) {
    std::uniform_int_distribution<size_t> pick(0, it->second.size() - 1);
    size_t idx = pick(m_generator);
    if (idx != 0)
      std::swap(it->second[0], it->second[idx]);
    Address next = it->second.front();

    nlohmann::json req;
    req["tx_hashes"] = std::vector<std::string>{tx_hash};
    SendMessage(NO_MESSAGE, REQ_TRANSACTIONS, req.dump(), next,
                WireSizeTxInv(1));

    m_inv_tx_timeouts[tx_hash] =
        Simulator::Schedule(m_inv_timeout_minutes,
                            &GhostDagNode::InvTxTimeoutExpired, this, tx_hash);

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " retrying tx " << tx_hash
                         << " from next peer");
  } else {
    m_queue_inv_tx.erase(it);
    m_inv_tx_timeouts.erase(tx_hash);
    NS_LOG_WARN("Node " << GetNode()->GetId() << " gave up fetching tx "
                        << tx_hash);
  }
}

void GhostDagNode::BroadcastInvTransactions(
    const std::vector<std::string> &tx_hashes, Ipv4Address exclude) {

  if (tx_hashes.empty()) {
    return;
  }

  nlohmann::json inv;
  inv["tx_hashes"] = tx_hashes;
  std::string payload = inv.dump();

  for (const auto &peer_addr : m_peers_addresses) {
    if (peer_addr == exclude) {
      continue;
    }
    InetSocketAddress peer(peer_addr, m_ghostdag_port);
    Address addr(peer);
    SendMessage(NO_MESSAGE, INV_TRANSACTIONS, payload, addr,
                WireSizeTxInv(tx_hashes.size()));
  }
}

void GhostDagNode::GenerateTransaction() {
  NS_LOG_FUNCTION(this);

  uint64_t txId =
      (static_cast<uint64_t>(GetNode()->GetId()) << 32) | m_txsGenerated;
  auto fee = static_cast<uint32_t>(m_txFeeDistribution(m_generator));

  if (m_mempool.size() < static_cast<size_t>(m_mempoolSize)) {
    m_mempool.insert(GetNode()->GetId(), txId, fee);
    m_known_txs.insert(txId);
    m_txsGenerated++;

    m_pending_inv_tx.push_back(std::to_string(txId));

    if (!m_invBatchEvent.IsPending()) {
      m_invBatchEvent = Simulator::Schedule(Seconds(INV_BATCH_INTERVAL_S),
                                            &GhostDagNode::FlushInvBatch, this,
                                            Ipv4Address());
    }

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " generated tx " << txId
                         << " fee=" << fee);
  } else {
    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " mempool full");
  }

  ScheduleNextTxGeneration();
}

void GhostDagNode::ScheduleNextTxGeneration() {
  std::exponential_distribution<double> txRate(1.0 / m_txGenInterval);
  double nextTxTime = txRate(m_generator);

  NS_LOG_DEBUG("Node " << GetNode()->GetId()
                       << " will generate next transaction in " << nextTxTime
                       << "s");

  m_nextTxGenerationEvent = Simulator::Schedule(
      Seconds(nextTxTime), &GhostDagNode::GenerateTransaction, this);
}

void GhostDagNode::StartTransactionGeneration() {
  if (!m_generateTransactions) {
    return;
  }

  NS_LOG_INFO("Node " << GetNode()->GetId()
                      << " starting transaction generation (mempool size: "
                      << m_mempoolSize << ", tx fee lambda: " << m_txFeeLambda
                      << ")");

  ScheduleNextTxGeneration();
}

void GhostDagNode::StopTransactionGeneration() {
  if (m_nextTxGenerationEvent.IsPending()) {
    Simulator::Cancel(m_nextTxGenerationEvent);
  }

  NS_LOG_INFO("Node " << GetNode()->GetId() << " generated " << m_txsGenerated
                      << " transactions");
}

/*
 * ns3 life cycle managment
 * */
bool GhostDagNode::HandleConnectionRequest(Ptr<Socket> s, const Address &from) {
  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " accepting connection from "
                       << from);
  return true; // Explicitly accept the connection
}

void GhostDagNode::HandlePeerClose(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
}

void GhostDagNode::HandlePeerError(Ptr<Socket> socket) {
  NS_LOG_FUNCTION(this << socket);
}

void GhostDagNode::HandleAccept(Ptr<Socket> s, const Address &from) {
  NS_LOG_FUNCTION(this << s << from);
  s->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
}

Ptr<Socket> GhostDagNode::CreatePeerSocket(Ipv4Address peer_addr) {
  auto sock = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
  m_socket_to_peer[sock] = peer_addr;
  m_peers_sockets[peer_addr] = sock;

  m_pending_messages.emplace(peer_addr,
                             std::vector<std::pair<std::string, uint32_t>>{});

  sock->SetConnectCallback(
      MakeCallback(&GhostDagNode::HandleConnectionSucceeded, this),
      MakeCallback(&GhostDagNode::HandleConnectionFailed, this));
  sock->SetSendCallback(MakeCallback(&GhostDagNode::HandleSendReady, this));
  sock->Connect(InetSocketAddress(peer_addr, m_ghostdag_port));
  return sock;
}

void GhostDagNode::HandleConnectionSucceeded(Ptr<Socket> socket) {
  auto map_it = m_socket_to_peer.find(socket);
  if (map_it == m_socket_to_peer.end())
    return;

  Ipv4Address peer_addr = map_it->second;

  auto pending_it = m_pending_messages.find(peer_addr);
  if (pending_it != m_pending_messages.end()) {
    for (const auto &[serialized, wire_bytes] : pending_it->second) {
      EnqueueFrame(socket, serialized, wire_bytes);
    }
    m_pending_messages.erase(pending_it);
  }
}

void GhostDagNode::HandleConnectionFailed(Ptr<Socket> socket) {
  auto map_it = m_socket_to_peer.find(socket);
  if (map_it == m_socket_to_peer.end())
    return;

  Ipv4Address peer_addr = map_it->second;
  NS_LOG_WARN("Node " << GetNode()->GetId() << ": connection FAILED to "
                      << peer_addr);

  m_peers_sockets.erase(peer_addr);
  m_pending_messages.erase(peer_addr);
  m_socket_to_peer.erase(map_it);
}

void GhostDagNode::ScheduleSnapshot() {
  if (m_snapshotInterval <= 0.0)
    return;
  m_snapshotEvent = Simulator::Schedule(Seconds(m_snapshotInterval),
                                        &GhostDagNode::EmitDagSnapshot, this);
}

void GhostDagNode::EmitDagSnapshot() {
  uint64_t total = m_blockchain.blocks.size();
  uint64_t blue_count = 0;

  for (auto &[id, blk] : m_blockchain.blocks)
    if (blk.is_blue)
      ++blue_count;

  uint64_t red_count = total - blue_count;
  double red_ratio = total > 0 ? (double)red_count / total : 0.0;

  EVENT_DAG_SNAPSHOT(NID, total, blue_count, red_count, red_ratio,
                     m_blockchain.GetDagWidth(), m_mempool.size());

  ScheduleSnapshot(); // reschedule — keeps firing every interval
}

} // namespace ns3
