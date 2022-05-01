/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2007,2008,2009 INRIA, UDCAST
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
 *
 * Author: Amine Ismail <amine.ismail@sophia.inria.fr>
 *                      <amine.ismail@udcast.com>
 *
 */

#ifndef CUSTOM_APP_H
#define CUSTOM_APP_H

#include <vector>
#include <unordered_set>

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/address.h"
#include "ns3/traced-callback.h"
#include "ns3/packet-loss-counter.h"
#include "ns3/inet-socket-address.h"
#include "libwasmfaas.h"

namespace ns3 {
/**
 * \ingroup applications
 * \defgroup customapp CustomApp
 */

/**
 * \ingroup customapp
 *
 * \brief A UDP server, receives UDP packets from a remote host.
 *
 * UDP packets carry a 32bits sequence number followed by a 64bits time
 * stamp in their payloads. The application uses the sequence number
 * to determine if a packet is lost, and the time stamp to compute the delay.
 */
class CustomApp : public Application
{
public:
  /**
   * \brief Get the type ID.
   * \return the object TypeId
   */
  static TypeId GetTypeId (void);
  CustomApp ();
  virtual ~CustomApp ();
  /**
   * \brief Returns the number of lost packets
   * \return the number of lost packets
   */
  uint32_t GetLost (void) const;

  /**
   * \brief Returns the number of received packets
   * \return the number of received packets
   */
  uint64_t GetReceived (void) const;

  /**
   * \brief Returns the size of the window used for checking loss.
   * \return the size of the window used for checking loss.
   */
  uint16_t GetPacketWindowSize () const;

  /**
   * \brief Set the size of the window used for checking loss. This value should
   *  be a multiple of 8
   * \param size the size of the window used for checking loss. This value should
   *  be a multiple of 8
   */
  void SetPacketWindowSize (uint16_t size);

  /**
   * \brief Get Current node X and Y position according to mobility model
   * \return Current node x position
   */
  double GetCurrentNodeXPosition (void);

  void RegisterWasmModule (char *name, char *base64_data);

  void RegisterNode (Ipv4Address address, uint16_t port);

  int32_t ExecuteModule (char *module_name, char *func_name, int32_t arg1, int32_t arg2);

  void QueryPeersForModule (char *name);

  uint64_t GetNodeId (void);
  void InitRuntime (void);

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  /**
   * \brief Handle a packet reception.
   *
   * This function is called by lower layers.
   *
   * \param socket the socket the packet was received to.
   */
  void HandleRead (Ptr<Socket> socket);
  void QueryPeersCallback (Ptr<Socket> socket);
  Ptr<Packet> HandlePeerPacket (Ptr<Packet> packet, Ptr<Socket> socket, Address from);
  void
  resolveTag (char c)
  {
  }

  uint16_t m_port; //!< Port on which we listen for incoming packets.
  Ptr<Socket> m_socket; //!< IPv4 Soscket
  uint64_t m_received; //!< Number of received packets
  uint64_t m_sent; //!< Number of sent packets

  PacketLossCounter m_lossCounter; //!< Lost packet counter
  u_int64_t m_runtime_id;
  u_int8_t m_is_querying_peers_idx;
  bool m_is_querying_peers;

  u_int32_t m_microseconds_eventloop_interval;

  std::vector<InetSocketAddress> m_peers_queried;
  std::string m_query_peers_func_name;
  std::vector<int32_t> m_query_peers_func_args;
  Address m_original_module_requester;

  u_int32_t m_peer_module_exec_query_state;
  bool m_has_module_exec_result;
  int32_t m_module_exec_result;

  bool m_is_waiting_for_module_load = false;

  std::vector<InetSocketAddress> m_peerAddresses; //!< Remote peer address

  /// Callbacks for tracing the packet Rx events
  TracedCallback<Ptr<const Packet>> m_rxTrace;

  /// Callbacks for tracing the packet Rx events, includes source and destination addresses
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_rxTraceWithAddresses;
};

} // namespace ns3

#endif /* CUSTOM_APP_H */
