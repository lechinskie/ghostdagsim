#include "node.h"

#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/uinteger.h"

#include <algorithm>
#include <cstdint>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("GhostDagNode");

NS_OBJECT_ENSURE_REGISTERED(GhostDagNode);

TypeId
GhostDagNode::GetTypeId()
{
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
            .AddAttribute("Local",
                          "The Address on which to Bind the rx socket.",
                          AddressValue(),
                          MakeAddressAccessor(&GhostDagNode::m_local),
                          MakeAddressChecker())
            .AddAttribute("IsMiner",
                          "Whether the node should mine or not.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&GhostDagNode::m_is_miner),
                          MakeBooleanChecker())
            .AddAttribute("MineNotSynced",
                          "Whether the node should mine while still syncing with DAG or not.",
                          BooleanValue(false),
                          MakeBooleanAccessor(&GhostDagNode::m_mine_not_synced),
                          MakeBooleanChecker())
            .AddAttribute("InvTimeoutMinutes",
                          "The timeout of inv messages in minutes",
                          TimeValue(Minutes(20)),
                          MakeTimeAccessor(&GhostDagNode::m_inv_timeout_minutes),
                          MakeTimeChecker())
            .AddAttribute("MaxPeers",
                          "The max numbers of peers a node should have discovering",
                          UintegerValue(32),
                          MakeUintegerAccessor(&GhostDagNode::m_max_peers),
                          MakeUintegerChecker<uint8_t>())
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
            .AddTraceSource("Rx",
                            "A packet has been received",
                            MakeTraceSourceAccessor(&GhostDagNode::m_rx_trace),
                            "ns3::Packet::AddressTracedCallback");
    return tid;
}

GhostDagNode::GhostDagNode()
    : m_is_miner(false),
      m_mine_not_synced(false),
      m_average_transaction_size(522.4),
      m_transaction_index_size(2)
{
    NS_LOG_FUNCTION(this);
    m_socket = nullptr;
    m_mean_block_receive_time = 0;
    m_previous_block_receive_time = 0;
    m_mean_block_propagation_time = 0;
    m_mean_block_size = 0;
    m_max_peers = 32;

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

GhostDagNode::~GhostDagNode()
{
    NS_LOG_FUNCTION(this);
}

Ptr<Socket>
GhostDagNode::GetListeningSocket() const
{
    NS_LOG_FUNCTION(this);
    return m_socket;
}

std::vector<Ipv4Address>
GhostDagNode::GetPeersAddresses() const
{
    NS_LOG_FUNCTION(this);
    return m_peers_addresses;
}

void
GhostDagNode::SetPeersAddresses(const std::vector<Ipv4Address>& peers)
{
    NS_LOG_FUNCTION(this);
    m_peers_addresses = peers;
}

void
GhostDagNode::SetPeersDownloadSpeeds(const std::map<Ipv4Address, double>& peers_download_speeds)
{
    NS_LOG_FUNCTION(this);
    m_peers_download_speeds = peers_download_speeds;
}

void
GhostDagNode::SetPeersUploadSpeeds(const std::map<Ipv4Address, double>& peers_upload_speeds)
{
    NS_LOG_FUNCTION(this);
    m_peers_upload_speeds = peers_upload_speeds;
}

void
GhostDagNode::SetNodeInternetSpeeds(const NodeInternetSpeeds& internet_speeds)
{
    NS_LOG_FUNCTION(this);
    m_download_speed = internet_speeds.download_speed * 1000000 / 8;
    m_upload_speed = internet_speeds.upload_speed * 1000000 / 8;
}

void
GhostDagNode::SetNodeStats(NodeStats* node_stats)
{
    NS_LOG_FUNCTION(this);
    m_node_stats = node_stats;
}

// ============================================================================
// Application Lifecycle
// ============================================================================

void
GhostDagNode::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_socket = nullptr;
    Application::DoDispose();
}

void
GhostDagNode::StartApplication()
{
    NS_LOG_FUNCTION(this);

    srand(time(nullptr) + GetNode()->GetId());

    NS_LOG_INFO("Node " << GetNode()->GetId() << ": download speed = " << m_download_speed
                        << " B/s");
    NS_LOG_INFO("Node " << GetNode()->GetId() << ": upload speed = " << m_upload_speed << " B/s");
    NS_LOG_INFO("Node " << GetNode()->GetId()
                        << ": GHOSTDAG K = " << static_cast<int>(m_ghostdag_k));
    NS_LOG_INFO("Node " << GetNode()->GetId() << ": peers count = " << m_peers_addresses.size());

    if (!m_socket)
    {
        m_socket = Socket::CreateSocket(GetNode(), m_tid);
        m_socket->Bind(m_local);
        m_socket->Listen();
    }

    m_socket->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
    m_socket->SetAcceptCallback(MakeNullCallback<bool, Ptr<Socket>, const Address&>(),
                                MakeCallback(&GhostDagNode::HandleAccept, this));
    m_socket->SetCloseCallbacks(MakeCallback(&GhostDagNode::HandlePeerClose, this),
                                MakeCallback(&GhostDagNode::HandlePeerError, this));

    NS_LOG_DEBUG("Node " << GetNode()->GetId() << ": Creating peer sockets");
    for (const auto& peer_addr : m_peers_addresses)
    {
        m_peers_sockets[peer_addr] = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        m_peers_sockets[peer_addr]->Connect(InetSocketAddress(peer_addr, m_ghostdag_port));
    }

    if (m_node_stats)
    {
        m_node_stats->node_id = GetNode()->GetId();
        m_node_stats->mean_block_receive_time = 0;
        m_node_stats->mean_block_propagation_time = 0;
        m_node_stats->total_blocks = 0;
        m_node_stats->connections = m_peers_addresses.size();
    }

    m_discoveryEvent = Simulator::Schedule(Seconds(3.0), &GhostDagNode::DiscoverPeers, this);

    m_pingEvent = Simulator::Schedule(Seconds(1.0), &GhostDagNode::PingPeers, this);
}

