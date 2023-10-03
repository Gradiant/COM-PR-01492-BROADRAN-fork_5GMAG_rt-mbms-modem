// 5G-MAG Reference Tools
// MBMS Modem Process
//
// Copyright (C) 2021 Klaus Kühnhammer (Österreichische Rundfunksender GmbH & Co KG)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
// 
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#pragma once
#include "srsran/srsran.h"
#include "srsran/rlc/rlc.h"
#include "srsran/asn1/rrc.h"
#include "srsran/interfaces/ue_gw_interfaces.h"

#include <string>
#include <libconfig.h++>

#include "Phy.h"

/**
 *  Network gateway component.
 *
 *  Creates a TUN network interface, and writes the received MCH PDU contents out on it.
 */
class Gw : public srsue::gw_interface_stack {
  public:
    /**
     *  Default constructor.
     *
     *  @param cfg Config singleton reference
     *  @param phy PHY reference
     */
    Gw(const libconfig::Config& /*cfg*/, Phy& phy)
        : _phy(phy)
      {}

    /**
     *  Default destructor.
     */
    virtual ~Gw();

    /**
     *  Creates the TUN interface according to params from Cfg
     */
    void init();

    /**
     *  Handle a MCH PDU. Verifies the contents start with an IP header, checks the IP header checksum
     *  and corrects it if necessary, and writes the packet out to the TUN interface. 
     */
    void write_pdu_mch(uint32_t mch_idx, uint32_t lcid, srsran::unique_byte_buffer_t pdu) override;

    // Unused interface methods
    void add_mch_port(uint32_t /*lcid*/, uint32_t /*port*/) override {};
    void write_pdu(uint32_t /*lcid*/, srsran::unique_byte_buffer_t /*pdu*/) override {};
    int setup_if_addr(uint32_t /*lcid*/, uint8_t /*pdn_type*/, uint32_t /*ip_addr*/, uint8_t* /*ipv6_if_id*/, char* /*err_str*/) override { return -1; };
    int apply_traffic_flow_template(const uint8_t& /*eps_bearer_id*/, const LIBLTE_MME_TRAFFIC_FLOW_TEMPLATE_STRUCT* /*tft*/) override { return -1; };
    void set_test_loop_mode(const test_loop_mode_state_t /*mode*/, const uint32_t /*ip_pdu_delay_ms = 0*/) override {};

    int deactivate_eps_bearer(const uint32_t /*eps_bearer_id*/) override {return 0;};
    bool is_running() override { return true; };
  private:
    std::mutex _wr_mutex;
    int32_t _tun_fd = -1;
    Phy& _phy;
};
