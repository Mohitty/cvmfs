/**
 * This file is part of the CernVM File System.
 */

#ifndef CVMFS_SWISSKNIFE_PULL_H_
#define CVMFS_SWISSKNIFE_PULL_H_

#include "swissknife.h"

namespace swissknife {

class CommandPull : public Command {
 public:
  ~CommandPull() { };
  std::string GetName() { return "pull"; };
  std::string GetDescription() {
    return "Makes a Stratum 1 replica of a Stratum 0 repository.";
  };
  ParameterList GetParams() {
    ParameterList result;
    result.push_back(Parameter('u', "repository url", false, false));
    result.push_back(Parameter('m', "repository name", false, false));
    result.push_back(Parameter('r', "spooler definition", false, false));
    result.push_back(Parameter('k', "repository master key(s)", false, false));
    result.push_back(Parameter('x', "directory for temporary files",
                               false, false));
    result.push_back(Parameter('n', "number of download threads", true, false));
    result.push_back(Parameter('t', "timeout (s)", true, false));
    return result;
  }
  int Main(const ArgumentList &args);
};

}

#endif  // CVMFS_SWISSKNIFE_PULL_H_
