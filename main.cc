/**
 * @file main.cc
 * @brief NS3 simulation entrypoint and configurations
 * @author Eduardo Ramos <eduardo_ramos@edu.univali.br>
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
#include <sys/stat.h>
#include <sys/time.h>

#ifndef NS3_MPI
#error                                                                         \
    "Distributed simulations need to run with NS3_MPI module, reconfigure and build your ns3 waf again pls"
#endif

using namespace ns3;

double GetWallTime();
NS_LOG_COMPONENT_DEFINE("GhostDagSimulator");

int main(int argc, char *argv[]) {
  double tStart = GetWallTime();
  double tStartSimulation;

  const uint16_t ghostdagPort = 16433;

  int totalNoNodes = 10;
  int minConnectionsPerNode = -1;
  int maxConnectionsPerNode = -1;
  int noMiners = 10;

  uint32_t ghostdagK = 10;

  double lambda = 20.0;
  double tau = 1.0;
  double pareto_shape_divider = 5.0;
  int txsPerBlock = 100;
  int mempoolSize = 10000;
  double txFeeLambda = 150.0;
  double txGenInterval = 0.5;

  int targetBlocksPerMiner = 1000;

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

  enum Region *minersRegions;
  int *minersStrategies;
  std::string metrics_scenario = "run0";

  Time::SetResolution(Time::NS);

  CommandLine cmd;
  cmd.AddValue("nodes", "Total number of nodes", totalNoNodes);
  cmd.AddValue("miners", "Number of miners", noMiners);
  cmd.AddValue("min_conn", "Minimum connections per node",
               minConnectionsPerNode);
  cmd.AddValue("max_conn", "Maximum connections per node",
               maxConnectionsPerNode);
  cmd.AddValue("lambda", "Mean block interval per miner (seconds)", lambda);
  cmd.AddValue("tau", "Propagation delay multiplier", tau);
  cmd.AddValue("pareto_divider",
               "Propagation latency Pareto distribution shape divider",
               pareto_shape_divider);
  cmd.AddValue("k", "GHOSTDAG k parameter", ghostdagK);
  cmd.AddValue("txs_per_block", "Transactions per block", txsPerBlock);
  cmd.AddValue("mempool_size", "Mempool size", mempoolSize);
  cmd.AddValue("tx_fee_lambda",
               "Mean transaction fee (exponential distribution)", txFeeLambda);
  cmd.AddValue("tx_gen_interval",
               "Mean transaction generation interval per node (seconds)",
               txGenInterval);
  cmd.AddValue("blocks_per_miner",
               "Target number of blocks each miner should produce",
               targetBlocksPerMiner);
  cmd.AddValue("run_name", "Name tag for this simulation run",
               metrics_scenario);
  cmd.Parse(argc, argv);

  if (noMiners > totalNoNodes) {
    std::cerr << "Error: number of miners (" << noMiners
              << ") cannot exceed total nodes (" << totalNoNodes << ")\n";
    return 1;
  }

  minersRegions = new enum Region[noMiners];
  minersStrategies = new int[noMiners];

  if (noMiners <= 10) {
    for (int i = 0; i < noMiners; i++) {
      minersRegions[i] = defaultMinersRegions[i];
      minersStrategies[i] = defaultMinersStrategies[i];
    }
  } else {
    for (int i = 0; i < noMiners; i++) {
      minersRegions[i] = static_cast<Region>(i % 6);
      minersStrategies[i] = 0;
    }
  }

  stop = (targetBlocksPerMiner * lambda) / 60.0;

  GlobalValue::Bind("SimulatorImplementationType",
                    StringValue("ns3::DistributedSimulatorImpl"));

  MpiInterface::Enable(&argc, &argv);
  uint32_t systemId = MpiInterface::GetSystemId();
  uint32_t systemCount = MpiInterface::GetSize();

  EventLogger::Get().Init("results/" + metrics_scenario, systemId);
#ifdef GHOSTDAGSIM_METRICS
  if (systemId == 0) {
    nlohmann::json cfg;
    cfg["lambda"] = lambda;
    cfg["k"] = ghostdagK;
    cfg["tau"] = tau;
    cfg["nodes"] = totalNoNodes;
    cfg["miners"] = noMiners;
    cfg["tx_fee_lambda"] = txFeeLambda;
    cfg["mempool_size"] = mempoolSize;
    cfg["scenario_name"] = metrics_scenario;
    cfg["sim_duration_minutes"] = stop;
    cfg["tx_gen_interval"] = txGenInterval;
    cfg["txs_per_block"] = txsPerBlock;
    std::error_code ec;
    std::filesystem::create_directories("results/" + metrics_scenario, ec);
    std::ofstream f("results/" + metrics_scenario + "/config.json");
    f << cfg.dump(2) << "\n";
  }
#endif

  if (systemId == 0) {
    std::cout << "\n=== GHOSTDAG Network Simulator ===\n";
    std::cout << "Total Nodes:                " << totalNoNodes << "\n";
    std::cout << "Miners:                     " << noMiners << "\n";
    std::cout << "GHOSTDAG k:                 " << ghostdagK << "\n";
    std::cout << "Lambda (block interval):    " << lambda << "s\n";
    std::cout << "Tau (propagation mult.):    " << tau << "\n";
    std::cout << "Txs per block:              " << txsPerBlock << "\n";
    std::cout << "Mempool size:               " << mempoolSize << "\n";
    std::cout << "Tx fee lambda (mean):       " << txFeeLambda << "\n";
    std::cout << "Tx gen interval (mean):     " << txGenInterval << "s\n";
    std::cout << "Target blocks per miner:    " << targetBlocksPerMiner << "\n";
    std::cout << "Expected total DAG blocks:  "
              << targetBlocksPerMiner * noMiners << "\n";
    std::cout << "Simulation duration:        " << stop << " minutes\n\n";
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

      minerHelper.SetAttribute("Kghostdag",
                               UintegerValue(static_cast<uint8_t>(ghostdagK)));
      minerHelper.SetAttribute("BlockGenInterval", DoubleValue(lambda));
      minerHelper.SetAttribute("TxsPerBlock", UintegerValue(txsPerBlock));
      minerHelper.SetAttribute("TxSelectionStrategy",
                               UintegerValue(minersStrategies[i]));
      minerHelper.SetAttribute("MempoolSize", UintegerValue(mempoolSize));
      minerHelper.SetAttribute("TxFeeLambda", DoubleValue(txFeeLambda));

      minerHelper.SetAttribute("TxGenInterval", DoubleValue(txGenInterval));

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

        nodeHelper.SetAttribute("Kghostdag",
                                UintegerValue(static_cast<uint8_t>(ghostdagK)));
        nodeHelper.SetAttribute("MempoolSize", UintegerValue(mempoolSize));
        nodeHelper.SetAttribute("TxFeeLambda", DoubleValue(txFeeLambda));
        nodeHelper.SetAttribute("TxGenInterval", DoubleValue(txGenInterval));

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
  EventLogger::Get().Close();

  MpiInterface::Disable();

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
