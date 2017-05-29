#ifndef BASE_COMPUTATION_HPP
#define BASE_COMPUTATION_HPP

#include <vector>
#include <string>

#include "FeatureMap.hpp"


namespace sam
{

//template <size_t... keyFields>
class BaseComputation
{
protected:
  size_t metricInterval = 100000; ///> How often (per items) to print metrics
  size_t nodeId; ///> Used for debugging/metrics per node

  /// The variable name assigned to this operator.  This is specified
  /// in the query.
  std::string identifier;

  /// This is a reference to the map that stores the mapping from 
  /// key/featurename to feature.
  FeatureMap& featureMap;

public:
  BaseComputation(size_t nodeId,
                  FeatureMap& _featureMap, 
                  std::string identifier)
  : featureMap(_featureMap) 
  {
    this->nodeId = nodeId;
    this->identifier = identifier;
  }


};


}


#endif