void
GhostDagNode::PingPeers()
{
    if (m_peers_sockets.empty())
    {
        NS_LOG_INFO("Node " << GetNode()->GetId() << " has no peers to ping");
    }
    else
    {
        NS_LOG_INFO("Node " << GetNode()->GetId() << " pinging peers");
    }
    for (auto& kv : m_peers_sockets)
    {
        Address addr;
        kv.second->GetPeerName(addr);
        SendMessage(PING, "", addr);
    }

    // Repeat every 5 seconds
    m_pingEvent = Simulator::Schedule(Seconds(1.0), &GhostDagNode::PingPeers, this);
}

void
GhostDagNode::StopApplication()
{
    NS_LOG_FUNCTION(this);
    if (m_discoveryEvent.IsPending())
    {
        Simulator::Cancel(m_discoveryEvent);
    }

    for (auto& socket_pair : m_peers_sockets)
    {
        socket_pair.second->Close();
    }

    if (m_socket)
    {
        m_socket->Close();
        m_socket->SetRecvCallback(MakeNullCallback<void, Ptr<Socket>>());
    }

    NS_LOG_WARN("\n\nGHOSTDAG NODE " << GetNode()->GetId() << ":");
    NS_LOG_WARN("Total Blocks in DAG = " << m_blockchain.blocks.size());
    NS_LOG_WARN("Mean Block Receive Time = " << m_mean_block_receive_time << "s");
    NS_LOG_WARN("Mean Block Propagation Time = " << m_mean_block_propagation_time << "s");
    NS_LOG_WARN("Mean Block Size = " << m_mean_block_size << " Bytes");

    // Update final stats
    if (m_node_stats)
    {
        m_node_stats->mean_block_receive_time = m_mean_block_receive_time;
        m_node_stats->mean_block_propagation_time = m_mean_block_propagation_time;
        m_node_stats->total_blocks = m_blockchain.blocks.size();
    }
}

void
GhostDagNode::SendMessage(enum Messages type, std::string payload, Address& to)
{
    std::ostringstream oss;
    oss << static_cast<uint8_t>(type) << payload;
    std::string data = oss.str();

    Ptr<Packet> packet = Create<Packet>((uint8_t*)data.c_str(), data.size());

    InetSocketAddress peer = InetSocketAddress::ConvertFrom(to);
    Ipv4Address ip = peer.GetIpv4();
    auto it = m_peers_sockets.find(ip);

    if (it == m_peers_sockets.end())
    {
        m_peers_sockets[ip] = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
        m_peers_sockets[ip]->Connect(InetSocketAddress(ip, m_ghostdag_port));
    }

    m_peers_sockets[ip]->Send(packet);
}

void
GhostDagNode::HandleRead(Ptr<Socket> socket)
{
    Address from;
    Ptr<Packet> packet;

    while ((packet = socket->RecvFrom(from)))
    {
        NS_LOG_INFO("RECEIVED PACKET FROM " << from);
        m_rx_trace(packet, from);

        uint32_t size = packet->GetSize();
        std::vector<uint8_t> buffer(size);
        packet->CopyData(buffer.data(), size);

        auto msg_type = static_cast<Messages>(buffer[0]);
        std::string payload(buffer.begin() + 1, buffer.end());

        NS_LOG_INFO(payload << " " << msg_type);
        ProcessMessage(msg_type, payload, from);
    }
}

