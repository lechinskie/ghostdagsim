#include "node.h"

#include "dag.h"
#include "json.h"

#include "ns3/address-utils.h"
#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/udp-socket.h"
#include "ns3/uinteger.h"

#include <cstdint>
#include <fmt/format.h>
#include <string>

#define MSG_DELIMITER '#'

namespace ns3 {

NS_LOG_COMPONENT_DEFINE("GhostDagNode");

NS_OBJECT_ENSURE_REGISTERED(GhostDagNode);

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
                        MakeUintegerChecker<uint8_t>())
          .AddAttribute("Local", "The Address on which to Bind the rx socket.",
                        AddressValue(),
                        MakeAddressAccessor(&GhostDagNode::m_local),
                        MakeAddressChecker())
          .AddAttribute("IsMiner", "Whether the node should mine or not.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&GhostDagNode::m_is_miner),
                        MakeBooleanChecker())
          .AddAttribute("MineNotSynced",
                        "Whether the node should mine while still syncing with "
                        "DAG or not.",
                        BooleanValue(false),
                        MakeBooleanAccessor(&GhostDagNode::m_mine_not_synced),
                        MakeBooleanChecker())
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
              "FixedBlockInterval",
              "Fixed interval to generate blocks (seconds).", DoubleValue(1.0),
              MakeDoubleAccessor(&GhostDagNode::m_fixed_block_interval),
              MakeDoubleChecker<double>())
          .AddTraceSource("Rx", "A packet has been received",
                          MakeTraceSourceAccessor(&GhostDagNode::m_rx_trace),
                          "ns3::Packet::AddressTracedCallback");
  return tid;
}

