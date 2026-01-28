#include "dag.h"
#include "node-helper.h"
#include "node-topology-helper.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/mpi-interface.h"
#include "ns3/netanim-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"

#include <sys/time.h>

#define MPI_TEST
#define NS3_MPI

#ifdef NS3_MPI
#include <mpi.h>
#endif

using namespace ns3;

// Function declarations
double GetWallTime();
void PrintStatsForEachNode(NodeStats *stats, int totalNodes);
void PrintTotalStats(NodeStats *stats, int totalNodes, double start,
                     double finish, double averageBlockGenIntervalMinutes);
void PrintRegionStats(uint32_t *nodesRegions, uint32_t totalNodes);

NS_LOG_COMPONENT_DEFINE("GhostDagSimulator");

int main(int argc, char *argv[]) {
  LogComponentEnable("GhostDagNode", LOG_LEVEL_INFO);
#ifdef NS3_MPI
  // Simulation parameters
  bool nullmsg = false;
  bool testScalability = false;
  double tStart = GetWallTime();
  double tStartSimulation;
  double tFinish;

  const int secsPerMin = 60;
  const uint16_t ghostdagPort = 16433;
  int targetNumberOfBlocks = 1000;
  double averageBlockGenIntervalSeconds = 1.0;

  int totalNoNodes = 1000;
  int minConnectionsPerNode = 4;
  int maxConnectionsPerNode = 8;
  int noMiners = 5;
  uint8_t ghostdagK = 10;

  double *minersHash;
  enum Region *minersRegions;

  // Default miner configuration for GHOSTDAG
  double defaultMinersHash[] = {0.15, 0.15, 0.12, 0.12, 0.10,
                                0.10, 0.08, 0.08, 0.05, 0.05};

  enum Region defaultMinersRegions[] = {
      NORTH_AMERICA, EUROPE,        ASIA_PACIFIC, NORTH_AMERICA, EUROPE,
      ASIA_PACIFIC,  NORTH_AMERICA, EUROPE,       ASIA_PACIFIC,  NORTH_AMERICA};

  double averageBlockGenIntervalMinutes =
      averageBlockGenIntervalSeconds / secsPerMin;
  double stop;

  Ipv4InterfaceContainer ipv4InterfaceContainer;
  std::map<uint32_t, std::vector<Ipv4Address>> nodesConnections;
  std::map<uint32_t, std::map<Ipv4Address, double>> peersDownloadSpeeds;
  std::map<uint32_t, std::map<Ipv4Address, double>> peersUploadSpeeds;
  std::map<uint32_t, NodeInternetSpeeds> nodesInternetSpeeds;
  std::vector<uint32_t> miners;
  int nodesInSystemId0 = 0;

  Time::SetResolution(Time::NS);

  // Command line arguments
  CommandLine cmd;
  cmd.AddValue("nullmsg", "Enable null-message synchronization", nullmsg);
  cmd.AddValue("noBlocks", "Number of blocks to generate",
               targetNumberOfBlocks);
  cmd.AddValue("nodes", "Total number of nodes", totalNoNodes);
  cmd.AddValue("miners", "Number of miners", noMiners);
  cmd.AddValue("minConnections", "Minimum connections per node",
               minConnectionsPerNode);
  cmd.AddValue("maxConnections", "Maximum connections per node",
               maxConnectionsPerNode);
  cmd.AddValue("blockInterval", "Average block generation interval (seconds)",
               averageBlockGenIntervalSeconds);
  cmd.AddValue("k", "GHOSTDAG k parameter", ghostdagK);
  cmd.AddValue("test", "Test scalability", testScalability);

  cmd.Parse(argc, argv);

  // Validation
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
    // Distribute hash power evenly for more miners
    double hashPerMiner = 1.0 / noMiners;
    for (int i = 0; i < noMiners; i++) {
      minersHash[i] = hashPerMiner;
      minersRegions[i] = static_cast<Region>(i % 6);
    }
  }

  stop = targetNumberOfBlocks * averageBlockGenIntervalSeconds / secsPerMin;
  auto stats = new NodeStats[totalNoNodes];

  // Initialize stats
  for (int i = 0; i < totalNoNodes; i++) {
    stats[i].node_id = i;
    stats[i].mean_block_receive_time = 0;
    stats[i].mean_block_propagation_time = 0;
    stats[i].mean_block_size = 0;
    stats[i].total_blocks = 0;
    stats[i].blue_blocks = 0;
    stats[i].red_blocks = 0;
    stats[i].orphan_rate = 0;
    stats[i].is_miner = false;
    stats[i].miner_generated_blocks = 0;
    stats[i].connections = 0;
    stats[i].max_dag_width_seen = 0;
  }

