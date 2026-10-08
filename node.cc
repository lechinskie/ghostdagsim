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
NS_OBJECT_ENSURE_REGISTERED(GhostDagFrameHeader);

/*
 * Framing
 *
 * Every message is one frame of exactly its modelled wire size: a 16-byte
 * prefix (magic, frame length, inline payload length, reserved) followed by
 * the CBOR payload and zero padding up to the modelled size, so the link
 * carries the modelled number of bytes regardless of how the payload is
 * encoded. The padding is a zero area of the ns-3 buffer and costs no memory.
 *
 * Only when the payload does not fit the modelled size (Graphene messages,
 * whose Bloom filter and IBLT encodings are larger than their modelled
 * sizes) it travels instead as a byte tag on the frame's last byte, which is
 * handed to TCP as its own one-byte packet. Byte tags are kept out of the
 * common path on purpose: ns-3 sizes every new byte-tag list as the largest
 * one it has ever freed (ByteTagList::Allocate), so a single large payload
 * tag makes every later tagged packet, however small, cost that much memory.
 * */

static constexpr uint32_t FRAME_MAGIC = 0x47444147; // "GDAG"
static constexpr uint32_t FRAME_PREFIX_SIZE = 16;
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

TypeId GhostDagFrameHeader::GetTypeId() {
  static TypeId tid = TypeId("ns3::GhostDagFrameHeader")
                          .SetParent<Header>()
                          .SetGroupName("Applications")
                          .AddConstructor<GhostDagFrameHeader>();
  return tid;
}

TypeId GhostDagFrameHeader::GetInstanceTypeId() const { return GetTypeId(); }

uint32_t GhostDagFrameHeader::GetSerializedSize() const {
  return FRAME_PREFIX_SIZE + static_cast<uint32_t>(payload.size());
}

void GhostDagFrameHeader::Serialize(Buffer::Iterator i) const {
  i.WriteHtolsbU32(FRAME_MAGIC);
  i.WriteHtolsbU32(frame_len);
  i.WriteHtolsbU32(static_cast<uint32_t>(payload.size()));
  i.WriteHtolsbU32(seq);
  i.Write(reinterpret_cast<const uint8_t *>(payload.data()),
          static_cast<uint32_t>(payload.size()));
}

uint32_t GhostDagFrameHeader::Deserialize(Buffer::Iterator i) {
  i.ReadLsbtohU32(); // magic
  frame_len = i.ReadLsbtohU32();
  uint32_t len = i.ReadLsbtohU32();
  seq = i.ReadLsbtohU32();
  payload.resize(len);
  i.Read(reinterpret_cast<uint8_t *>(payload.data()), len);
  return GetSerializedSize();
}

