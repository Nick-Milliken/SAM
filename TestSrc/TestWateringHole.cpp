
#define DEBUG

#define BOOST_TEST_MAIN TestWateringHole
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <sam/GraphStore.hpp>
#include <sam/tuples/VastNetflowGenerators.hpp>
#include <sam/tuples/Edge.hpp>
#include <sam/tuples/Tuplizer.hpp>
#include <sam/ZeroMQPushPull.hpp>
#include <sam/TopK.hpp>


using namespace sam;
using namespace sam::vast_netflow;
using namespace std::chrono;


typedef VastNetflow TupleType;
typedef EmptyLabel LabelType;
typedef Edge<size_t, EmptyLabel, TupleType> EdgeType;
typedef TuplizerFunction<EdgeType, MakeVastNetflow> Tuplizer;
typedef GraphStore<EdgeType, Tuplizer, SourceIp, DestIp,
                   TimeSeconds, DurationSeconds,
                   StringHashFunction, StringHashFunction,
                   StringEqualityFunction, StringEqualityFunction>
        GraphStoreType;

typedef GraphStoreType::QueryType SubgraphQueryType;
typedef GraphStoreType::ResultType ResultType;

typedef TupleStringHashFunction<TupleType, SourceIp> SourceHash;
typedef TupleStringHashFunction<TupleType, DestIp> TargetHash;
typedef ZeroMQPushPull<EdgeType, Tuplizer, SourceHash, TargetHash>
        PartitionType;


