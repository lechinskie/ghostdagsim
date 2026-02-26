/*
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
 *
 * Author: Josh Pelkey <jpelkey@gatech.edu>
 * Re-impl for ghostdag: Eduardo Lechinski Ramos <eduardo_ramos@edu.univali.br>
 */

#pragma once

#include "../dag.h"
#include "ipv4-address-helper-custom.h"

#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/net-device-container.h"
#include "ns3/point-to-point-helper.h"

#include <map>
#include <random>
#include <vector>

namespace ns3 {

/**
 * \brief Helper class to create GHOSTDAG network topologies
 *
 * This class provides functionality to:
 * - Create distributed nodes across geographical regions
 * - Establish peer-to-peer connections with realistic latencies
 * - Assign bandwidth based on regional distributions
 * - Configure miners with higher connectivity
 */
class GhostDagTopologyHelper {
public:
  /**
   * \brief Construct a GHOSTDAG topology helper
   *
   * \param noCpus Number of CPUs for parallel simulation
   * \param totalNoNodes Total number of nodes in the network
   * \param noMiners Number of mining nodes
   * \param minersRegions Array specifying the region for each miner
   * \param minConnectionsPerNode Minimum connections for regular nodes
   * \param maxConnectionsPerNode Maximum connections for regular nodes
   * \param latencyParetoShapeDivider Pareto distribution parameter for latency
   * variation
   * \param systemId System identifier for distributed simulations
   */
  GhostDagTopologyHelper(uint32_t noCpus, uint32_t totalNoNodes,
                         uint32_t noMiners, enum Region *minersRegions,
                         int minConnectionsPerNode, int maxConnectionsPerNode,
                         double latencyParetoShapeDivider, uint32_t systemId);

  ~GhostDagTopologyHelper();

  /**
   * \brief Get a node by its ID
   * \param id Node identifier
   * \return Pointer to the requested node
   */
  Ptr<Node> GetNode(uint32_t id);

  /**
   * \brief Install the Internet stack on all nodes
   * \param stack Internet stack helper to use
   */
  void InstallStack(InternetStackHelper stack);

  /**
   * \brief Assign IPv4 addresses to all network interfaces
   * \param ip IPv4 address helper for assignment
   */
  void AssignIpv4Addresses(Ipv4AddressHelperCustom ip);

  /**
   * \brief Get all IPv4 interface containers
   * \return Container with all interfaces
   */
  Ipv4InterfaceContainer GetIpv4InterfaceContainer() const;

  /**
   * \brief Get the IP addresses of peers for each node
   * \return Map of node ID to vector of peer IP addresses
   */
  std::map<uint32_t, std::vector<Ipv4Address>> GetNodesConnectionsIps() const;

  /**
   * \brief Get the list of miner node IDs
   * \return Vector of miner IDs
   */
  std::vector<uint32_t> GetMiners() const;

  /**
   * \brief Get the geographical regions of all nodes
   * \return Array of region assignments
   */
  uint32_t *GetNodesRegions();

  /**
   * \brief Get download speeds between peers
   * \return Nested map: nodeId -> (peerIp -> downloadSpeed)
   */
  std::map<uint32_t, std::map<Ipv4Address, double>>
  GetPeersDownloadSpeeds() const;

  /**
   * \brief Get upload speeds between peers
   * \return Nested map: nodeId -> (peerIp -> uploadSpeed)
   */
  std::map<uint32_t, std::map<Ipv4Address, double>>
  GetPeersUploadSpeeds() const;

  /**
   * \brief Get internet speeds for all nodes
   * \return Map of node ID to internet speeds
   */
  std::map<uint32_t, NodeInternetSpeeds> GetNodesInternetSpeeds() const;

private:
  /**
   * \brief Assign a geographical region to a node
   * \param id Node identifier
   */
  void AssignRegion(uint32_t id);

  /**
   * \brief Assign download/upload speeds to a node
   * \param id Node identifier
   */
  void AssignInternetSpeeds(uint32_t id);

  /**
   * \brief Create topology connections between nodes
   */
  void CreateTopology();

  // Network parameters
  uint32_t m_totalNoNodes;            //!< Total number of nodes
  uint32_t m_noMiners;                //!< Number of mining nodes
  uint32_t m_noCpus;                  //!< Number of CPUs for simulation
  double m_latencyParetoShapeDivider; //!< Pareto shape parameter for latency
  int m_minConnectionsPerNode;        //!< Min connections per regular node
  int m_maxConnectionsPerNode;        //!< Max connections per regular node
  int m_minConnectionsPerMiner;       //!< Min connections per miner
  int m_maxConnectionsPerMiner;       //!< Max connections per miner
  double m_minerDownloadSpeed;        //!< Download speed for miners (Mbps)
  double m_minerUploadSpeed;          //!< Upload speed for miners (Mbps)
  uint32_t m_totalNoLinks;            //!< Total number of P2P links
  uint32_t m_systemId;                //!< System ID for distributed simulation

  // Regional configuration
  enum Region *m_minersRegions;   //!< Region assignment for miners
  std::vector<uint32_t> m_miners; //!< List of miner node IDs
  uint32_t *m_nodesRegion;        //!< Region for each node
  double m_regionLatencies[6][6]; //!< Inter-region latency matrix (ms)
  double
      m_regionDownloadSpeeds[6];  //!< Average download speed per region (Mbps)
  double m_regionUploadSpeeds[6]; //!< Average upload speed per region (Mbps)

  // Network topology structures
  std::map<uint32_t, std::vector<uint32_t>>
      m_nodesConnections; //!< Node connectivity graph
  std::map<uint32_t, std::vector<Ipv4Address>>
      m_nodesConnectionsIps;                        //!< IP addresses of peers
  std::vector<NodeContainer> m_nodes;               //!< All nodes in network
  std::vector<NetDeviceContainer> m_devices;        //!< All network devices
  std::vector<Ipv4InterfaceContainer> m_interfaces; //!< All IPv4 interfaces

  // Bandwidth configuration
  std::map<uint32_t, std::map<Ipv4Address, double>>
      m_peersDownloadSpeeds; //!< Download speeds
  std::map<uint32_t, std::map<Ipv4Address, double>>
      m_peersUploadSpeeds; //!< Upload speeds
  std::map<uint32_t, NodeInternetSpeeds>
      m_nodesInternetSpeeds;                //!< Node internet speeds
  std::map<uint32_t, int> m_minConnections; //!< Min connections per node
  std::map<uint32_t, int> m_maxConnections; //!< Max connections per node

  // Random distributions
  std::default_random_engine m_generator;
  std::piecewise_constant_distribution<double> m_nodesDistribution;
  std::piecewise_constant_distribution<double> m_connectionsDistribution;
  std::piecewise_constant_distribution<double>
      m_europeDownloadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_europeUploadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_northAmericaDownloadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_northAmericaUploadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_asiaPacificDownloadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_asiaPacificUploadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_japanDownloadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_japanUploadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_southAmericaDownloadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_southAmericaUploadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_australiaDownloadBandwidthDistribution;
  std::piecewise_constant_distribution<double>
      m_australiaUploadBandwidthDistribution;
};

} // namespace ns3
