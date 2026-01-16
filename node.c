#pragma once

#include "node.h"

#include "ns3/address.h"
#include "ns3/boolean.h"
#include "ns3/double.h"
#include "ns3/nstime.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/uinteger.h"

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
            .AddAttribute("K ghostdag",
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
        m_socket->ShutdownSend();
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

    DiscoverPeers();
}

void
GhostDagNode::StopApplication()
{
    NS_LOG_FUNCTION(this);

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

} // namespace ns3
