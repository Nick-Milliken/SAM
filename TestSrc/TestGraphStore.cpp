#define BOOST_TEST_MAIN TestGraphStore

//#define DEBUG

#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#include "GraphStore.hpp"
#include "NetflowGenerators.hpp"
#include <zmq.hpp>

using namespace sam;

zmq::context_t context(1);

typedef GraphStore<Netflow, NetflowTuplizer, SourceIp, DestIp, 
                   TimeSeconds, DurationSeconds, 
                   StringHashFunction, StringHashFunction, 
                   StringEqualityFunction, StringEqualityFunction>
        GraphStoreType;

typedef GraphStoreType::EdgeRequestType EdgeRequestType;

typedef GraphStoreType::QueryType SubgraphQueryType;


BOOST_AUTO_TEST_CASE( test_graph_store )
{
  /// In this test we create a graphstore on two nodes (both local addresses).
  ///
  size_t numNodes = 2;
  size_t nodeId0 = 0;
  size_t nodeId1 = 1;
  std::vector<std::string> requestHostnames;
  std::vector<size_t> requestPorts;
  std::vector<std::string> edgeHostnames;
  std::vector<size_t> edgePorts;
  size_t hwm = 1000;
  size_t graphCapacity = 1000; //For csc and csr
  size_t tableCapacity = 1000; //For SubgraphQueryResultMap intermediate results
  size_t resultsCapacity = 1000; //For final results
  double timeWindow = 100;

  requestHostnames.push_back("localhost");
  requestPorts.push_back(10000);
  requestHostnames.push_back("localhost");
  requestPorts.push_back(10001);
  edgeHostnames.push_back("localhost");
  edgePorts.push_back(10002);
  edgeHostnames.push_back("localhost");
  edgePorts.push_back(10003);

  int n = 1000;

  size_t numThreads = 1;

  GraphStoreType* graphStore0 = new GraphStoreType(context, numNodes, nodeId0, 
                        requestHostnames, requestPorts,
                        edgeHostnames, edgePorts,
                        hwm, graphCapacity, 
                        tableCapacity, resultsCapacity, timeWindow, numThreads); 

  // One thread runs this.
  auto graph_function0 = [graphStore0, n]()
  {
    AbstractNetflowGenerator *generator0 = 
      new UniformDestPort("192.168.0.0", 1);
    
    for (int i = 0; i < n; i++) {
      std::string str = generator0->generate();
      Netflow n = makeNetflow(0, str);
      graphStore0->consume(n);
    }
    graphStore0->terminate();

    
    delete generator0;
  };

  GraphStoreType* graphStore1 = new GraphStoreType(context,
                        numNodes, nodeId1, 
                        requestHostnames, requestPorts,
                        edgeHostnames, edgePorts,
                        hwm, graphCapacity, 
                        tableCapacity, resultsCapacity, timeWindow, numThreads); 

  // Another thread runs this.
  auto graph_function1 = [graphStore1, n]()
                          
  {
    AbstractNetflowGenerator *generator1 = 
      new UniformDestPort("192.168.0.1", 1);
    
    for (int i = 0; i < n; i++) {
      std::string str = generator1->generate();
      Netflow n = makeNetflow(0, str);
      graphStore1->consume(n);
    }
    graphStore1->terminate();

    
    delete generator1;
  };

  std::thread thread0(graph_function0);
  std::thread thread1(graph_function1);

  thread0.join();
  thread1.join();


  // There is no query that forces communication, so the number of received
  // tuples over zeromq should be zero.
  BOOST_CHECK_EQUAL(graphStore0->getTotalEdgePulls(), 0);
  BOOST_CHECK_EQUAL(graphStore1->getTotalEdgePulls(), 0);

  delete graphStore0;
  delete graphStore1;
}

struct SingleNodeFixture  {

  size_t numNodes = 1;
  size_t nodeId0 = 0;
  size_t hwm = 1000;
  size_t graphCapacity = 1000; //For csc and csr
  size_t tableCapacity = 1000; //For SubgraphQueryResultMap results
  size_t resultsCapacity = 1000; //For final results
  double timeWindow = 100;

  std::vector<std::string> requestHostnames;
  std::vector<size_t> requestPorts;
  std::vector<std::string> edgeHostnames;
  std::vector<size_t> edgePorts;

  EdgeFunction starttimeFunction = EdgeFunction::StartTime;
  EdgeFunction endtimeFunction = EdgeFunction::EndTime;
  EdgeOperator equal_edge_operator = EdgeOperator::Assignment;
  EdgeOperator greater_edge_operator = EdgeOperator::GreaterThan;
  std::string e1 = "e1";
  std::string e2 = "e2";
  std::string nodex = "nodex";
  std::string nodey = "nodey";
  std::string nodez = "nodez";

