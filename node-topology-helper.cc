#include "node-topology-helper.h"

#include "ns3/constant-position-mobility-model.h"
#include "ns3/double.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/log.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/random-variable-stream.h"
#include "ns3/string.h"
#include "ns3/vector.h"

#include <algorithm>
#include <sys/time.h>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("GhostDagTopologyHelper");

// Helper function for timing
static double
GetWallTime()
{
    struct timeval time;
    if (gettimeofday(&time, nullptr))
    {
        return 0;
    }
    return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

GhostDagTopologyHelper::GhostDagTopologyHelper(uint32_t noCpus,
                                               uint32_t totalNoNodes,
                                               uint32_t noMiners,
                                               enum Region* minersRegions,
                                               int minConnectionsPerNode,
                                               int maxConnectionsPerNode,
                                               double latencyParetoShapeDivider,
                                               uint32_t systemId)
    : m_totalNoNodes(totalNoNodes),
      m_noMiners(noMiners),
      m_noCpus(noCpus),
      m_latencyParetoShapeDivider(latencyParetoShapeDivider),
      m_minConnectionsPerNode(minConnectionsPerNode),
      m_maxConnectionsPerNode(maxConnectionsPerNode),
      m_minConnectionsPerMiner(8),
      m_maxConnectionsPerMiner(16),
      m_minerDownloadSpeed(100), // Higher connectivity for miners
      m_minerUploadSpeed(100),
      m_totalNoLinks(0), // 100 Mbps for miners
      m_systemId(systemId)
{
    double tStart = GetWallTime();

    // Initialize region latency matrix (ms)
    double regionLatencies[6][6] = {
        {35.5, 119.49, 254.79, 310.11, 154.36, 207.91},   // North America
        {119.49, 11.61, 221.08, 241.9, 266.45, 350.07},   // Europe
        {254.79, 221.08, 137.09, 346.65, 255.95, 268.91}, // South America
        {310.11, 241.9, 346.65, 99.46, 172.24, 277.8},    // Asia Pacific
        {154.36, 266.45, 255.95, 172.24, 8.76, 162.59},   // Japan
        {207.91, 350.07, 268.91, 277.8, 162.59, 21.72}    // Australia
    };

    for (int k = 0; k < 6; k++)
    {
        for (int j = 0; j < 6; j++)
        {
            m_regionLatencies[k][j] = regionLatencies[k][j];
        }
    }

    // Average regional internet speeds (Mbps)
    m_regionDownloadSpeeds[NORTH_AMERICA] = 41.68;
    m_regionDownloadSpeeds[EUROPE] = 21.29;
    m_regionDownloadSpeeds[SOUTH_AMERICA] = 9.89;
    m_regionDownloadSpeeds[ASIA_PACIFIC] = 14.56;
    m_regionDownloadSpeeds[JAPAN] = 6.9;
    m_regionDownloadSpeeds[AUSTRALIA] = 16;

    m_regionUploadSpeeds[NORTH_AMERICA] = 6.74;
    m_regionUploadSpeeds[EUROPE] = 6.72;
    m_regionUploadSpeeds[SOUTH_AMERICA] = 2.2;
    m_regionUploadSpeeds[ASIA_PACIFIC] = 6.53;
    m_regionUploadSpeeds[JAPAN] = 1.7;
    m_regionUploadSpeeds[AUSTRALIA] = 6.1;

    srand(1000);

    // Validation
    if (m_noMiners > m_totalNoNodes)
    {
        NS_FATAL_ERROR("The number of miners is larger than the total number of nodes\n");
    }

    if (m_noMiners < 1)
    {
        NS_FATAL_ERROR("You need at least one miner\n");
    }

    m_nodesRegion = new uint32_t[m_totalNoNodes];
    m_minersRegions = new enum Region[m_noMiners];

    for (int i = 0; i < m_noMiners; i++)
    {
        m_minersRegions[i] = minersRegions[i];
    }

    // Initialize regional distribution for GHOSTDAG
    // Using a balanced distribution across regions
    std::array<double, 7> nodesDistributionIntervals{NORTH_AMERICA,
                                                     EUROPE,
                                                     SOUTH_AMERICA,
                                                     ASIA_PACIFIC,
                                                     JAPAN,
                                                     AUSTRALIA,
                                                     OTHER};
    std::array<double, 6> nodesDistributionWeights{38.0, 48.0, 2.0, 8.0, 2.0, 2.0};

    m_nodesDistribution =
        std::piecewise_constant_distribution<double>(nodesDistributionIntervals.begin(),
                                                     nodesDistributionIntervals.end(),
                                                     nodesDistributionWeights.begin());

    if (m_systemId == 0)
    {
        std::cout << "GHOSTDAG Network Topology Configuration\n";
    }

    // Create topology
    CreateTopology();

    double tFinish = GetWallTime();
    if (m_systemId == 0)
    {
        std::cout << "Topology created in " << tFinish - tStart << "s.\n";
    }
}

GhostDagTopologyHelper::~GhostDagTopologyHelper()
{
    delete[] m_nodesRegion;
    delete[] m_minersRegions;
}

void
GhostDagTopologyHelper::CreateTopology()
{
    double tStart = GetWallTime();
    std::vector<uint32_t> nodes;

    // Create node ID vector
    nodes.reserve(m_totalNoNodes);
    for (uint32_t i = 0; i < m_totalNoNodes; i++)
    {
        nodes.push_back(i);
    }

    // Select miners randomly
    for (uint32_t i = 0; i < m_noMiners; i++)
    {
        uint32_t index = rand() % nodes.size();
        m_miners.push_back(nodes[index]);
        nodes.erase(nodes.begin() + index);
    }

    std::sort(m_miners.begin(), m_miners.end());

    // Fully interconnect miners (important for GHOSTDAG consensus)
    for (auto& miner : m_miners)
    {
        for (auto& peer : m_miners)
        {
            if (miner != peer)
            {
                m_nodesConnections[miner].push_back(peer);
            }
        }
    }

    // Set connection limits for each node
    for (uint32_t i = 0; i < m_totalNoNodes; i++)
    {
        if (std::find(m_miners.begin(), m_miners.end(), i) != m_miners.end())
        {
            m_minConnections[i] = m_minConnectionsPerMiner;
            m_maxConnections[i] = m_maxConnectionsPerMiner;
        }
        else
        {
            m_minConnections[i] = m_minConnectionsPerNode > 0 ? m_minConnectionsPerNode : 4;
            m_maxConnections[i] = m_maxConnectionsPerNode > 0 ? m_maxConnectionsPerNode : 8;
        }
    }

    // Reset nodes vector for connection creation
    nodes.clear();
    for (uint32_t i = 0; i < m_totalNoNodes; i++)
    {
        nodes.push_back(i);
    }

    // Create connections - miners first
    for (auto& miner : m_miners)
    {
        int count = 0;
        while (m_nodesConnections[miner].size() < m_minConnections[miner] &&
               count < 10 * m_minConnections[miner])
        {
            uint32_t index = rand() % nodes.size();
            uint32_t candidatePeer = nodes[index];

            if (candidatePeer == miner ||
                std::find(m_nodesConnections[miner].begin(),
                          m_nodesConnections[miner].end(),
                          candidatePeer) != m_nodesConnections[miner].end() ||
                m_nodesConnections[candidatePeer].size() >= m_maxConnections[candidatePeer])
            {
                count++;
                continue;
            }

            m_nodesConnections[miner].push_back(candidatePeer);
            m_nodesConnections[candidatePeer].push_back(miner);

            if (m_nodesConnections[candidatePeer].size() == m_maxConnections[candidatePeer])
            {
                nodes.erase(nodes.begin() + index);
            }
            count++;
        }
    }

    // Create connections for regular nodes
    for (uint32_t i = 0; i < m_totalNoNodes; i++)
    {
        int count = 0;
        while (m_nodesConnections[i].size() < m_minConnections[i] &&
               count < 10 * m_minConnections[i])
        {
            if (nodes.empty())
            {
                break;
            }

            uint32_t index = rand() % nodes.size();
            uint32_t candidatePeer = nodes[index];

            if (candidatePeer == i ||
                std::find(m_nodesConnections[i].begin(),
                          m_nodesConnections[i].end(),
                          candidatePeer) != m_nodesConnections[i].end() ||
                m_nodesConnections[candidatePeer].size() >= m_maxConnections[candidatePeer])
            {
                count++;
                continue;
            }

            m_nodesConnections[i].push_back(candidatePeer);
            m_nodesConnections[candidatePeer].push_back(i);

            if (m_nodesConnections[candidatePeer].size() == m_maxConnections[candidatePeer])
            {
                nodes.erase(nodes.begin() + index);
            }
            count++;
        }
    }

    // Print statistics
    if (m_systemId == 0)
    {
        double avgConnections = 0;
        double avgMinerConnections = 0;

        for (auto& node : m_nodesConnections)
        {
            if (std::find(m_miners.begin(), m_miners.end(), node.first) == m_miners.end())
            {
                avgConnections += node.second.size();
            }
            else
            {
                avgMinerConnections += node.second.size();
            }
        }

        std::cout << "Average connections per node: "
                  << avgConnections / (m_totalNoNodes - m_noMiners) << "\n";
        std::cout << "Average connections per miner: " << avgMinerConnections / m_noMiners << "\n";
    }

    // Create NS-3 nodes
    tStart = GetWallTime();
    for (uint32_t i = 0; i < m_totalNoNodes; i++)
    {
        NodeContainer currentNode;
        currentNode.Create(1, i % m_noCpus);
        m_nodes.push_back(currentNode);
        AssignRegion(i);
        AssignInternetSpeeds(i);
    }

    double tFinish = GetWallTime();
    if (m_systemId == 0)
    {
        std::cout << "Nodes created in " << tFinish - tStart << "s.\n";
    }

    // Create P2P links
    tStart = GetWallTime();
    PointToPointHelper pointToPoint;
    std::ostringstream latencyStream;
    std::ostringstream bandwidthStream;

    for (auto& node : m_nodesConnections)
    {
        for (auto& peer : node.second)
        {
            if (peer > node.first)
            { // Avoid duplicate links
                NetDeviceContainer newDevices;
                m_totalNoLinks++;

                // Calculate link bandwidth (minimum of both directions)
                double bandwidth =
                    std::min(std::min(m_nodesInternetSpeeds[node.first].upload_speed,
                                      m_nodesInternetSpeeds[node.first].download_speed),
                             std::min(m_nodesInternetSpeeds[peer].upload_speed,
                                      m_nodesInternetSpeeds[peer].download_speed));

                bandwidthStream.str("");
                bandwidthStream.clear();
                bandwidthStream << bandwidth << "Mbps";

                // Calculate latency
                latencyStream.str("");
                latencyStream.clear();

                uint32_t region1 = m_nodesRegion[node.first];
                uint32_t region2 = m_nodesRegion[peer];

                if (m_latencyParetoShapeDivider > 0)
                {
                    Ptr<ParetoRandomVariable> paretoDistribution =
                        CreateObject<ParetoRandomVariable>();
                    paretoDistribution->SetAttribute(
                        "Shape",
                        DoubleValue(m_regionLatencies[region1][region2] /
                                    m_latencyParetoShapeDivider));
                    latencyStream << paretoDistribution->GetValue() << "ms";
                }
                else
                {
                    latencyStream << m_regionLatencies[region1][region2] << "ms";
                }

                pointToPoint.SetDeviceAttribute("DataRate", StringValue(bandwidthStream.str()));
                pointToPoint.SetChannelAttribute("Delay", StringValue(latencyStream.str()));

                newDevices.Add(
                    pointToPoint.Install(m_nodes.at(node.first).Get(0), m_nodes.at(peer).Get(0)));
                m_devices.push_back(newDevices);
            }
        }
    }

    tFinish = GetWallTime();
    if (m_systemId == 0)
    {
        std::cout << "Total links created: " << m_totalNoLinks << " (" << tFinish - tStart
                  << "s)\n";
    }
}

void
GhostDagTopologyHelper::InstallStack(InternetStackHelper stack)
{
    double tStart = GetWallTime();

    for (uint32_t i = 0; i < m_nodes.size(); ++i)
    {
        NodeContainer currentNode = m_nodes[i];
        for (uint32_t j = 0; j < currentNode.GetN(); ++j)
        {
            stack.Install(currentNode.Get(j));
        }
    }

    double tFinish = GetWallTime();
    if (m_systemId == 0)
    {
        std::cout << "Internet stack installed in " << tFinish - tStart << "s.\n";
    }
}

void
GhostDagTopologyHelper::AssignIpv4Addresses(Ipv4AddressHelper ip)
{
    double tStart = GetWallTime();

    for (uint32_t i = 0; i < m_devices.size(); ++i)
    {
        Ipv4InterfaceContainer newInterfaces;
        NetDeviceContainer currentContainer = m_devices[i];

        newInterfaces.Add(ip.Assign(currentContainer.Get(0)));
        newInterfaces.Add(ip.Assign(currentContainer.Get(1)));

        auto interfaceAddress1 = newInterfaces.GetAddress(0);
        auto interfaceAddress2 = newInterfaces.GetAddress(1);
        uint32_t node1 = (currentContainer.Get(0))->GetNode()->GetId();
        uint32_t node2 = (currentContainer.Get(1))->GetNode()->GetId();

        m_nodesConnectionsIps[node1].push_back(interfaceAddress2);
        m_nodesConnectionsIps[node2].push_back(interfaceAddress1);

        ip.NewNetwork();

        m_interfaces.push_back(newInterfaces);

        m_peersDownloadSpeeds[node1][interfaceAddress2] =
            m_nodesInternetSpeeds[node2].download_speed;
        m_peersDownloadSpeeds[node2][interfaceAddress1] =
            m_nodesInternetSpeeds[node1].download_speed;
        m_peersUploadSpeeds[node1][interfaceAddress2] = m_nodesInternetSpeeds[node2].upload_speed;
        m_peersUploadSpeeds[node2][interfaceAddress1] = m_nodesInternetSpeeds[node1].upload_speed;
    }

    double tFinish = GetWallTime();
    if (m_systemId == 0)
    {
        std::cout << "IP addresses assigned in " << tFinish - tStart << "s.\n";
    }
}

Ptr<Node>
GhostDagTopologyHelper::GetNode(uint32_t id)
{
    if (id > m_nodes.size() - 1)
    {
        NS_FATAL_ERROR("Index out of bounds in GhostDagTopologyHelper::GetNode.");
    }
    return (m_nodes.at(id)).Get(0);
}

Ipv4InterfaceContainer
GhostDagTopologyHelper::GetIpv4InterfaceContainer() const
{
    Ipv4InterfaceContainer ipv4InterfaceContainer;
    for (auto container = m_interfaces.begin(); container != m_interfaces.end(); container++)
    {
        ipv4InterfaceContainer.Add(*container);
    }
    return ipv4InterfaceContainer;
}

std::map<uint32_t, std::vector<Ipv4Address>>
GhostDagTopologyHelper::GetNodesConnectionsIps() const
{
    return m_nodesConnectionsIps;
}

std::vector<uint32_t>
GhostDagTopologyHelper::GetMiners() const
{
    return m_miners;
}

void
GhostDagTopologyHelper::AssignRegion(uint32_t id)
{
    auto index = std::find(m_miners.begin(), m_miners.end(), id);
    if (index != m_miners.end())
    {
        m_nodesRegion[id] = m_minersRegions[index - m_miners.begin()];
    }
    else
    {
        int number = m_nodesDistribution(m_generator);
        m_nodesRegion[id] = number;
    }
}

void
GhostDagTopologyHelper::AssignInternetSpeeds(uint32_t id)
{
    auto index = std::find(m_miners.begin(), m_miners.end(), id);
    if (index != m_miners.end())
    {
        m_nodesInternetSpeeds[id].download_speed = m_minerDownloadSpeed;
        m_nodesInternetSpeeds[id].upload_speed = m_minerUploadSpeed;
    }
    else
    {
        // Use region average speeds with some variation
        auto region = static_cast<Region>(m_nodesRegion[id]);
        m_nodesInternetSpeeds[id].download_speed =
            m_regionDownloadSpeeds[region] * (0.7 + (rand() % 60) / 100.0);
        m_nodesInternetSpeeds[id].upload_speed =
            m_regionUploadSpeeds[region] * (0.7 + (rand() % 60) / 100.0);
    }
}

uint32_t*
GhostDagTopologyHelper::GetNodesRegions()
{
    return m_nodesRegion;
}

std::map<uint32_t, std::map<Ipv4Address, double>>
GhostDagTopologyHelper::GetPeersDownloadSpeeds() const
{
    return m_peersDownloadSpeeds;
}

std::map<uint32_t, std::map<Ipv4Address, double>>
GhostDagTopologyHelper::GetPeersUploadSpeeds() const
{
    return m_peersUploadSpeeds;
}

std::map<uint32_t, NodeInternetSpeeds>
GhostDagTopologyHelper::GetNodesInternetSpeeds() const
{
    return m_nodesInternetSpeeds;
}

void
GhostDagTopologyHelper::BoundingBox(double ulx, double uly, double lrx, double lry)
{
    // Implementation for positioning nodes in animation
    // Can be used with NetAnim for visualization
}

} // namespace ns3
