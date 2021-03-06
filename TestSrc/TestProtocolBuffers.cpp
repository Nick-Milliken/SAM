#define BOOST_TEST_MAIN TestProtocolBuffers
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>
#include "proto/NetflowEdgeRequest.pb.h"

using namespace sam;

BOOST_AUTO_TEST_CASE( test_netflow_edge_request )
{
  NetflowEdgeRequest request;
  request.set_sourceip("192.168.0.1");

  std::string str;
  bool b = request.SerializeToString(&str);
  BOOST_CHECK(b);

  NetflowEdgeRequest request2;
  b = request2.ParseFromString(str);
  BOOST_CHECK(b);

  std::string str2;
  b = request.SerializeToString(&str2);
  BOOST_CHECK(b);


  BOOST_CHECK_EQUAL(str.compare(str2), 0);
}