#ifdef MPI_TEST
  // Distributed simulation setup
  if (nullmsg) {
    GlobalValue::Bind("SimulatorImplementationType",
                      StringValue("ns3::NullMessageSimulatorImpl"));
  } else {
    GlobalValue::Bind("SimulatorImplementationType",
                      StringValue("ns3::DistributedSimulatorImpl"));
  }

  MpiInterface::Enable(&argc, &argv);
  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();
#else
  uint32_t systemId = 0;
  uint32_t systemCount = 1;
#endif

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

  if (systemId == 0) {
    PrintRegionStats(topologyHelper.GetNodesRegions(), totalNoNodes);
  }

  // Install GHOSTDAG miners
  ApplicationContainer ghostdagMiners;
  int minerCount = 0;

  for (auto &miner : miners) {
    Ptr<Node> targetNode = topologyHelper.GetNode(miner);

    if (systemId == targetNode->GetSystemId()) {
      GhostDagNodeHelper minerHelper(
          InetSocketAddress(Ipv4Address::GetAny(), ghostdagPort),
          nodesConnections[miner], peersDownloadSpeeds[miner],
          peersUploadSpeeds[miner], nodesInternetSpeeds[miner], &stats[miner]);

      // Configure miner-specific attributes
      minerHelper.SetAttribute("IsMiner", BooleanValue(true));
      minerHelper.SetAttribute("Kghostdag", UintegerValue(ghostdagK));

      if (testScalability) {
        minerHelper.SetAttribute("FixedBlockInterval",
                                 DoubleValue(averageBlockGenIntervalSeconds));
      }

      ghostdagMiners.Add(minerHelper.Install(targetNode));
      stats[miner].is_miner = true;
      stats[miner].hash_rate = minersHash[minerCount];

      if (systemId == 0) {
        nodesInSystemId0++;
      }
    }
    minerCount++;
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

        nodeHelper.SetAttribute("IsMiner", BooleanValue(false));
        nodeHelper.SetAttribute("Kghostdag", UintegerValue(ghostdagK));

        ghostdagNodes.Add(nodeHelper.Install(targetNode));

        if (systemId == 0) {
          nodesInSystemId0++;
        }
      }
    }
  }

  ghostdagNodes.Start(Seconds(0));
  ghostdagNodes.Stop(Minutes(stop));

  if (systemId == 0) {
    std::cout << "Applications configured and ready.\n";
  }

  // Run simulation
  tStartSimulation = GetWallTime();
  if (systemId == 0) {
    std::cout << "Setup time = " << tStartSimulation - tStart << "s\n";
  }

  Simulator::Stop(Minutes(stop + 0.1));

  AnimationInterface anim("topology.xml");
  MobilityHelper mobility;
  mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
  mobility.InstallAll();
  ns3::AnimationInterface::SetConstantPosition(ghostdagNodes.Get(0)->GetNode(),
                                               10.0, 10.0);
  Simulator::Run();
  Simulator::Destroy();