GhostDagNode::GhostDagNode()
    : m_is_miner(false), m_mine_not_synced(false),
      m_average_transaction_size(522.4), m_transaction_index_size(2) {
  NS_LOG_FUNCTION(this);
  m_socket = nullptr;
  m_mean_block_receive_time = 0;
  m_previous_block_receive_time = 0;
  m_mean_block_propagation_time = 0;
  m_mean_block_size = 0;

  m_ghostdag_port = 16443;
  m_ghostdag_k = 10;
  m_seconds_per_min = 60;
  m_count_bytes = 4;
  m_message_header_size = 90;
  m_inventory_size = 36;
  m_get_headers_size = 72;
  m_headers_size = 81;
  m_block_locator_size = 81;

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

void GhostDagNode::SetNodeStats(NodeStats *node_stats) {
  NS_LOG_FUNCTION(this);
  m_node_stats = node_stats;
}

// ============================================================================
// Application Lifecycle
// ============================================================================

void GhostDagNode::DoDispose() {
  NS_LOG_FUNCTION(this);
  m_socket = nullptr;
  Application::DoDispose();
}

void GhostDagNode::StartApplication() {
  NS_LOG_FUNCTION(this);

  srand(time(nullptr) + GetNode()->GetId());

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
    InetSocketAddress local =
        InetSocketAddress(Ipv4Address::GetAny(), m_ghostdag_port);
    m_socket->Bind(local);
    m_socket->Listen();

    if (addressUtils::IsMulticast(m_local)) {
      Ptr<UdpSocket> udpSocket = DynamicCast<UdpSocket>(m_socket);
      if (udpSocket) {
        udpSocket->MulticastJoinGroup(0, local);
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
    m_peers_sockets[peer_addr] =
        Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_peers_sockets[peer_addr]->Connect(
        InetSocketAddress(peer_addr, m_ghostdag_port));
  }

  if (m_node_stats) {
    m_node_stats->node_id = GetNode()->GetId();
    m_node_stats->mean_block_receive_time = 0;
    m_node_stats->mean_block_propagation_time = 0;
    m_node_stats->total_blocks = 0;
    m_node_stats->connections = m_peers_addresses.size();
  }
  m_ping_event =
      Simulator::Schedule(Seconds(10.0), &GhostDagNode::PingPeers, this);
}

void GhostDagNode::StopApplication() {
  if (m_ping_event.IsPending()) {
    Simulator::Cancel(m_ping_event);
  }
  NS_LOG_FUNCTION(this);
  for (auto &socket_pair : m_peers_sockets) {
    socket_pair.second->Close();
  }

  if (m_socket) {
    m_socket->Close();
    m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
  }

  NS_LOG_WARN("\n\nGHOSTDAG NODE " << GetNode()->GetId() << ":");
  NS_LOG_WARN("Total Blocks in DAG = " << m_blockchain.blocks.size());
  NS_LOG_WARN("Mean Block Receive Time = " << m_mean_block_receive_time << "s");
  NS_LOG_WARN("Mean Block Propagation Time = " << m_mean_block_propagation_time
                                               << "s");
  NS_LOG_WARN("Mean Block Size = " << m_mean_block_size << " Bytes");

  // Update final stats
  if (m_node_stats) {
    m_node_stats->mean_block_receive_time = m_mean_block_receive_time;
    m_node_stats->mean_block_propagation_time = m_mean_block_propagation_time;
    m_node_stats->total_blocks = m_blockchain.blocks.size();
  }
}

void GhostDagNode::HandleRead(Ptr<Socket> socket) {
  Address from;
  Ptr<Packet> packet;

  while ((packet = socket->RecvFrom(from))) {
    if (packet->GetSize() == 0) {
      break;
    }

    std::vector<uint8_t> buf(packet->GetSize());
    packet->CopyData(buf.data(), packet->GetSize());
    std::string new_data(buf.begin(), buf.end());

    m_buffered_data[from] += new_data;

    size_t pos = 0;
    while ((pos = m_buffered_data[from].find(MSG_DELIMITER)) !=
           std::string::npos) {
      std::string parsed_packet = m_buffered_data[from].substr(0, pos);

      m_buffered_data[from].erase(0, pos + 1);

      auto data = nlohmann::json::parse(parsed_packet, nullptr, false);
      if (data.is_discarded()) {
        m_buffered_data[from].erase(0, pos + 1);
        continue;
      }

      int msg_data = data.value("msg", -1);
      if (msg_data != -1) {
        NS_LOG_INFO("At time " << Simulator::Now().GetSeconds()
                               << "s ghostdag node " << GetNode()->GetId()
                               << " received " << packet->GetSize()
                               << " bytes from "
                               << InetSocketAddress::ConvertFrom(from).GetIpv4()
                               << " port "
                               << InetSocketAddress::ConvertFrom(from).GetPort()
                               << " with info = " << data.dump(4));
        ProcessMessage((enum Messages)msg_data, parsed_packet, from);
      } else {
        m_buffered_data[from].erase(0, pos + 1);
      }
    }
  }
}

void GhostDagNode::SendMessage(enum Messages recv, enum Messages type,
                               std::string payload, Address &to) {
  nlohmann::json d = nlohmann::json::parse(payload);
  d["msg"] = type;
  payload = d.dump();
  Ptr<Packet> packet =
      Create<Packet>((uint8_t *)payload.c_str(), payload.size());
  const uint8_t buf[] = {MSG_DELIMITER};

  InetSocketAddress peer = InetSocketAddress::ConvertFrom(to);
  Ipv4Address ip = peer.GetIpv4();
  auto it = m_peers_sockets.find(ip);

  if (it == m_peers_sockets.end()) {
    m_peers_sockets[ip] =
        Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    m_peers_sockets[ip]->Connect(InetSocketAddress(ip, m_ghostdag_port));
    return;
  }

  if (m_peers_sockets[ip]->Send(packet) > 0) {
    m_peers_sockets[ip]->Send(buf, 1, 0);
  }
}

void GhostDagNode::ProcessMessage(enum Messages msg_type, std::string payload,
                                  Address &from) {
  switch (msg_type) {
  case PING: {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " <- PING → PONG");
    nlohmann::json d;
    d["whoami"] = std::to_string(GetNode()->GetId());

    SendMessage(PING, PONG, d.dump(), from);
    break;
  }
  case PONG:
    NS_LOG_INFO("Node " << GetNode()->GetId() << " <- PONG from " << from);
    break;
  default:
    NS_LOG_ERROR("Node: " << GetNode()->GetId()
                          << " Received not know message: " << payload);
    break;
  }
}

void GhostDagNode::PingPeers() {
  if (m_peers_sockets.empty()) {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " has no peers to ping");
  } else {
    NS_LOG_INFO("Node " << GetNode()->GetId() << " pinging peers");
  }
  for (const auto &ipv4 : m_peers_addresses) {
    auto sk = InetSocketAddress(ipv4, m_ghostdag_port);
    auto addr = Address(sk);
    nlohmann::json d;
    d["whoami"] = std::to_string(GetNode()->GetId());

    SendMessage(NO_MESSAGE, PING, d.dump(), addr);
  }

  m_ping_event =
      Simulator::Schedule(Minutes(1.0), &GhostDagNode::PingPeers, this);
}

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

} // namespace ns3