  EdgeExpression* y2x;
  EdgeExpression* z2x;
  TimeEdgeExpression* startY2Xboth;
  TimeEdgeExpression* startZ2Xbeg;

  AbstractNetflowGenerator* generator;

  GraphStoreType* graphStore0;

  SingleNodeFixture () {
    y2x = new EdgeExpression(nodey, e1, nodex);
    z2x = new EdgeExpression(nodez, e2, nodex);
    startY2Xboth = new TimeEdgeExpression(starttimeFunction,
                                                  e1, equal_edge_operator, 0);
    startZ2Xbeg = new TimeEdgeExpression(starttimeFunction,
                                                 e2, greater_edge_operator, 0); 
    generator = new UniformDestPort("192.168.0.2", 1);


    requestHostnames.push_back("localhost");
    requestPorts.push_back(10000);
    edgeHostnames.push_back("localhost");
    edgePorts.push_back(10002);

    size_t numThreads = 1;

    graphStore0 = new GraphStoreType(context, numNodes, nodeId0, 
                        requestHostnames, requestPorts,
                        edgeHostnames, edgePorts,
                        hwm, graphCapacity, 
                        tableCapacity, resultsCapacity, timeWindow, numThreads); 
  }

  ~SingleNodeFixture() {
    delete y2x;
    delete z2x;
    delete startY2Xboth;
    delete startZ2Xbeg;
    delete generator;
    delete graphStore0;
  }
};

///
/// In this test the query is simply an edge such that every edge
/// matches.
///
BOOST_FIXTURE_TEST_CASE( test_single_edge_match, SingleNodeFixture )
{
  SubgraphQueryType query;

  query.addExpression(*startY2Xboth);
  query.addExpression(*y2x);

  // Should complain that the query hasn't been finalized
  BOOST_CHECK_THROW(graphStore0->registerQuery(query), GraphStoreException);

  query.finalize();

  graphStore0->registerQuery(query);

  size_t n = 1000;
  for(size_t i = 0; i < n; i++) 
  {
    std::string str = generator->generate();
    Netflow netflow = makeNetflow(i, str);
    graphStore0->consume(netflow);
  }

  graphStore0->terminate();

  BOOST_CHECK_EQUAL(graphStore0->getNumResults(), n);
}

BOOST_FIXTURE_TEST_CASE( test_double_terminate, SingleNodeFixture )
{
  graphStore0->terminate();
  graphStore0->terminate();
}


///
/// In this test the query is simply an edge such but the time constraints
/// make so that nothing matches.
///
BOOST_FIXTURE_TEST_CASE( test_single_edge_no_match, SingleNodeFixture )
{
  SubgraphQuery<Netflow, TimeSeconds, DurationSeconds> query;

  TimeEdgeExpression endTimeExpressionE1(endtimeFunction, e1, 
                                         equal_edge_operator, 0);
                                         
 
  query.addExpression(*startY2Xboth);
  query.addExpression(endTimeExpressionE1);
  query.addExpression(*y2x);
  query.finalize();

  graphStore0->registerQuery(query);

  size_t n = 10000;
  for(size_t i = 0; i < n; i++) 
  {
    std::string str = generator->generate();
    Netflow netflow = makeNetflow(i, str);
    graphStore0->consume(netflow);
  }

  BOOST_CHECK_EQUAL(graphStore0->getNumResults(), 0);
 
}


///
/// In this test the query is two connected edges.
///
BOOST_FIXTURE_TEST_CASE( test_double_edge_match, SingleNodeFixture )
{
  SubgraphQuery<Netflow, TimeSeconds, DurationSeconds> query;

  TimeEdgeExpression startTimeExpressionE2(starttimeFunction, e2,
                                           greater_edge_operator, 0);
 

  query.addExpression(*startY2Xboth);
  query.addExpression(*startZ2Xbeg);
  query.addExpression(*y2x);
  query.addExpression(*z2x);
  query.finalize();

  graphStore0->registerQuery(query);

  size_t numExtra = 1000;

  size_t n = 3;
  for(size_t i = 0; i < n; i++) 
  {
    std::string str = generator->generate();
    Netflow netflow = makeNetflow(i, str);
    graphStore0->consume(netflow);
  }
  graphStore0->terminate();

  BOOST_CHECK_EQUAL(graphStore0->getNumResults(), (n-1)*(n)/2);
 
}

