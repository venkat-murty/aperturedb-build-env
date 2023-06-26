

#include "VDMSClient.h"
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdlib.h>

using namespace VDMS;
using namespace std::chrono;
using json = nlohmann::json;

const json nullValue(nlohmann::json::value_t::null);
const json arrayValue(nlohmann::json::value_t::array);
const json objectValue(nlohmann::json::value_t::object);

struct T {
  const char *name;
  decltype(high_resolution_clock::now()) start;

  T(const char *s) {
    name = s;
    start = high_resolution_clock::now();
  }
  unsigned duration() const {
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    return (duration.count() / 1000);
  }
};

std::tuple<std::string, unsigned> exec(VDMSClient *client, std::string command,
                                       std::vector<std::string *> blobs) {
  T t("exec");
  auto r = client->query(command, std::move(blobs));
  auto m = t.duration();
  return std::make_tuple(std::move(r.json), m);
}

int main(int argc, char *argv[]) {
  std::shared_ptr<VDMSClient> conn =
      std::make_shared<VDMSClient>("admin", "admin", VDMSClientConfig{});

  json init(arrayValue);
  {
    json index(objectValue);
    index["CreateIndex"] = objectValue;
    index["CreateIndex"]["index_type"] = "entity";
    index["CreateIndex"]["class"] = "_Image";
    index["CreateIndex"]["property_key"] = "line_number";

    init.push_back(index);
  }


  auto response = json::parse(conn->query(init.dump()).json);
  std::cerr << response.dump(4) << std::endl;

  for (unsigned i = 0; i < 100; ++i) {
    int start = i * 1000000;
    int end  =  start + 1000000;
    json request(arrayValue);
    {
      json req(objectValue);
      req["FindImage"] = objectValue;
      req["FindImage"]["blobs"] = false;
      req["FindImage"]["constraints"] = objectValue;
      req["FindImage"]["constraints"]["line_number"] = arrayValue;
      req["FindImage"]["constraints"]["line_number"].push_back(">=");
      req["FindImage"]["constraints"]["line_number"].push_back(start);
      req["FindImage"]["constraints"]["line_number"].push_back("<=");
      req["FindImage"]["constraints"]["line_number"].push_back(end);
      req["FindImage"]["results"] = objectValue;
      req["FindImage"]["results"]["count"] = true;

      request.push_back (std::move(req));
    }
    auto [r, timing ] = exec (conn.get(), request.dump(), {});
    int count = json::parse(r)[0]["FindImage"]["count"].get<int>();
    std::cerr << "Timing " <<  timing << " ms" << " for " << count << " in range [ " << start << ", " << end << "] at rate " << (count / timing) << std::endl;
  }
  return 0;
}

