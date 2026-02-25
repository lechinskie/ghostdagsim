#include "dag.h"
#include "helpers/node-helper.h"
#include "helpers/node-topology-helper.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/mpi-interface.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <mpi.h>
#include <sys/time.h>

#ifndef NS3_MPI
#error                                                                         \
    "Distributed simulations need to run with NS3_MPI module, reconfigure and build your ns3 waf again pls"
#endif

using namespace ns3;

double GetWallTime();
NS_LOG_COMPONENT_DEFINE("GhostDagSimulator");

int main(int argc, char *argv[]) {
  LogComponentEnable("GhostDagNode", LOG_LEVEL_INFO);
  // Simulation parameters
  double tStart = GetWallTime();
  double tStartSimulation;

  const uint16_t ghostdagPort = 16433;
  int targetNumberOfBlocks = 100;
  double averageBlockGenIntervalSeconds = 1.0;

  int totalNoNodes = 10;
  int minConnectionsPerNode = -1;
  int maxConnectionsPerNode = -1;
  int noMiners = 3;
  uint8_t ghostdagK = 10;

  double *minersHash;
  enum Region *minersRegions;

  // Default miner configuration for GHOSTDAG
  double defaultMinersHash[] = {0.15, 0.15, 0.12, 0.12, 0.10,
                                0.10, 0.08, 0.08, 0.05, 0.05};

  enum Region defaultMinersRegions[] = {
      NORTH_AMERICA, EUROPE,        ASIA_PACIFIC, NORTH_AMERICA, EUROPE,
      ASIA_PACIFIC,  NORTH_AMERICA, EUROPE,       ASIA_PACIFIC,  NORTH_AMERICA};

  double stop;

  Ipv4InterfaceContainer ipv4InterfaceContainer;
  std::map<uint32_t, std::vector<Ipv4Address>> nodesConnections;
  std::map<uint32_t, std::map<Ipv4Address, double>> peersDownloadSpeeds;
  std::map<uint32_t, std::map<Ipv4Address, double>> peersUploadSpeeds;
  std::map<uint32_t, NodeInternetSpeeds> nodesInternetSpeeds;
  std::vector<uint32_t> miners;

  Time::SetResolution(Time::NS);

  // Command line arguments
  CommandLine cmd;
  cmd.AddValue("blocks", "Number of blocks to generate", targetNumberOfBlocks);
  cmd.AddValue("nodes", "Total number of nodes", totalNoNodes);
  cmd.AddValue("miners", "Number of miners", noMiners);
  cmd.AddValue("minConnections", "Minimum connections per node",
               minConnectionsPerNode);
  cmd.AddValue("maxConnections", "Maximum connections per node",
               maxConnectionsPerNode);
  cmd.AddValue("blockInterval", "Average block generation interval (seconds)",
               averageBlockGenIntervalSeconds);
  cmd.AddValue("k", "GHOSTDAG k parameter", ghostdagK);

  cmd.Parse(argc, argv);

  if (noMiners > totalNoNodes) {
    std::cout << "Number of miners cannot exceed total nodes" << std::endl;
    return 0;
  }

  // Setup miner configuration
  minersHash = new double[noMiners];
  minersRegions = new enum Region[noMiners];

  if (noMiners <= 10) {
    for (int i = 0; i < noMiners; i++) {
      minersHash[i] = defaultMinersHash[i];
      minersRegions[i] = defaultMinersRegions[i];
    }
  } else {
    double hashPerMiner = 1.0 / noMiners;
    for (int i = 0; i < noMiners; i++) {
      minersHash[i] = hashPerMiner;
      minersRegions[i] = static_cast<Region>(i % 6);
    }
  }

  stop = targetNumberOfBlocks * averageBlockGenIntervalSeconds / 60;
  auto stats = new NodeStats[totalNoNodes];
  // Initialize stats
  for (int i = 0; i < totalNoNodes; i++) {
  }

  GlobalValue::Bind("SimulatorImplementationType",
                    StringValue("ns3::DistributedSimulatorImpl"));

  MpiInterface::Enable(&argc, &argv);
  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();

  if (systemId == 0) {
    std::cout << "\n=== GHOSTDAG Network Simulator ===\n";
    std::cout << "Total Nodes: " << totalNoNodes << "\n";
    std::cout << "Miners: " << noMiners << "\n";
    std::cout << "GHOSTDAG k: " << (int)ghostdagK << "\n";
    std::cout << "Block Interval: " << averageBlockGenIntervalSeconds << "s\n";
    std::cout << "Target Blocks: " << targetNumberOfBlocks << "\n";
    std::cout << "Simulation Duration: " << stop << " minutes\n\n";
  }

  // Create topology
  GhostDagTopologyHelper topologyHelper(systemCount, totalNoNodes, noMiners,
                                        minersRegions, minConnectionsPerNode,
                                        maxConnectionsPerNode, 5.0, systemId);

  // Install Internet stack
  InternetStackHelper stack;
  topologyHelper.InstallStack(stack);

  // Assign IP addresses
  topologyHelper.AssignIpv4Addresses(
      Ipv4AddressHelperCustom("10.1.0.0", "255.255.255.0", false));

  ipv4InterfaceContainer = topologyHelper.GetIpv4InterfaceContainer();
  nodesConnections = topologyHelper.GetNodesConnectionsIps();
  miners = topologyHelper.GetMiners();
  peersDownloadSpeeds = topologyHelper.GetPeersDownloadSpeeds();
  peersUploadSpeeds = topologyHelper.GetPeersUploadSpeeds();
  nodesInternetSpeeds = topologyHelper.GetNodesInternetSpeeds();

  // Install GHOSTDAG miners
  ApplicationContainer ghostdagMiners;
  for (auto &miner : miners) {
    Ptr<Node> targetNode = topologyHelper.GetNode(miner);

    if (systemId == targetNode->GetSystemId()) {
      GhostDagNodeHelper minerHelper(
          InetSocketAddress(Ipv4Address::GetAny(), ghostdagPort),
          nodesConnections[miner], peersDownloadSpeeds[miner],
          peersUploadSpeeds[miner], nodesInternetSpeeds[miner], &stats[miner]);

      // Configure miner-specific attributes
      minerHelper.SetAttribute("Kghostdag", UintegerValue(ghostdagK));

      ghostdagMiners.Add(minerHelper.Install(targetNode));
    }
  }

  ghostdagMiners.Start(Seconds(0));
  ghostdagMiners.Stop(Minutes(stop));

  // Install regular GHOSTDAG nodes
  ApplicationContainer ghostdagNodes;

  for (auto &node : nodesConnections) {
    Ptr<Node> targetNode = topologyHelper.GetNode(node.first);

    if (systemId == targetNode->GetSystemId()) {
      if (std::find(miners.begin(), miners.end(), node.first) == miners.end()) {
        GhostDagNodeHelper nodeHelper(
            InetSocketAddress(Ipv4Address::GetAny(), ghostdagPort), node.second,
            peersDownloadSpeeds[node.first], peersUploadSpeeds[node.first],
            nodesInternetSpeeds[node.first], &stats[node.first]);

        nodeHelper.SetAttribute("Kghostdag", UintegerValue(ghostdagK));

        ghostdagNodes.Add(nodeHelper.Install(targetNode));
      }
    }
  }

  ghostdagNodes.Start(Seconds(0));
  ghostdagNodes.Stop(Minutes(stop));

  if (systemId == 0) {
    std::cout << "Applications configured and ready.\n";
  }

  tStartSimulation = GetWallTime();
  if (systemId == 0) {
    std::cout << "Setup time = " << tStartSimulation - tStart << "s\n";
  }

  Simulator::Stop(Minutes(stop + 0.1));

  Simulator::Run();
  Simulator::Destroy();

  MpiInterface::Disable();

  delete[] stats;
  delete[] minersHash;
  delete[] minersRegions;

  return 0;
}

double GetWallTime() {
  struct timeval time;
  if (gettimeofday(&time, nullptr)) {
    return 0;
  }
  return (double)time.tv_sec + (double)time.tv_usec * .000001;
}