///
/// This tests where two of the edges in the triangle have the same 
/// time.  We are assuming strictly increasing time for the edges, but
/// TODO that should be specifiable.
BOOST_FIXTURE_TEST_CASE( test_triangle_same_time, SingleNodeFixture )
{
  // Set up the triangle query
  double queryTimeWindow = 10;
  //SubgraphQueryType query;
  
  EdgeFunction starttimeFunction = EdgeFunction::StartTime;
  EdgeFunction endtimeFunction = EdgeFunction::EndTime;
  EdgeOperator equal_edge_operator = EdgeOperator::Assignment;
  EdgeOperator greater_edge_operator = EdgeOperator::GreaterThan;
  EdgeOperator less_edge_operator = EdgeOperator::LessThan;

  std::string e0 = "e0";
  std::string e1 = "e1";
  std::string e2 = "e2";
  std::string nodex = "nodex";
  std::string nodey = "nodey";
  std::string nodez = "nodez";

  EdgeExpression x2y(nodex, e0, nodey);
  EdgeExpression y2z(nodey, e1, nodez);
  EdgeExpression z2x(nodez, e2, nodex);
  TimeEdgeExpression startE0First(starttimeFunction, e0,
                                  equal_edge_operator, 0);
  TimeEdgeExpression startE1First(starttimeFunction,e1,
                                  greater_edge_operator, 0);
  TimeEdgeExpression startE2First(starttimeFunction,e2,
                                  greater_edge_operator, 0);
  TimeEdgeExpression startE0Second(starttimeFunction, e0,
                                   less_edge_operator,queryTimeWindow);
  TimeEdgeExpression startE1Second(starttimeFunction,e1,
                                   less_edge_operator,queryTimeWindow);
  TimeEdgeExpression startE2Second(starttimeFunction,e2,
                                   less_edge_operator, queryTimeWindow);
  TimeEdgeExpression endE0Second(endtimeFunction, e0,
                                   less_edge_operator,queryTimeWindow);
  TimeEdgeExpression endE1Second(endtimeFunction,e1,
                                   less_edge_operator,queryTimeWindow);
  TimeEdgeExpression endE2Second(endtimeFunction,e2,
                                   less_edge_operator, queryTimeWindow);

  SubgraphQueryType query;
  query.addExpression(x2y);
  query.addExpression(y2z);
  query.addExpression(z2x);
  query.addExpression(startE0First);
  query.addExpression(startE1First);
  query.addExpression(startE2First);
  query.addExpression(startE0Second);
  query.addExpression(startE1Second);
  query.addExpression(startE2Second);
  query.addExpression(endE0Second);
  query.addExpression(endE1Second);
  query.addExpression(endE2Second);
  query.finalize();

  graphStore0->registerQuery(query);

  std::string s1 = "0.47000000000000025,parseDate,dateTimeStr,ipLayerProtocol,"
    "ipLayerProtocolCode,node1,node2,51482,40020,1,1,1,1,1,1,1,1,1,1";
  std::string s2 = "0.52000000000000024,parseDate,dateTimeStr,ipLayerProtocol,"
    "ipLayerProtocolCode,node2,node3,51482,40020,1,1,1,1,1,1,1,1,1,1";
  std::string s3 = "0.52000000000000024,parseDate,dateTimeStr,ipLayerProtocol,"
    "ipLayerProtocolCode,node3,node1,51482,40020,1,1,1,1,1,1,1,1,1,1";
  std::string s4 = "0.47000000000000025,parseDate,dateTimeStr,ipLayerProtocol,"
    "ipLayerProtocolCode,node4,node5,51482,40020,1,1,1,1,1,1,1,1,1,1";
  std::string s5 = "0.47000000000000025,parseDate,dateTimeStr,ipLayerProtocol,"
    "ipLayerProtocolCode,node5,node6,51482,40020,1,1,1,1,1,1,1,1,1,1";
  std::string s6 = "0.52000000000000024,parseDate,dateTimeStr,ipLayerProtocol,"
    "ipLayerProtocolCode,node6,node4,51482,40020,1,1,1,1,1,1,1,1,1,1";


  Netflow n1 = makeNetflow(0, s1);
  Netflow n2 = makeNetflow(1, s2);
  Netflow n3 = makeNetflow(2, s3);
  Netflow n4 = makeNetflow(0, s4);
  Netflow n5 = makeNetflow(1, s5);
  Netflow n6 = makeNetflow(2, s6);
  graphStore0->consume(n1);
  graphStore0->consume(n2);
  graphStore0->consume(n3);
  graphStore0->consume(n4);
  graphStore0->consume(n5);
  graphStore0->consume(n6);

  graphStore0->terminate();

  BOOST_CHECK_EQUAL(graphStore0->getNumResults(), 0);
}


