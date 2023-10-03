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

#include "RestHandler.h"

#include <memory>
#include <utility>

#include "spdlog/spdlog.h"

using web::json::value;
using web::http::methods;
using web::http::uri;
using web::http::http_request;
using web::http::status_codes;
using web::http::experimental::listener::http_listener;
using web::http::experimental::listener::http_listener_config;

RestHandler::RestHandler(const libconfig::Config& cfg, const std::string& url,
                         state_t& state, SdrReader& sdr, Phy& phy,
                         set_params_t set_params)
    : _state(state)
    , _sdr(sdr)
    , _phy(phy)
    , _set_params(std::move(set_params)) 
{

  http_listener_config server_config;
  if (url.rfind("https", 0) == 0) {
    server_config.set_ssl_context_callback(
        [&](boost::asio::ssl::context& ctx) {
          std::string cert_file = "/usr/share/5gmag-rt/cert.pem";
          cfg.lookupValue("modem.restful_api.cert", cert_file);

          std::string key_file = "/usr/share/5gmag-rt/key.pem";
          cfg.lookupValue("modem.restful_api.key", key_file);

          ctx.set_options(boost::asio::ssl::context::default_workarounds);
          ctx.use_certificate_chain_file(cert_file);
          ctx.use_private_key_file(key_file, boost::asio::ssl::context::pem);
        });
  }

  cfg.lookupValue("modem.restful_api.api_key.enabled", _require_bearer_token);
  if (_require_bearer_token) {
    _api_key = "106cd60-76c8-4c37-944c-df21aa690c1e";
    cfg.lookupValue("modem.restful_api.api_key.key", _api_key);
  }

  _listener = std::make_unique<http_listener>(
      url, server_config);

  _listener->support(methods::GET, std::bind(&RestHandler::get, this, std::placeholders::_1));  // NOLINT
  _listener->support(methods::PUT, std::bind(&RestHandler::put, this, std::placeholders::_1));  // NOLINT

  _listener->open().wait();
}

RestHandler::~RestHandler() = default;