void GhostDagFrameHeader::Print(std::ostream &os) const {
  os << "frame_len=" << frame_len << " inline=" << payload.size() << "B";
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
          .AddAttribute("Seed",
                        "Replication seed mixed into the node's generator "
                        "(0 = seeded by node id only, the original behaviour)",
                        UintegerValue(0),
                        MakeUintegerAccessor(&GhostDagNode::m_seed),
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
  m_seed = 0;
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
  m_running = true;
  m_blockchain.ghostdag_k = static_cast<int>(m_ghostdag_k);
  m_blockchain.node_id_metric = GetNode()->GetId();
  m_txFeeDistribution =
      std::exponential_distribution<double>(1.0 / m_txFeeLambda);

  // Deterministic per node; --seed shifts every node's stream (seed 0 keeps
  // the original expression).
  m_generator.seed(static_cast<uint32_t>(GetNode()->GetId()) * 2654435761u +
                   m_seed * 0x9E3779B9u);

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
  m_running = false;

  // No more traffic in either direction: a frame half-written into TCP would
  // otherwise be followed by fresh frames and desynchronise the peer's stream.
  for (auto &[addr, sock] : m_peers_sockets) {
    sock->SetSendCallback(MakeNullCallback<void, Ptr<Socket>, uint32_t>());
    sock->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    sock->Close();
  }
  for (auto &sock : m_accepted_sockets) {
    sock->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    sock->Close();
  }
  m_accepted_sockets.clear();
  m_socket_to_peer.clear();
  m_peers_sockets.clear();
  m_pending_messages.clear();
  m_tx_queue.clear();
  m_tx_frame_seq.clear();
  m_rx_state.clear();

  if (m_socket) {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    m_socket->SetAcceptCallback(
        MakeNullCallback<bool, Ptr<Socket>, const Address &>(),
        MakeNullCallback<void, Ptr<Socket>, const Address &>());
  }

  NS_LOG_WARN("\n\nGHOSTDAG NODE " << GetNode()->GetId() << ":");
  NS_LOG_WARN("Total Blocks in DAG = " << m_blockchain.blocks.size());

  if (m_snapshotEvent.IsPending())
    Simulator::Cancel(m_snapshotEvent);
  if (m_txTimeoutEvent.IsPending())
    Simulator::Cancel(m_txTimeoutEvent);
  if (m_invBatchEvent.IsPending())
    Simulator::Cancel(m_invBatchEvent);
  for (auto &[hash, ev] : m_inv_timeouts)
    Simulator::Cancel(ev);
  m_inv_timeouts.clear();
  for (auto &[hash, ev] : m_graphene_timeouts)
    Simulator::Cancel(ev);
  m_graphene_timeouts.clear();
  StopTransactionGeneration();
}

void GhostDagNode::HandleRead(Ptr<Socket> socket) {
  if (!m_running) {
    return;
  }
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
        payloads[item.GetEnd()] = std::move(tag.payload);
      }
    }

    RxFrameState &st = m_rx_state[socket];
    uint32_t pos = 0;
    while (pos < size) {
      if (st.prefix_have < FRAME_PREFIX_SIZE) {
        uint32_t take =
            std::min(FRAME_PREFIX_SIZE - st.prefix_have, size - pos);
        packet->CreateFragment(pos, take)->CopyData(st.prefix + st.prefix_have,
                                                    take);
        st.prefix_have += take;
        pos += take;
        if (st.prefix_have == FRAME_PREFIX_SIZE) {
          uint32_t magic, len, payload_len, seq;
          std::memcpy(&magic, st.prefix, 4);
          std::memcpy(&len, st.prefix + 4, 4);
          std::memcpy(&payload_len, st.prefix + 8, 4);
          std::memcpy(&seq, st.prefix + 12, 4);
          NS_ABORT_MSG_IF(
              magic != FRAME_MAGIC || len <= FRAME_PREFIX_SIZE ||
                  payload_len > len - FRAME_PREFIX_SIZE || seq != st.next_seq,
              "Node " << GetNode()->GetId() << ": corrupt frame stream from "
                      << InetSocketAddress::ConvertFrom(from).GetIpv4()
                      << " (magic=0x" << std::hex << magic << std::dec
                      << " len=" << len << " payload_len=" << payload_len
                      << " seq=" << seq << " expected seq=" << st.next_seq
                      << ")");
          st.next_seq = seq + 1;
          st.frame_len = len;
          st.payload_len = payload_len;
          st.payload.clear();
          st.payload.reserve(payload_len);
          st.body_remaining = len - FRAME_PREFIX_SIZE;
        }
        continue;
      }

      // Inline payload bytes come right after the prefix.
      if (st.payload.size() < st.payload_len) {
        uint32_t take = std::min<uint32_t>(
            st.payload_len - static_cast<uint32_t>(st.payload.size()),
            size - pos);
        size_t old = st.payload.size();
        st.payload.resize(old + take);
        packet->CreateFragment(pos, take)->CopyData(
            reinterpret_cast<uint8_t *>(&st.payload[old]), take);
        pos += take;
        st.body_remaining -= take;
        continue;
      }

      uint32_t take = std::min(st.body_remaining, size - pos);
      pos += take;
      st.body_remaining -= take;
      if (st.body_remaining > 0) {
        continue;
      }

      uint32_t wire_bytes = st.frame_len;
      std::string payload;
      if (st.payload_len > 0) {
        payload = std::move(st.payload);
      } else {
        auto pit = payloads.find(pos);
        if (pit == payloads.end()) {
          NS_LOG_ERROR("Node " << GetNode()->GetId()
                               << " received a frame without payload");
          st.ResetFrame();
          continue;
        }
        payload = std::move(pit->second);
      }
      st.ResetFrame();

      auto data = nlohmann::json::from_cbor(payload, true, false);
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

        ProcessMessage((enum Messages)msg_data, data, from, wire_bytes);
        if (!m_running) {
          return;
        }
      }
    }
  }
}

