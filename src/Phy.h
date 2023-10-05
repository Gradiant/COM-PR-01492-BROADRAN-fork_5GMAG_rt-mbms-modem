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

#include <functional>
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <libconfig.h++>

#include "srsran/srsran.h"
#include "srsran/interfaces/rrc_interface_types.h"
#include "srsran/common/gen_mch_tables.h"
#include "srsran/phy/common/phy_common.h"

constexpr unsigned int MAX_PRB = 100;

/**
 *  The PHY component. Handles synchronisation and is the central hub for
 *  lower level processing
 *
 */
class Phy {
 public:
    /**
     *  Definition of the callback function used to fetch samples from the SDR
     */
    typedef std::function<int(cf_t* data[SRSRAN_MAX_CHANNELS], uint32_t nsamples, srsran_timestamp_t* rx_time)> get_samples_t;

    /**
     *  Default constructor.
     *
     *  @param cfg Config singleton reference
     *  @param cb  Sample recv callback
     *  @param cs_nof_prb  Nr of PRBs to use during cell search
     *  @param override_nof_prb  If set, overrides the nof PRB received in the MIB
     */
    Phy(const libconfig::Config& cfg, get_samples_t cb, uint8_t cs_nof_prb, int8_t override_nof_prb, uint8_t rx_channels);
    
    /**
     *  Default destructor.
     */
    virtual ~Phy();
    
    /**
     *  Initialize the underlying components.
     */
    bool init();

    /**
     *  Search for a cell
     *
     *  Returns true if a cell has been found and the MIB could be decoded, false otherwise.
     */
    bool cell_search();

    /**
     *  Synchronizes PSS/SSS and tries to deocode the MIB.
     *
     *  Returns true on success, false otherwise.
     */
    bool synchronize_subframe();

    /**
     * Get the sample data for the next subframe.
     */
    bool get_next_frame(cf_t** buffer, uint32_t size);

    /**
     * Get the current cell (with params adjusted for MBSFN)
     */
    srsran_cell_t cell() { 
      return _cell;
    }

    /**
     * Get the current number of PRB.
     */
    unsigned nr_prb() { return _cell.nof_prb; }

    /**
     * Get the current subframe TTI
     */
    uint32_t tti() { return _tti; }
    
    /**
     * Get the current CFO value
     */
    float cfo() { return srsran_ue_sync_get_cfo(&_ue_sync);}

    /**
     * Set the CFO value from channel estimation
     */
    void set_cfo_from_channel_estimation(float cfo) { srsran_ue_sync_set_cfo_ref(&_ue_sync, cfo); }

    /**
     * Set the values received in SIB13
     */
    void set_mch_scheduling_info(const srsran::sib13_t& sib13);

    /**
     * Set MBSFN configuration values
     */
    void set_mbsfn_config(const srsran::mcch_msg_t& mcch);

    /**
     * Clear configuration values
     */
    void reset() { _mcch_configured = _mch_configured = false; }

    /**
     * Return true if MCCH has been configured
     */
    bool mcch_configured() { return _mcch_configured; }

    /**
     * Returns the current MBSFN area ID
     */
    uint8_t mbsfn_area_id() { return _sib13.mbsfn_area_info_list[0].mbsfn_area_id; }

    /**
     * Returns the MBSFN configuration (MCS, etc) for the subframe with the passed TTI
     */
    srsran_mbsfn_cfg_t mbsfn_config_for_tti(uint32_t tti, unsigned& area);

    /**
     * Enable MCCH decoding
     */
    void set_decode_mcch(bool d) { _decode_mcch = d; }

    /**
     * Get number of PRB in MBSFN/PMCH
     */
    uint8_t nof_mbsfn_prb() { return _cell.mbsfn_prb; }

    /**
     * Override number of PRB in MBSFN/PMCH
     */
    void set_nof_mbsfn_prb(uint8_t prb) { _cell.mbsfn_prb = prb; }

    void set_cell();

    bool is_cas_subframe(unsigned tti);
    bool is_mbsfn_subframe(unsigned tti);

    typedef struct {
      std::string tmgi;
      std::string dest;
      int lcid;
    } mtch_info_t;
    typedef struct {
      int mcs;
      std::vector< mtch_info_t > mtchs;
    } mch_info_t;

    const std::vector< mch_info_t>& mch_info() { return _mch_info;  }

    void set_dest_for_lcid(uint32_t mch_idx, int lcid, std::string dest) { _dests[mch_idx][lcid] = dest; }

    enum class SubcarrierSpacing {
      df_15kHz,
      df_7kHz5,
      df_2kHz5,
      df_1kHz25,
      df_0kHz37
    };

    SubcarrierSpacing mbsfn_subcarrier_spacing() {
      if (_cell.mbms_dedicated) {
        switch (_sib13.mbsfn_area_info_list[0].subcarrier_spacing) {
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_0dot37: return SubcarrierSpacing::df_0kHz37;
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_1dot25: return SubcarrierSpacing::df_1kHz25;
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_2dot5: return SubcarrierSpacing::df_2kHz5;
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_7dot5: return SubcarrierSpacing::df_7kHz5;
          default: return SubcarrierSpacing::df_15kHz;
        }
      } else {
          return SubcarrierSpacing::df_15kHz;
      }
    }

    float mbsfn_subcarrier_spacing_khz() {
      if (_cell.mbms_dedicated) {
        switch (_sib13.mbsfn_area_info_list[0].subcarrier_spacing) {
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_0dot37: return 0.37;
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_1dot25: return 1.25;
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_2dot5: return 2.5;
          case srsran::mbsfn_area_info_t::subcarrier_spacing_t::khz_7dot5: return 7.5;
          default: return 15;
        }
      } else {
          return 15;
      }
    }

    srsran::mcch_msg_t& mcch() { return _mcch; }

    int _mcs = 0;

    get_samples_t _sample_cb;
 private:
    srsran_ue_sync_t _ue_sync = {};
    srsran_ue_cellsearch_t _cell_search = {};
    srsran_ue_mib_sync_t  _mib_sync = {};
    srsran_ue_mib_t  _mib = {};
    srsran_cell_t _cell = {};

    bool _decode_mcch = false;

    cf_t* _mib_buffer[SRSRAN_MAX_CHANNELS] = {};
    uint32_t _buffer_max_samples = 0;
    uint32_t _tti = 0;

    uint8_t  _mcch_table[10] = {};
    bool _mcch_configured = false;
    srsran::sib13_t _sib13 = {};
    srsran::mcch_msg_t _mcch = {};

    bool _mch_configured = false;

    uint8_t _cs_nof_prb;

    std::vector< mch_info_t > _mch_info;

    std::map< uint32_t, std::map< int, std::string >> _dests;

    int8_t _override_nof_prb;
    uint8_t _rx_channels;
    bool _search_extended_cp = true;

    bool _has_pbch_repetition_r16 = false;
};