void
GhostDagNode::ProcessMessage(enum Messages msg_type, std::string payload, Address& from)
{
    switch (msg_type)
    {
    case PING:
        NS_LOG_INFO("Node " << GetNode()->GetId() << " <- PING → PONG");
        SendMessage(PONG, "", from);
        break;

    case PONG:
        NS_LOG_INFO("Node " << GetNode()->GetId() << " <- PONG");
        break;

    case REQ_ADDRESSES: {
        std::ostringstream oss;
        int sent = 0;

        for (auto& ip : m_peers_addresses)
        {
            if (sent >= m_max_peers)
            {
                break;
            }
            oss << ip << ",";
            sent++;
        }

        NS_LOG_INFO("Node " << GetNode()->GetId() << " sending " << sent << " addresses");

        SendMessage(ADDRESSES, oss.str(), from);
        break;
    }

    case ADDRESSES: {
        std::stringstream ss(payload);
        std::string ipStr;
        NS_LOG_INFO("received address " << m_local << " from " << from);

        while (std::getline(ss, ipStr, ','))
        {
            if (ipStr.empty())
            {
                continue;
            }

            if ((int)m_peers_addresses.size() >= m_max_peers)
            {
                break;
            }

            Ipv4Address ip(ipStr.c_str());

            if (m_peers_sockets.count(ip))
            {
                continue;
            }

            if (std::find(m_peers_addresses.begin(), m_peers_addresses.end(), ip) !=
                m_peers_addresses.end())
            {
                continue;
            }

            NS_LOG_INFO("Node " << GetNode()->GetId() << " discovered new peer " << ip);

            m_peers_addresses.push_back(ip);
            ConnectToPeer(ip, m_ghostdag_port);
        }
        break;
    }

    default:
        break;
    }
}

void
GhostDagNode::DiscoverPeers()
{
    if ((int)m_peers_addresses.size() >= m_max_peers)
    {
        NS_LOG_INFO("Node " << GetNode()->GetId() << " has max peers, skipping discovery");
        m_discoveryEvent = Simulator::Schedule(Seconds(32), &GhostDagNode::DiscoverPeers, this);
        return;
    }

    NS_LOG_INFO("Node " << GetNode()->GetId() << " running peer discovery");

    for (auto& ip : m_peers_addresses)
    {
        auto addr = InetSocketAddress(ip, m_ghostdag_port).ConvertTo();

        SendMessage(REQ_ADDRESSES, "", addr);
        NS_LOG_INFO("Node " << GetNode()->GetId() << " sent req address" << " to " << addr);
    }

    m_discoveryEvent = Simulator::Schedule(Seconds(5), &GhostDagNode::DiscoverPeers, this);
}

void
GhostDagNode::ConnectToPeer(Ipv4Address peerIp, uint16_t port)
{
    NS_LOG_INFO("CONNECTION TO PEER: " << peerIp);
    if ((int)m_peers_addresses.size() >= m_max_peers)
    {
        return;
    }

    if (m_peers_sockets.count(peerIp))
    {
        return;
    }

    Ptr<Socket> socket = Socket::CreateSocket(GetNode(), TcpSocketFactory::GetTypeId());
    socket->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
    socket->SetCloseCallbacks(MakeCallback(&GhostDagNode::HandlePeerClose, this),
                              MakeCallback(&GhostDagNode::HandlePeerError, this));

    InetSocketAddress remote(peerIp, m_ghostdag_port);
    socket->Connect(remote);

    m_peers_sockets[peerIp] = socket;

    if (std::find(m_peers_addresses.begin(), m_peers_addresses.end(), peerIp) ==
        m_peers_addresses.end())
    {
        m_peers_addresses.push_back(peerIp);
    }
}

void
GhostDagNode::HandlePeerClose(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    for (auto it = m_peers_sockets.begin(); it != m_peers_sockets.end(); ++it)
    {
        if (it->second == socket)
        {
            Ipv4Address ip = it->first;
            NS_LOG_INFO("Node " << GetNode()->GetId() << " peer closed: " << ip);

            m_peers_sockets.erase(it);
            m_peers_download_speeds.erase(ip);
            m_peers_upload_speeds.erase(ip);
            break;
        }
    }
}

void
GhostDagNode::HandlePeerError(Ptr<Socket> socket)
{
    NS_LOG_FUNCTION(this << socket);

    for (auto it = m_peers_sockets.begin(); it != m_peers_sockets.end(); ++it)
    {
        if (it->second == socket)
        {
            Ipv4Address ip = it->first;
            NS_LOG_WARN("Node " << GetNode()->GetId() << " peer error: " << ip);

            it->second->Close();
            m_peers_sockets.erase(it);
            m_peers_download_speeds.erase(ip);
            m_peers_upload_speeds.erase(ip);
            break;
        }
    }
}

void
GhostDagNode::HandleAccept(Ptr<Socket> s, const Address& from)
{
    InetSocketAddress peer = InetSocketAddress::ConvertFrom(from);
    Ipv4Address ip = peer.GetIpv4();

    if ((int)m_peers_addresses.size() >= m_max_peers)
    {
        s->Close();
        return;
    }

    NS_LOG_INFO("Node " << GetNode()->GetId() << " accepted peer " << ip);

    s->SetRecvCallback(MakeCallback(&GhostDagNode::HandleRead, this));
    s->SetCloseCallbacks(MakeCallback(&GhostDagNode::HandlePeerClose, this),
                         MakeCallback(&GhostDagNode::HandlePeerError, this));

    m_peers_sockets[ip] = s;

    if (std::find(m_peers_addresses.begin(), m_peers_addresses.end(), ip) ==
        m_peers_addresses.end())
    {
        m_peers_addresses.push_back(ip);
    }
}

} // namespace ns3