#ifdef MPI_TEST
  // MPI data gathering
  int blocklen[15] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  MPI_Aint disp[15];
  MPI_Datatype dtypes[15] = {MPI_INT, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE,
                             MPI_INT, MPI_INT,    MPI_INT,    MPI_DOUBLE,
                             MPI_INT, MPI_DOUBLE, MPI_DOUBLE, MPI_DOUBLE,
                             MPI_INT, MPI_LONG,   MPI_DOUBLE};
  MPI_Datatype mpi_nodeStatsType;

  disp[0] = offsetof(NodeStats, node_id);
  disp[1] = offsetof(NodeStats, mean_block_receive_time);
  disp[2] = offsetof(NodeStats, mean_block_propagation_time);
  disp[3] = offsetof(NodeStats, mean_block_size);
  disp[4] = offsetof(NodeStats, total_blocks);
  disp[5] = offsetof(NodeStats, blue_blocks);
  disp[6] = offsetof(NodeStats, red_blocks);
  disp[7] = offsetof(NodeStats, orphan_rate);
  disp[8] = offsetof(NodeStats, is_miner);
  disp[9] = offsetof(NodeStats, miner_generated_blocks);
  disp[10] = offsetof(NodeStats, miner_average_block_gen_interval);
  disp[11] = offsetof(NodeStats, hash_rate);
  disp[12] = offsetof(NodeStats, connections);
  disp[13] = offsetof(NodeStats, block_timeouts);
  disp[14] = offsetof(NodeStats, mempool_similarity_score);

  MPI_Type_create_struct(15, blocklen, disp, dtypes, &mpi_nodeStatsType);
  MPI_Type_commit(&mpi_nodeStatsType);

  if (systemId != 0 && systemCount > 1) {
    // Send stats to system 0
    for (int i = 0; i < totalNoNodes; i++) {
      Ptr<Node> targetNode = topologyHelper.GetNode(i);
      if (systemId == targetNode->GetSystemId()) {
        MPI_Send(&stats[i], 1, mpi_nodeStatsType, 0, 8888, MPI_COMM_WORLD);
      }
    }
  } else if (systemId == 0 && systemCount > 1) {
    // Receive stats from other systems
    int count = nodesInSystemId0;
    while (count < totalNoNodes) {
      MPI_Status status;
      NodeStats recv;
      MPI_Recv(&recv, 1, mpi_nodeStatsType, MPI_ANY_SOURCE, 8888,
               MPI_COMM_WORLD, &status);

      stats[recv.node_id] = recv;
      count++;
    }
  }
#endif

  // Print results
  if (systemId == 0) {
    tFinish = GetWallTime();

    std::cout << "\n=== Simulation Results ===\n";
    PrintTotalStats(stats, totalNoNodes, tStartSimulation, tFinish,
                    averageBlockGenIntervalMinutes);

    std::cout << "\nSimulation completed in " << tFinish - tStart << "s\n";
    std::cout << "Simulated " << stop << " minutes in " << tFinish - tStart
              << "s\n";
    std::cout << "Performance: " << stop * secsPerMin / (tFinish - tStart)
              << "x realtime\n";
    std::cout << "Setup time: " << tStartSimulation - tStart << "s\n";
    std::cout << "Network: " << totalNoNodes << " nodes (" << noMiners
              << " miners)\n";
    std::cout << "Connections: " << minConnectionsPerNode << "-"
              << maxConnectionsPerNode << " per node\n";
    std::cout << "GHOSTDAG k: " << (int)ghostdagK << "\n";
    std::cout << "Block interval: " << averageBlockGenIntervalSeconds << "s\n";
  }

#ifdef MPI_TEST
  MpiInterface::Disable();
#endif

  delete[] stats;
  delete[] minersHash;
  delete[] minersRegions;

  return 0;

#else
  NS_FATAL_ERROR("Cannot use distributed simulator without MPI compiled in");
#endif
}

double GetWallTime() {
  struct timeval time;
  if (gettimeofday(&time, nullptr)) {
    return 0;
  }
  return (double)time.tv_sec + (double)time.tv_usec * .000001;
}

void PrintStatsForEachNode(NodeStats *stats, int totalNodes) {
  for (int i = 0; i < totalNodes; i++) {
    std::cout << "\n--- Node " << stats[i].node_id << " ---\n";
    std::cout << "Connections: " << stats[i].connections << "\n";
    std::cout << "Total Blocks: " << stats[i].total_blocks << "\n";
    std::cout << "Blue Blocks: " << stats[i].blue_blocks << "\n";
    std::cout << "Red Blocks: " << stats[i].red_blocks << "\n";
    std::cout << "Blue Ratio: "
              << (stats[i].total_blocks > 0
                      ? 100.0 * stats[i].blue_blocks / stats[i].total_blocks
                      : 0)
              << "%\n";
    std::cout << "Mean Block Propagation: "
              << stats[i].mean_block_propagation_time << "s\n";
    std::cout << "Max DAG Width: " << stats[i].max_dag_width_seen << "\n";

    if (stats[i].is_miner) {
      std::cout << "MINER - Hash Rate: " << stats[i].hash_rate * 100 << "%\n";
      std::cout << "Generated Blocks: " << stats[i].miner_generated_blocks
                << "\n";
      std::cout << "Avg Gen Interval: "
                << stats[i].miner_average_block_gen_interval << "s\n";
    }
  }
}