std::string GhostDagNode::EncodePayload(const nlohmann::json &payload) {
  std::vector<uint8_t> bytes = nlohmann::json::to_cbor(payload);
  return std::string(bytes.begin(), bytes.end());
}

void GhostDagNode::SendMessage(enum Messages type, nlohmann::json &&payload,
                               Address &to, uint32_t wire_bytes) {
  payload["msg"] = type;
  SendSerialized(EncodePayload(payload), to, wire_bytes);
}

void GhostDagNode::SendSerialized(const std::string &serialized, Address &to,
                                  uint32_t wire_bytes) {
  if (!m_running) {
    return;
  }
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
  auto &queue = m_tx_queue[socket];

  uint32_t payload_size = static_cast<uint32_t>(serialized.size());
  bool inline_payload = FRAME_PREFIX_SIZE + payload_size <= wire_bytes;

  GhostDagFrameHeader hdr;
  hdr.frame_len = wire_bytes;
  hdr.seq = m_tx_frame_seq[socket]++;
  if (inline_payload) {
    hdr.payload = serialized;
    // Zero padding (a zero area, not allocated) with the prefix and payload
    // prepended as a header, the layout every ns-3 application uses.
    Ptr<Packet> frame = Create<Packet>(wire_bytes - hdr.GetSerializedSize());
    frame->AddHeader(hdr);
    queue.push_back(frame);
    HandleSendReady(socket, socket->GetTxAvailable());
    return;
  }

  NS_ABORT_MSG_IF(MpiInterface::GetSize() > 1 &&
                      serialized.size() > MPI_PAYLOAD_LIMIT,
                  "Message payload of " << serialized.size()
                                        << " bytes exceeds the MPI buffer; "
                                           "patch MAX_MPI_MSG_SIZE in ns-3");

  // Payload larger than the modelled frame: body (prefix + padding, all but
  // the last byte, no tags) and a one-byte tail carrying the payload tag.
  Ptr<Packet> body = Create<Packet>(wire_bytes - 1 - FRAME_PREFIX_SIZE);
  body->AddHeader(hdr);
  Ptr<Packet> tail = Create<Packet>(1);
  GhostDagPayloadTag tag;
  tag.payload = serialized;
  tail->AddByteTag(tag);

  queue.push_back(body);
  queue.push_back(tail);
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
  return static_cast<uint32_t>(std::ceil(-static_cast<double>(n_items) *
                                         std::log(fpr) / (8.0 * LN2_SQUARED)));
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
         m_transaction_size * block.tx_count();
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
                                  const nlohmann::json &data, Address &from,
                                  uint32_t wire_bytes) {
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

      TxSet txs;
      if (blockData.contains("transactions")) {
        for (auto &txData : blockData["transactions"]) {
          Transaction tx;
          tx.tx_id = txData.value("tx_id", uint64_t{0});
          tx.size_bytes = txData.value("size_bytes", uint32_t{522});
          tx.fee = txData.value("fee", uint32_t{0});
          txs.insert(tx);
        }
      }
      newBlock.txs =
          BlockBodyStore::Intern(newBlock.header.block_id, std::move(txs));

      EVENT_MSG_RECV(NID, IPV4_STR(from), "block", newBlock.header.block_id,
                     wire_bytes);

      HandleBlock(newBlock, from);
    }
    break;
  }
  case GRAPHENE_BLOCK: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received GRAPHENE_BLOCK");
    uint64_t block_id = data.value("block_id", uint64_t{0});
    EVENT_MSG_RECV(NID, IPV4_STR(from), "graphene_block", block_id, wire_bytes);
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
    std::vector<uint64_t> tx_ids;
    if (data.contains("tx_hashes")) {
      tx_ids = data["tx_hashes"].get<std::vector<uint64_t>>();
    }
    HandleInvTransactions(tx_ids, from);
    break;
  }
  case REQ_TRANSACTIONS: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " received REQ_TRANSACTIONS");
    std::vector<uint64_t> tx_ids;
    if (data.contains("tx_hashes")) {
      tx_ids = data["tx_hashes"].get<std::vector<uint64_t>>();
    }
    HandleReqTransactions(tx_ids, from);
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
                          << " Received unknown message: " << data.dump());
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
    SendMessage(REQ_RELAY_BLOCK, std::move(req), from, WireSizeInv());
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
    if (GrapheneProtocol::BuildSenderComponents(block.transactions(), mc, bf,
                                                iblt, fpr)) {
      uint64_t tx_checksum = 0;
      for (const auto &tx : block.transactions()) {
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
      gm["tx_count"] = block.tx_count();
      gm["bloom_filter"] = GrapheneProtocol::SerializeBloomFilter(bf);
      gm["iblt"] = GrapheneProtocol::SerializeIBLT(iblt);
      gm["tx_checksum"] = tx_checksum;
      gm["fpr"] = bf.effective_fpp();

      SendMessage(GRAPHENE_BLOCK, std::move(gm), from,
                  WireSizeGrapheneBlock(block.header.parent_hashes.size(),
                                        block.tx_count(), fpr, iblt));
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

  auto &txArray = blockMsg["block"]["transactions"] = nlohmann::json::array();
  for (const auto &tx : block.transactions()) {
    txArray.push_back(
        {{"tx_id", tx.tx_id}, {"size_bytes", tx.size_bytes}, {"fee", tx.fee}});
  }

  SendMessage(BLOCK, std::move(blockMsg), from, WireSizeBlock(block));
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
  for (const auto &tx : block.transactions()) {
    uint32_t miner_id = IdFromTxId(tx.tx_id);
    HtabIterator it = m_mempool.find(miner_id, tx.tx_id);
    if (it.isValid()) {
      already_known_txs++;
    }
    m_known_txs.insert(tx.tx_id);
  }

  EVENT_BLOCK_RECEIVED(NID, new_block.header.block_id, IPV4_STR(from),
                       WireSizeBlock(new_block), new_block.tx_count(),
                       already_known_txs, new_block.header.parent_hashes.size(),
                       new_block.header.time_created);

  std::string blockHash = std::to_string(new_block.header.block_id);
  auto timeout_it = m_inv_timeouts.find(blockHash);
  if (timeout_it != m_inv_timeouts.end()) {
    Simulator::Cancel(timeout_it->second);
    m_inv_timeouts.erase(timeout_it);
  }
  m_queue_inv.erase(blockHash);

  std::vector<uint64_t> before_orphans;
  before_orphans.reserve(m_blockchain.orphans.size());
  for (auto &[oid, _] : m_blockchain.orphans)
    before_orphans.push_back(oid);
  std::vector<bool> was_blue = m_blockchain.SnapshotBlueFlags();

  m_blockchain.AddBlock(block);

  {
    uint64_t sibling_count = 0;
    for (uint64_t tip : m_blockchain.tips)
      if (tip != block.header.block_id)
        ++sibling_count;

    double sibling_overlap = 0.0;
    if (block.tx_count() > 0 && sibling_count > 0) {
      std::set<uint64_t> sibling_tx_ids;
      for (uint64_t tip : m_blockchain.tips)
        if (tip != block.header.block_id)
          for (const auto &tx : m_blockchain.blocks[tip].transactions())
            sibling_tx_ids.insert(tx.tx_id);
      uint64_t overlap = 0;
      for (const auto &tx : block.transactions())
        if (sibling_tx_ids.count(tx.tx_id))
          ++overlap;
      sibling_overlap = (double)overlap / block.tx_count();
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

  for (uint64_t oid : before_orphans)
    if (!m_blockchain.IsOrphan(oid))
      EVENT_BLOCK_UNORPHANED(NID, oid);

  if (m_blockchain.HasBlock(block.header.block_id)) {
    uint64_t bid = block.header.block_id;
    EVENT_BLOCK_COLORED(NID, bid, m_blockchain.blocks[bid].is_blue,
                        m_blockchain.blocks[bid].blue_score,
                        m_blockchain.GetDagWidth());
  }

  for (uint64_t id : m_blockchain.NewlyBlue(was_blue)) {
    const Block &blk = m_blockchain.blocks[id];
    EVENT_BLOCK_COLORED(NID, id, blk.is_blue, blk.blue_score,
                        m_blockchain.GetDagWidth());
    for (const auto &tx : blk.transactions()) {
      uint32_t miner_id = IdFromTxId(tx.tx_id);
      HtabIterator it = m_mempool.find(miner_id, tx.tx_id);
      if (it.isValid())
        m_mempool.eraseTransaction(it);
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
  SendMessage(GRAPHENE_RECOVERY_REQUEST, std::move(result.recovery_request),
              from, request_bytes);
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
  for (const auto &tx : blk.transactions()) {
    tx_checksum ^= tx.tx_id;
  }

  resp["block_hash"] = block_hash;
  resp["tx_checksum"] = tx_checksum;
  resp["iblt"] = GrapheneProtocol::SerializeIBLT(sender_second_iblt);

  resp["missing"] = missing;

  SendMessage(GRAPHENE_RECOVERY_RESPONSE, std::move(resp), from,
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
    TxSet recovered_txs;
    for (auto txid : result.block_txids) {
      Transaction tx;
      tx.tx_id = txid;
      tx.size_bytes = 522;

      HtabIterator txit = m_mempool.find(IdFromTxId(txid), txid);
      if (txit.isValid()) {
        tx.fee = txit.iterator->fee;
      }

      recovered_txs.insert(tx);
      m_known_txs.insert(txid);
    }
    recovered.txs = BlockBodyStore::Intern(recovered.header.block_id,
                                           std::move(recovered_txs));

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
  SendMessage(REQ_RELAY_BLOCK, std::move(msg), from, WireSizeInv());
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
    SendMessage(REQ_RELAY_BLOCK, std::move(msg), sit->second, WireSizeInv());
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
    SendMessage(REQ_RELAY_BLOCK, std::move(req), next, WireSizeInv());
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
  inv["msg"] = INV_RELAY_BLOCK;
  std::string serialized = EncodePayload(inv);

  for (const auto &peer_addr : m_peers_addresses) {
    if (peer_addr == exclude)
      continue;
    InetSocketAddress peer = InetSocketAddress(peer_addr, m_ghostdag_port);
    auto addr = Address(peer);
    SendSerialized(serialized, addr, WireSizeInv());
  }
}

/*
 * Transaction Logic
 * */

void GhostDagNode::HandleInvTransactions(const std::vector<uint64_t> &tx_ids,
                                         Address &from) {

  NS_LOG_FUNCTION(this);

  std::vector<uint64_t> wanted;

  for (uint64_t tx_id : tx_ids) {
    if (m_known_txs.contains(tx_id)) {
      continue;
    }

    TxRequest &req = m_queue_inv_tx[tx_id];
    bool first_announcement = req.announcers.empty();
    req.announcers.push_back(from);

    if (first_announcement) {
      req.deadline = Simulator::Now() + m_inv_timeout_minutes;
      m_tx_timeout_queue.emplace_back(req.deadline, tx_id);
      wanted.push_back(tx_id);
    }
  }

  if (wanted.empty()) {
    return;
  }
  ArmTxTimeoutTimer();

  nlohmann::json req;
  req["tx_hashes"] = wanted;
  SendMessage(REQ_TRANSACTIONS, std::move(req), from,
              WireSizeTxInv(wanted.size()));

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " requested " << wanted.size()
                       << " txs in one REQ");
}

void GhostDagNode::HandleReqTransactions(const std::vector<uint64_t> &tx_ids,
                                         Address &from) {

  NS_LOG_FUNCTION(this);

  nlohmann::json msg;
  auto &txArray = msg["transactions"] = nlohmann::json::array();
  uint64_t found_count = 0;

  for (uint64_t tx_id : tx_ids) {
    uint32_t node_id = IdFromTxId(tx_id);

    HtabIterator it = m_mempool.find(node_id, tx_id);
    if (!it.isValid()) {
      continue; // Evicted or never had it
    }

    txArray.push_back(
        {{"tx_id", it.iterator->txId},
         {"size_bytes", static_cast<uint32_t>(m_average_transaction_size)},
         {"fee", it.iterator->fee}});
    found_count++;
  }

  if (found_count == 0) {
    NS_LOG_DEBUG("Node " << GetNode()->GetId() << " had none of the "
                         << tx_ids.size() << " requested txs");
    return;
  }

  SendMessage(TRANSACTIONS, std::move(msg), from, WireSizeTxs(found_count));

  NS_LOG_DEBUG("Node " << GetNode()->GetId() << " replied with " << found_count
                       << "/" << tx_ids.size()
                       << " txs in one TRANSACTION message");
}

void GhostDagNode::HandleTransactions(const std::vector<Transaction> &txs,
                                      Address &from) {
  NS_LOG_FUNCTION(this);

  InetSocketAddress sender = InetSocketAddress::ConvertFrom(from);

  for (const auto &tx : txs) {
    // Any deadline still queued for this tx is stale and will be skipped.
    m_queue_inv_tx.erase(tx.tx_id);

    if (m_known_txs.contains(tx.tx_id)) {
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

    m_pending_inv_tx.push_back(tx.tx_id);

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

  std::vector<uint64_t> batch;
  batch.swap(m_pending_inv_tx);

  BroadcastInvTransactions(batch, exclude);

  NS_LOG_DEBUG("Node " << GetNode()->GetId()
                       << " flushed INV batch: " << batch.size() << " txs");
}

void GhostDagNode::ArmTxTimeoutTimer() {
  if (m_tx_timeout_queue.empty() || m_txTimeoutEvent.IsPending()) {
    return;
  }
  Time delay = m_tx_timeout_queue.front().first - Simulator::Now();
  if (delay.IsNegative()) {
    delay = Time(0);
  }
  m_txTimeoutEvent =
      Simulator::Schedule(delay, &GhostDagNode::ProcessTxTimeouts, this);
}

// Fires at the deadline of the oldest outstanding transaction request and
// handles every request whose deadline has passed: the announcer that was
// asked is dropped and the tx is re-requested from one of the remaining
// announcers, or given up when none is left.
void GhostDagNode::ProcessTxTimeouts() {
  NS_LOG_FUNCTION(this);

  Time now = Simulator::Now();
  while (!m_tx_timeout_queue.empty() &&
         m_tx_timeout_queue.front().first <= now) {
    auto [deadline, tx_id] = m_tx_timeout_queue.front();
    m_tx_timeout_queue.pop_front();

    auto it = m_queue_inv_tx.find(tx_id);
    if (it == m_queue_inv_tx.end() || it->second.deadline != deadline) {
      continue; // received meanwhile, or superseded by a newer request
    }

    auto &announcers = it->second.announcers;
    announcers.erase(announcers.begin());

    if (!announcers.empty()) {
      std::uniform_int_distribution<size_t> pick(0, announcers.size() - 1);
      size_t idx = pick(m_generator);
      if (idx != 0)
        std::swap(announcers[0], announcers[idx]);
      Address next = announcers.front();

      nlohmann::json req;
      req["tx_hashes"] = std::vector<uint64_t>{tx_id};
      SendMessage(REQ_TRANSACTIONS, std::move(req), next, WireSizeTxInv(1));

      it->second.deadline = now + m_inv_timeout_minutes;
      m_tx_timeout_queue.emplace_back(it->second.deadline, tx_id);

      NS_LOG_DEBUG("Node " << GetNode()->GetId() << " retrying tx " << tx_id
                           << " from next peer");
    } else {
      m_queue_inv_tx.erase(it);
      NS_LOG_WARN("Node " << GetNode()->GetId() << " gave up fetching tx "
                          << tx_id);
    }
  }

  ArmTxTimeoutTimer();
}

void GhostDagNode::BroadcastInvTransactions(const std::vector<uint64_t> &tx_ids,
                                            Ipv4Address exclude) {

  if (tx_ids.empty()) {
    return;
  }

  nlohmann::json inv;
  inv["tx_hashes"] = tx_ids;
  inv["msg"] = INV_TRANSACTIONS;
  std::string serialized = EncodePayload(inv);

  for (const auto &peer_addr : m_peers_addresses) {
    if (peer_addr == exclude) {
      continue;
    }
    InetSocketAddress peer(peer_addr, m_ghostdag_port);
    Address addr(peer);
    SendSerialized(serialized, addr, WireSizeTxInv(tx_ids.size()));
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

    m_pending_inv_tx.push_back(txId);

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
  m_accepted_sockets.push_back(s);
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