BOOST_AUTO_TEST_CASE( test_watering_hole )
{
  size_t numClients = 1000;
  size_t numServers = 5;
  size_t numNetflows = 100;

  WateringHoleGenerator generator(numClients, numServers);

  /////////////// Setting up Partition object ///////////////////////////
  size_t numNodes = 1;
  size_t nodeId0 = 0;
  std::vector<std::string> hostnames;
  hostnames.push_back("localhost");
  size_t startingPort = 10000;
  size_t timeout = 1000;
  size_t hwm = 1000;
  size_t queueLength = 1;
  
  auto pushPull = std::make_shared<PartitionType>(queueLength, 
                                                  numNodes, nodeId0,
                                                  hostnames, 
                                                  startingPort, timeout, true,
                                                  hwm);


  ////////////////// Setting up topk operator /////////////////////
  size_t capacity = 100000;
  auto featureMap = std::make_shared<FeatureMap>(capacity);
  size_t N = 1000; ///> The total number of elements in a sliding window
  size_t b = 100; ///> The number of elements in a dormant or active window
  size_t k = numServers; ///> The number of elements to keep track of
  std::string topkId = "topk";
  auto topk = std::make_shared<
    TopK<EdgeType, DestIp>>(N, b, k, nodeId0, featureMap, topkId);

  pushPull->registerConsumer(topk);

  /////////////// Setting up GraphStore /////////////////////////////////
  size_t graphCapacity = 100000; //For csc and csr
  size_t tableCapacity = 100000; //For intermediate results
  size_t resultsCapacity = 1000; //For final results
  double timeWindow = 10000;
  size_t numPushSockets = 1;
  size_t numPullThreads = 1;

  auto graphStore = std::make_shared<GraphStoreType>( numNodes, nodeId0,
                                             hostnames, startingPort,
                                             hwm, graphCapacity,
                                             tableCapacity, resultsCapacity,
                                             numPushSockets, numPullThreads,
                                             timeout, timeWindow,
                                             featureMap, true);

  pushPull->registerConsumer(graphStore);

  /////////////// The Watering Hole query ///////////////////////////////
  std::string e0 = "e0";
  std::string e1 = "e1";
  std::string bait = "bait";
  std::string target = "target";
  std::string controller = "controller";

  // Set up the query
  EdgeFunction starttimeFunction = EdgeFunction::StartTime;
  EdgeFunction endtimeFunction = EdgeFunction::EndTime;
  EdgeOperator greaterEdgeOperator = EdgeOperator::GreaterThan;
  EdgeOperator lessEdgeOperator = EdgeOperator::LessThan;
  EdgeOperator equalEdgeOperator = EdgeOperator::Assignment;

  EdgeExpression target2Bait(target, e0, bait);
  EdgeExpression target2Controller(target, e1, controller);
  TimeEdgeExpression endE0Second(endtimeFunction, e0,
                                 equalEdgeOperator, 0);
  TimeEdgeExpression startE1First(starttimeFunction, e1,
                                  greaterEdgeOperator, 0);
  TimeEdgeExpression startE1Second(starttimeFunction, e1,
                                  lessEdgeOperator, 10);

  //bait in Top1000
  VertexConstraintExpression baitTopK(bait, VertexOperator::In, topkId);
  
  //controller not in Top1000
  VertexConstraintExpression controllerNotTopK(controller, 
                                               VertexOperator::NotIn, topkId);

  auto query = std::make_shared<SubgraphQueryType>(featureMap);
  query->addExpression(target2Bait);
  query->addExpression(target2Controller);
  query->addExpression(endE0Second);
  query->addExpression(startE1First);
  query->addExpression(startE1Second);
  query->addExpression(baitTopK);
  query->addExpression(controllerNotTopK);
  query->finalize();

  graphStore->registerQuery(query);

  double time = 0.0;
  double increment = 0.01;

  size_t numBadMessages = 5;
  size_t totalNumMessages = 0;
 
  auto starttime = std::chrono::high_resolution_clock::now();
  Tuplizer tuplizer;
  // Sending benign message
  for (size_t i = 0; i < numNetflows; i++) 
  {
    auto currenttime = std::chrono::high_resolution_clock::now();
    duration<double> diff = duration_cast<duration<double>>(
      currenttime - starttime);
    if (diff.count() < totalNumMessages * increment) {
      size_t numMilliseconds = 
        (totalNumMessages * increment - diff.count()) * 1000;
      std::this_thread::sleep_for(
        std::chrono::milliseconds(numMilliseconds));
    }

    std::string str = generator.generate(time);
    EdgeType edge = tuplizer(totalNumMessages++, str);
    printf("Netflow %s\n", str.c_str()); 
    time += increment;
    pushPull->consume(edge);
  }

  // Create the infection message.
  std::string str = generator.generateInfection(time);
  EdgeType edge = tuplizer(totalNumMessages++, str);
  time += increment;
  pushPull->consume(edge);

  // Sending some benign messages so that the infection message completes
  // (the pattern is that the malicious messages begin after the end
  // of the infection message).  The duration of each message is one second,
  // so we create enough messages to fill one second.
  for (size_t i = 0; i < 1/increment + 1; i++) 
  {
    auto currenttime = std::chrono::high_resolution_clock::now();
    duration<double> diff = duration_cast<duration<double>>(
      currenttime - starttime);
    if (diff.count() < totalNumMessages * increment) {
      size_t numMilliseconds = 
          (totalNumMessages * increment - diff.count()) * 1000;
      std::this_thread::sleep_for(
        std::chrono::milliseconds(numMilliseconds));
    }

    std::string str = generator.generate(time);
    printf("Netflow %s\n", str.c_str()); 
    time += increment;
    EdgeType edge = tuplizer(totalNumMessages++, str);
    pushPull->consume(edge);
  }
 
  graphStore->clearResults(); 
  
  // Sending malicious messages
  for (size_t i = 0; i < numBadMessages; i++)
  {
    auto currenttime = std::chrono::high_resolution_clock::now();
    duration<double> diff = duration_cast<duration<double>>(
      currenttime - starttime);
    if (diff.count() < totalNumMessages * increment) {
      size_t numMilliseconds = 
        (totalNumMessages * increment - diff.count()) * 1000;
      std::this_thread::sleep_for(
        std::chrono::milliseconds(numMilliseconds));
    }

    std::string str = generator.generateControlMessage(time);
    printf("Netflow %s\n", str.c_str()); 
    time += increment;
    EdgeType edge = tuplizer(totalNumMessages++, str);
    pushPull->consume(edge);
  }

  // Sending benign message
  for (size_t i = 0; i < numNetflows; i++) 
       
  {
    auto currenttime = std::chrono::high_resolution_clock::now();
    duration<double> diff = duration_cast<duration<double>>(
      currenttime - starttime);
    if (diff.count() < totalNumMessages * increment) {
      size_t numMilliseconds = 
          (totalNumMessages * increment - diff.count()) * 1000;
      std::this_thread::sleep_for(
        std::chrono::milliseconds(numMilliseconds));
    }

    std::string str = generator.generate(time);
    printf("Netflow %s\n", str.c_str()); 
    time += increment;
    EdgeType edge = tuplizer(totalNumMessages++, str);
    pushPull->consume(edge);
  }
 
  pushPull->terminate();

  BOOST_CHECK_EQUAL(graphStore->getNumResults(), numBadMessages);
  printf("The End\n");
}