void PrintTotalStats(NodeStats *stats, int totalNodes, double start,
                     double finish, double averageBlockGenIntervalMinutes) {
  double meanBlockPropagation = 0;
  int totalBlocks = 0;
  int totalBlueBlocks = 0;
  int totalRedBlocks = 0;
  int maxDAGWidth = 0;
  double avgConnections = 0;
  double avgMinerConnections = 0;
  int nodeCount = 0;
  int minerCount = 0;

  std::vector<double> propagationTimes;

  for (int i = 0; i < totalNodes; i++) {
    if (stats[i].total_blocks > 0) {
      meanBlockPropagation += stats[i].mean_block_propagation_time;
      totalBlocks += stats[i].total_blocks;
      totalBlueBlocks += stats[i].blue_blocks;
      totalRedBlocks += stats[i].red_blocks;
      propagationTimes.push_back(stats[i].mean_block_propagation_time);
    }

    if (stats[i].max_dag_width_seen > maxDAGWidth) {
      maxDAGWidth = stats[i].max_dag_width_seen;
    }

    if (stats[i].is_miner) {
      avgMinerConnections += stats[i].connections;
      minerCount++;
    } else {
      avgConnections += stats[i].connections;
      nodeCount++;
    }
  }

  if (totalNodes > 0) {
    meanBlockPropagation /= totalNodes;
    totalBlocks /= totalNodes;
    totalBlueBlocks /= totalNodes;
    totalRedBlocks /= totalNodes;
  }

  if (nodeCount > 0) {
    avgConnections /= nodeCount;
  }
  if (minerCount > 0) {
    avgMinerConnections /= minerCount;
  }

  std::sort(propagationTimes.begin(), propagationTimes.end());
  double median = propagationTimes.size() > 0
                      ? propagationTimes[propagationTimes.size() / 2]
                      : 0;

  std::cout << "\n=== GHOSTDAG Network Statistics ===\n";
  std::cout << "Average Connections (nodes): " << avgConnections << "\n";
  std::cout << "Average Connections (miners): " << avgMinerConnections << "\n";
  std::cout << "Mean Block Propagation Time: " << meanBlockPropagation << "s\n";
  std::cout << "Median Block Propagation Time: " << median << "s\n";
  std::cout << "Total Blocks (avg per node): " << totalBlocks << "\n";
  std::cout << "Blue Blocks (avg): " << totalBlueBlocks << "\n";
  std::cout << "Red Blocks (avg): " << totalRedBlocks << "\n";
  std::cout << "Blue Ratio: "
            << (totalBlocks > 0 ? 100.0 * totalBlueBlocks / totalBlocks : 0)
            << "%\n";
  std::cout << "Max DAG Width Observed: " << maxDAGWidth << "\n";
  std::cout << "Simulation Time: " << finish - start << "s\n";

  if (totalBlocks > 0) {
    std::cout << "Time per Block: " << (finish - start) / totalBlocks << "s\n";
  }
}

void PrintRegionStats(uint32_t *nodesRegions, uint32_t totalNodes) {
  uint32_t regions[7] = {0, 0, 0, 0, 0, 0, 0};

  for (uint32_t i = 0; i < totalNodes; i++) {
    regions[nodesRegions[i]]++;
  }

  std::cout << "Node Distribution by Region:\n";
  const char *regionNames[] = {"North America", "Europe", "South America",
                               "Asia Pacific",  "Japan",  "Australia",
                               "Other"};

  for (uint32_t i = 0; i < 7; i++) {
    if (regions[i] > 0) {
      std::cout << "  " << regionNames[i] << ": "
                << regions[i] * 100.0 / totalNodes << "%\n";
    }
  }
  std::cout << "\n";
}
