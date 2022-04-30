/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
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
 */

#include "ns3/core-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/network-module.h"
#include "ns3/applications-module.h"
#include "ns3/mobility-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/ssid.h"
#include "ns3/applications-module.h"
#include "ns3/custom-app-helper.h"
#include "ns3/custom-app.h"
#include "ns3/custom-app-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("WifiSimWasm");

int
main (int argc, char *argv[])
{

  bool verbose = true;
  uint32_t nWifi = 3;
  bool tracing = false;

  CommandLine cmd (__FILE__);
  cmd.AddValue ("nWifi", "Number of wifi STA devices", nWifi);
  cmd.AddValue ("verbose", "Tell echo applications to log if true", verbose);
  cmd.AddValue ("tracing", "Enable pcap tracing", tracing);

  cmd.Parse (argc, argv);

  // // Get static wasm module data from static lib

  auto sumWasmBase64 = get_static_module_data (StaticModuleList::WasmSum);
  auto divWasmBase64 = get_static_module_data (StaticModuleList::WasmDiv);

  Time::SetResolution (Time::NS);
  LogComponentEnable ("CustomApp", LOG_LEVEL_INFO);
  LogComponentEnable ("UdpEchoClientApplication", LOG_LEVEL_INFO);

  // Create 2 point to point nodes, one being the server and another one being the wireless AP
  NodeContainer p2pNodes;
  p2pNodes.Create (2);
  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("100Mbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));

  NetDeviceContainer p2pdevices;
  p2pdevices = pointToPoint.Install (p2pNodes);

  // Fixed server node
  NodeContainer serverNode = p2pNodes.Get (0);

  // Initialize wifi AP
  NodeContainer wifiApNode = p2pNodes.Get (1);
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy;
  phy.SetChannel (channel.Create ());

  // Set wiki AP params
  WifiHelper wifi;
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");

  WifiMacHelper mac;
  Ssid ssid = Ssid ("ns-3-ssid");
  mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid), "ActiveProbing", BooleanValue (false));

  NodeContainer wifiMobileNodes;
  wifiMobileNodes.Create (nWifi);

  //moving wifi devices
  NetDeviceContainer mobileDevices;
  mobileDevices = wifi.Install (phy, mac, wifiMobileNodes);
  mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));

  // Configure ap node device
  NetDeviceContainer apDevices;
  apDevices = wifi.Install (phy, mac, wifiApNode);

  // Position and mobility model for nodes
  MobilityHelper mobility;

  // Randomly move mobileNodes

  mobility.SetPositionAllocator ("ns3::GridPositionAllocator", "MinX", DoubleValue (0.0), "MinY",
                                 DoubleValue (0.0), "DeltaX", DoubleValue (5.0), "DeltaY",
                                 DoubleValue (10.0), "GridWidth", UintegerValue (3), "LayoutType",
                                 StringValue ("RowFirst"));

  mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel", "Bounds",
                             RectangleValue (Rectangle (-50, 50, -50, 50)));
  mobility.Install (wifiMobileNodes);

  // Fixed ap node
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (wifiApNode);

  InternetStackHelper stack;
  stack.Install (serverNode);
  stack.Install (wifiApNode);
  stack.Install (wifiMobileNodes);

  // Server and ap  base 10.1.1.0
  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer p2pinterfaces = address.Assign (p2pdevices);

  address.SetBase ("10.1.2.0", "255.255.255.0");

  Ipv4InterfaceContainer apInterface = address.Assign (apDevices);
  Ipv4InterfaceContainer wifiInterfaces = address.Assign (mobileDevices);

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  CustomAppHelper wasmFaasHelper = CustomAppHelper (3000);

  ApplicationContainer serverApps = wasmFaasHelper.Install (p2pNodes);
  ApplicationContainer wifiApps = wasmFaasHelper.Install (wifiMobileNodes);

  auto idx = 0;
  std::cout << "[NodeIps] " << std::endl;
  for (auto i = p2pNodes.Begin (); i != p2pNodes.End (); ++i)
    {
      auto node = (*i);
      auto wasmRuntime = node->GetApplication (0)->GetObject<CustomApp> ();
      wasmRuntime->InitRuntime ();

      auto id = wasmRuntime->GetNodeId ();
      std::cout << id << " " << p2pinterfaces.GetAddress (idx) << std::endl;

      idx++;
    }

  // AP IP
  std::cout << p2pNodes.Get (1)->GetApplication (0)->GetObject<CustomApp> ()->GetNodeId () << " "
            << apInterface.GetAddress (0) << std::endl;

  idx = 0;
  for (auto i = wifiMobileNodes.Begin (); i != wifiMobileNodes.End (); ++i)
    {
      auto node = (*i);
      auto wasmRuntime = node->GetApplication (0)->GetObject<CustomApp> ();
      wasmRuntime->InitRuntime ();

      auto id = wasmRuntime->GetNodeId ();
      std::cout << id << " " << wifiInterfaces.GetAddress (idx) << std::endl;

      idx++;
    }

  std::cout << "[Setup] " << std::endl;

  auto p2pServerNode = p2pNodes.Get (0)->GetApplication (0)->GetObject<CustomApp> ();
  // Install apps on p2p nodes
  p2pServerNode->RegisterWasmModule ((char *) "sum", sumWasmBase64);
  p2pServerNode->RegisterWasmModule ((char *) "div", divWasmBase64);

  auto wifiApNodeServer = p2pNodes.Get (1)->GetApplication (0)->GetObject<CustomApp> ();

  // Register p2p nodes with each other
  p2pServerNode->RegisterNode (p2pinterfaces.GetAddress (1), 3000);
  wifiApNodeServer->RegisterNode (p2pinterfaces.GetAddress (0), 3000);

  idx = 0;
  // Register wifi nodes with each other and ap node
  for (auto i = wifiMobileNodes.Begin (); i != wifiMobileNodes.End (); ++i)
    {
      auto jIdx = 0;
      auto node = (*i);
      auto nodeWasmRuntime = node->GetApplication (0)->GetObject<CustomApp> ();
      nodeWasmRuntime->RegisterNode (apInterface.GetAddress (0), 3000);
      for (auto j = wifiMobileNodes.Begin (); j != wifiMobileNodes.End (); ++j)
        {
          if (i != j)
            {
              nodeWasmRuntime->RegisterNode (wifiInterfaces.GetAddress (jIdx), 3000);
            }
          jIdx++;
        }
      idx++;
    }

  auto wifiNode0 = wifiMobileNodes.Get (0)->GetApplication (0)->GetObject<CustomApp> ();

  Simulator::Schedule (Seconds (2), &CustomApp::ExecuteModule, wifiNode0, (char *) "div",
                       (char *) "div", 10, 10);

  // Simulator::Schedule (Seconds (4), &CustomApp::ExecuteModule, wifiApNodeServer, (char *) "sum",
  //                      (char *) "sum", 100, 100);

  // Simulator::Schedule (Seconds (5), &CustomApp::ExecuteModule, wifiApNodeServer, (char *) "div",
  //                      (char *) "div", 100, 5);

  // Simulator::Schedule (Seconds (6), &CustomApp::ExecuteModule, wifiApNodeServer, (char *) "sum",
  //                      (char *) "sum", 100, 100);

  std::cout << "[Main] " << std::endl;
  serverApps.Start (Seconds (1.0));
  serverApps.Stop (Seconds (10.0));

  Simulator::Stop (Seconds (10.0));
  Simulator::Run ();
  Simulator::Destroy ();
  return 0;
}
