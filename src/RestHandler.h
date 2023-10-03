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
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <libconfig.h++>

#include "SdrReader.h"
#include "Phy.h"

#include "cpprest/json.h"
#include "cpprest/http_listener.h"
#include "cpprest/uri.h"
#include "cpprest/asyncrt_utils.h"
#include "cpprest/filestream.h"
#include "cpprest/containerstream.h"
#include "cpprest/producerconsumerstream.h"

const int CINR_RAVG_CNT = 100;
typedef enum { searching, syncing, processing } state_t;

/**
 *  The RESTful API handler. Supports GET and PUT verbs for SDR parameters, and GET for reception info
 */
class RestHandler {
  public:
    /**
     *  Definition of the callback for setting new reception parameters
     */
    typedef std::function<void(const std::string& antenna, unsigned fcen, double gain, unsigned sample_rate, unsigned bandwidth)> set_params_t;

    /**
     *  Default constructor.
     *
     *  @param cfg Config singleton reference
     *  @param url URL to open the server on
     *  @param state Reference to the main loop sate
     *  @param sdr Reference to the SDR reader
     *  @param set_params Set parameters callback
     */
    RestHandler(const libconfig::Config& cfg, const std::string& url, state_t& state,
        SdrReader& sdr, Phy& phy, set_params_t set_params);
    /**
     *  Default destructor.
     */
    virtual ~RestHandler();

    /**
     *  RX Info pertaining to an SCH (MCCH/MCH or PDSCH)
     */
    class ChannelInfo {
      public:
        void SetData( std::vector<uint8_t> data) {
          std::lock_guard<std::mutex> lock(_data_mutex);
          _data = data;
        };
        std::vector<uint8_t> GetData() { 
          std::lock_guard<std::mutex> lock(_data_mutex);
          return _data; 
        };
        bool present = false;
        int mcs = 0;
        double ber;
        unsigned total = 1;
        unsigned errors = 0;
      private:
        std::vector<uint8_t> _data = {};
        std::mutex _data_mutex;
    };

    /**
     *  Time domain subcarrier CE values
     */
    std::vector<uint8_t> _ce_values = {};

    /**
     *  RX info for PDSCH
     */
    ChannelInfo _pdsch;

    /**
     *  RX info for MCCH
     */
    ChannelInfo _mcch;

    /**
     *  RX info for MCHs
     */
    std::map<uint32_t, ChannelInfo> _mch;

    /**
     *  Current CINR value
     */
    float cinr_db() { return _cinr_db.size() ? (std::accumulate(_cinr_db.begin(), _cinr_db.end(), 0) / (_cinr_db.size() * 1.0)) : 0.0; };
    void add_cinr_value( float cinr);

  private:
    std::vector<float>  _cinr_db;
    void get(web::http::http_request message);
    void put(web::http::http_request message);

    std::unique_ptr<web::http::experimental::listener::http_listener> _listener;

    state_t& _state;
    SdrReader& _sdr;
    Phy& _phy;

    set_params_t _set_params;

    bool _require_bearer_token = false;
    std::string _api_key;
};

