#include "dag.h"
#include "helpers/node-helper.h"
#include "helpers/node-topology-helper.h"
#include "metrics.h"

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
  // LogComponentEnable("GhostDagNode", LOG_LEVEL_INFO);
  // LogComponentEnable("GhostDagMiner", LOG_LEVEL_INFO);

  double tStart = GetWallTime();
  double tStartSimulation;

  const uint16_t ghostdagPort = 16433;

  int totalNoNodes = 10;
  int minConnectionsPerNode = -1;
  int maxConnectionsPerNode = -1;
  int noMiners = 10;

  uint8_t ghostdagK = 10;

  double lambda = 20.0;
  double tau = 1.0;
  double pareto_shape_divider = 5.0;
  int txsPerBlock = 100;
  int mempoolSize = 10000;
  double txFeeLambda = 150.0;
  int targetNumberOfBlocks = 10000;

  uint16_t metricsPrometheusPort = 9091;
  double metricsFlushInterval = 2;

  double *minersHash;
  enum Region *minersRegions;
  int *minersStrategies;

  double defaultMinersHash[] = {0.15, 0.15, 0.12, 0.12, 0.10,
                                0.10, 0.08, 0.08, 0.05, 0.05};

  enum Region defaultMinersRegions[] = {
      NORTH_AMERICA, EUROPE,        ASIA_PACIFIC, NORTH_AMERICA, EUROPE,
      ASIA_PACIFIC,  NORTH_AMERICA, EUROPE,       ASIA_PACIFIC,  NORTH_AMERICA};

  int defaultMinersStrategies[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  double stop;

  Ipv4InterfaceContainer ipv4InterfaceContainer;
  std::map<uint32_t, std::vector<Ipv4Address>> nodesConnections;
  std::map<uint32_t, std::map<Ipv4Address, double>> peersDownloadSpeeds;
  std::map<uint32_t, std::map<Ipv4Address, double>> peersUploadSpeeds;
  std::map<uint32_t, NodeInternetSpeeds> nodesInternetSpeeds;
  std::vector<uint32_t> miners;

  Time::SetResolution(Time::NS);

  CommandLine cmd;
  cmd.AddValue("nodes", "Total number of nodes", totalNoNodes);
  cmd.AddValue("miners", "Number of miners", noMiners);
  cmd.AddValue("min_conn", "Minimum connections per node",
               minConnectionsPerNode);
  cmd.AddValue("max_conn", "Maximum connections per node",
               maxConnectionsPerNode);
  cmd.AddValue("lambda", "Block creation rate (seconds)", lambda);
  cmd.AddValue("tau", "Propagation delay multiplier", tau);
  cmd.AddValue("pareto_divider",
               "Propagation latency pareto distribution divider",
               pareto_shape_divider);
  cmd.AddValue("k", "GHOSTDAG k parameter", ghostdagK);
  cmd.AddValue("txs_per_block", "Transactions per block", txsPerBlock);
  cmd.AddValue("mempool_size", "Mempool size", mempoolSize);
  cmd.AddValue("tx_fee_lambda", "Transaction fee exponential lambda",
               txFeeLambda);
  cmd.AddValue("blocks", "Number of blocks to generate", targetNumberOfBlocks);
  cmd.AddValue("metrics_port", "Prometheus exporter port",
               metricsPrometheusPort);
  cmd.AddValue("metrics_flush_interval",
               "Periodic flush interval in seconds (0 to disable)",
               metricsFlushInterval);

  cmd.Parse(argc, argv);

  if (noMiners > totalNoNodes) {
    std::cout << "Number of miners cannot exceed total nodes" << std::endl;
    return 0;
  }

  minersHash = new double[noMiners];
  minersRegions = new enum Region[noMiners];
  minersStrategies = new int[noMiners];

  if (noMiners <= 10) {
    for (int i = 0; i < noMiners; i++) {
      minersHash[i] = defaultMinersHash[i];
      minersRegions[i] = defaultMinersRegions[i];
      minersStrategies[i] = defaultMinersStrategies[i];
    }
  } else {
    double hashPerMiner = 1.0 / noMiners;
    for (int i = 0; i < noMiners; i++) {
      minersHash[i] = hashPerMiner;
      minersRegions[i] = static_cast<Region>(i % 6);
      minersStrategies[i] = 0;
    }
  }

  stop = (targetNumberOfBlocks * lambda / 60.0) / noMiners;

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
    std::cout << "Lambda (block interval): " << lambda << "s\n";
    std::cout << "Tau (propagation delay): " << tau << "s\n";
    std::cout << "Txs per block: " << txsPerBlock << "\n";
    std::cout << "Mempool size: " << mempoolSize << "\n";
    std::cout << "Tx fee lambda: " << txFeeLambda << "\n";
    std::cout << "Target Blocks: " << targetNumberOfBlocks << "\n";
    std::cout << "Simulation Duration: " << stop << " minutes\n\n";
  }

  GhostDagTopologyHelper topologyHelper(
      systemCount, totalNoNodes, noMiners, minersRegions, minConnectionsPerNode,
      maxConnectionsPerNode, pareto_shape_divider, tau, systemId);

  InternetStackHelper stack;
  topologyHelper.InstallStack(stack);

  topologyHelper.AssignIpv4Addresses(
      Ipv4AddressHelperCustom("10.1.0.0", "255.255.255.0", false));

  ipv4InterfaceContainer = topologyHelper.GetIpv4InterfaceContainer();
  nodesConnections = topologyHelper.GetNodesConnectionsIps();
  miners = topologyHelper.GetMiners();
  peersDownloadSpeeds = topologyHelper.GetPeersDownloadSpeeds();
  peersUploadSpeeds = topologyHelper.GetPeersUploadSpeeds();
  nodesInternetSpeeds = topologyHelper.GetNodesInternetSpeeds();

  ApplicationContainer ghostdagMiners;
  for (size_t i = 0; i < miners.size(); i++) {
    uint32_t minerId = miners[i];
    Ptr<Node> targetNode = topologyHelper.GetNode(minerId);

    if (systemId == targetNode->GetSystemId()) {
      GhostDagMinerHelper minerHelper(
          InetSocketAddress(Ipv4Address::GetAny(), ghostdagPort),
          nodesConnections[minerId], peersDownloadSpeeds[minerId],
          peersUploadSpeeds[minerId], nodesInternetSpeeds[minerId]);

      minerHelper.SetAttribute("Kghostdag", UintegerValue(ghostdagK));
      minerHelper.SetAttribute("BlockGenInterval", DoubleValue(lambda));
      minerHelper.SetAttribute("TxsPerBlock", UintegerValue(txsPerBlock));
      minerHelper.SetAttribute("TxSelectionStrategy",
                               UintegerValue(minersStrategies[i]));
      minerHelper.SetAttribute("MempoolSize", UintegerValue(mempoolSize));
      minerHelper.SetAttribute("TxFeeLambda", DoubleValue(txFeeLambda));

      ghostdagMiners.Add(minerHelper.Install(targetNode));
    }
  }

  ghostdagMiners.Start(Seconds(0));
  ghostdagMiners.Stop(Minutes(stop));

  ApplicationContainer ghostdagNodes;
  for (auto &node : nodesConnections) {
    Ptr<Node> targetNode = topologyHelper.GetNode(node.first);

    if (systemId == targetNode->GetSystemId()) {
      if (std::find(miners.begin(), miners.end(), node.first) == miners.end()) {
        GhostDagNodeHelper nodeHelper(
            InetSocketAddress(Ipv4Address::GetAny(), ghostdagPort), node.second,
            peersDownloadSpeeds[node.first], peersUploadSpeeds[node.first],
            nodesInternetSpeeds[node.first]);

        nodeHelper.SetAttribute("Kghostdag", UintegerValue(ghostdagK));
        nodeHelper.SetAttribute("MempoolSize", UintegerValue(mempoolSize));
        nodeHelper.SetAttribute("TxFeeLambda", DoubleValue(txFeeLambda));

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

  delete[] minersHash;
  delete[] minersRegions;
  delete[] minersStrategies;

  return 0;
}

double GetWallTime() {
  struct timeval time;
  if (gettimeofday(&time, nullptr)) {
    return 0;
  }
  return (double)time.tv_sec + (double)time.tv_usec * .000001;
}