void RestHandler::get(http_request message) {
  spdlog::debug("Received GET request {}", message.to_string() );
  auto paths = uri::split_path(uri::decode(message.relative_uri().path()));
  if (_require_bearer_token &&
    (message.headers()["Authorization"] != "Bearer " + _api_key)) {
    message.reply(status_codes::Unauthorized);
    return;
  }

  if (paths.empty()) {
    message.reply(status_codes::NotFound);
  } else {
    if (paths[0] == "status") {
      auto state = value::object();

      switch (_state) {
        case searching:
          state["state"] = value::string("searching");
          break;
        case syncing:
          state["state"] = value::string("syncing");
          break;
        case processing:
          state["state"] = value::string("synchronized");
          break;
      }

      if (_phy.cell().nof_prb == _phy.cell().mbsfn_prb) {
        state["nof_prb"] = value(_phy.cell().nof_prb);
      } else {
        state["nof_prb"] = value(_phy.cell().mbsfn_prb);
      }
      state["cell_id"] = value(_phy.cell().id);
      state["cfo"] = value(_phy.cfo());
      state["cinr_db"] = value(cinr_db());
      state["subcarrier_spacing"] = value(_phy.mbsfn_subcarrier_spacing_khz());
      message.reply(status_codes::OK, state);
    } else if (paths[0] == "sdr_params") {
      value sdr = value::object();
      sdr["frequency"] = value(_sdr.get_frequency());
      sdr["gain"] = value(_sdr.get_gain());
      sdr["min_gain"] = value(_sdr.min_gain());
      sdr["max_gain"] = value(_sdr.max_gain());
      sdr["filter_bw"] = value(_sdr.get_filter_bw());
      sdr["antenna"] = value(_sdr.get_antenna());
      sdr["sample_rate"] = value(_sdr.get_sample_rate());
      sdr["buffer_level"] = value(_sdr.get_buffer_level());
      message.reply(status_codes::OK, sdr);
    } else if (paths[0] == "ce_values") {
      auto cestream = Concurrency::streams::bytestream::open_istream(_ce_values);
      message.reply(status_codes::OK, cestream);
    } else if (paths[0] == "pdsch_status") {
      value sdr = value::object();
      sdr["bler"] = value(static_cast<float>(_pdsch.errors) /
                                static_cast<float>(_pdsch.total));
      sdr["ber"] = value(_pdsch.ber);
      sdr["mcs"] = value(_pdsch.mcs);
      sdr["present"] = 1;
      message.reply(status_codes::OK, sdr);
    } else if (paths[0] == "pdsch_data") {
      auto cestream = Concurrency::streams::bytestream::open_istream(_pdsch.GetData());
      message.reply(status_codes::OK, cestream);
    } else if (paths[0] == "mcch_status") {
      value sdr = value::object();
      sdr["bler"] = value(static_cast<float>(_mcch.errors) /
                                static_cast<float>(_mcch.total));
      sdr["ber"] = value(_mcch.ber);
      sdr["mcs"] = value(_mcch.mcs);
      sdr["present"] = 1;
      message.reply(status_codes::OK, sdr);
    } else if (paths[0] == "mcch_data") {
      auto cestream = Concurrency::streams::bytestream::open_istream(_mcch.GetData());
      message.reply(status_codes::OK, cestream);
    } else if (paths[0] == "mch_info") {
      std::vector<value> mi;
      auto mch_info = _phy.mch_info();
      std::for_each(std::begin(mch_info), std::end(mch_info), [&mi](Phy::mch_info_t const& mch) {
          value m;
          m["mcs"] = value(mch.mcs);
          std::vector<value> mti;
          std::for_each(std::begin(mch.mtchs), std::end(mch.mtchs), [&mti](Phy::mtch_info_t const& mtch) {
              value mt;
              mt["tmgi"] = value(mtch.tmgi);
              mt["dest"] = value(mtch.dest);
              mt["lcid"] = value(mtch.lcid);
              mti.push_back(mt);
          });
          m["mtchs"] = value::array(mti);
          mi.push_back(m);
      });
      message.reply(status_codes::OK, value::array(mi));
    } else if (paths[0] == "mch_status") {
      int idx = std::stoi(paths[1]);
      value sdr = value::object();
      sdr["bler"] = value(static_cast<float>(_mch[idx].errors) /
                                static_cast<float>(_mch[idx].total));
      sdr["ber"] = value(_mch[idx].ber);
      sdr["mcs"] = value(_mch[idx].mcs);
      sdr["present"] = value(_mch[idx].present);
      message.reply(status_codes::OK, sdr);
    } else if (paths[0] == "mch_data") {
      int idx = std::stoi(paths[1]);
      auto cestream = Concurrency::streams::bytestream::open_istream(_mch[idx].GetData());
      message.reply(status_codes::OK, cestream);
    } else if (paths[0] == "log") {
      std::string logfile = "/var/log/syslog";

      Concurrency::streams::file_stream<uint8_t>::open_istream(logfile).then(
          [message](const Concurrency::streams::basic_istream<unsigned char>&
                        file_stream) {
            message.reply(status_codes::OK, file_stream, "text/plain");
          });
    }
  }
}

void RestHandler::put(http_request message) {
  spdlog::debug("Received PUT request {}", message.to_string() );

  if (_require_bearer_token &&
    (message.headers()["Authorization"] != "Bearer " + _api_key)) {
    message.reply(status_codes::Unauthorized);
    return;
  }

  auto paths = uri::split_path(uri::decode(message.relative_uri().path()));
  if (paths.empty()) {
    message.reply(status_codes::NotFound);
  } else {
    if (paths[0] == "sdr_params") {
      value answer;

      auto f = _sdr.get_frequency();
      auto g = _sdr.get_gain();
      auto bw = _sdr.get_filter_bw();
      auto a = _sdr.get_antenna();
      auto sr = _sdr.get_sample_rate();

      const auto & jval = message.extract_json().get();
      spdlog::debug("Received JSON: {}", jval.serialize());

      if (jval.has_field("antenna")) {
        a = jval.at("antenna").as_string();
      }
      if (jval.has_field("frequency")) {
        f = jval.at("frequency").as_integer();
      }
      if (jval.has_field("gain")) {
        g = jval.at("gain").as_double();
      }
      _set_params( a, static_cast<unsigned int>(f), g, static_cast<unsigned int>(sr), bw);

      message.reply(status_codes::OK, answer);
    }
  }
}

void RestHandler::add_cinr_value( float cinr) {
  if (_cinr_db.size() > CINR_RAVG_CNT) {
    _cinr_db.erase(_cinr_db.begin());
  }
  _cinr_db.push_back(cinr);
}
