#!/bin/sh

#
# This script wraps the unit test run of CernVM-FS.
#

set -e

SCRIPT_LOCATION=$(cd "$(dirname "$0")"; pwd)
. ${SCRIPT_LOCATION}/common.sh

usage() {
  echo "Usage: $0 [-q only quick tests] [-s shrinkwrap test binary]\\"
  echo "          [-c cache plugin binary] [-g GeoAPI sources] \\"
  echo "          <unittests binary> <XML output location>"
  echo "This script runs the CernVM-FS unit tests"
  exit 1
}

CVMFS_UNITTESTS_QUICK=0
CVMFS_SHRINKWRAP_TEST_BINARY=
CVMFS_CACHE_PLUGIN=
CVMFS_GEOAPI_SOURCES=

while getopts "qc:g:s:l:" option; do
  case $option in
    q)
      CVMFS_UNITTESTS_QUICK=1
    ;;
    c)
      CVMFS_CACHE_PLUGIN=$OPTARG
    ;;
    g)
      CVMFS_GEOAPI_SOURCES=$OPTARG
    ;;
    s)
      CVMFS_SHRINKWRAP_TEST_BINARY=$OPTARG
    ;;
    l)
      # Preloading a library now unused
      :
    ;;
    ?)
      usage
    ;;
  esac
done

shift $(( $OPTIND - 1 ))
if [ $# -lt 2 ]; then
  usage
fi

CVMFS_UNITTESTS_BINARY=$1
CVMFS_UNITTESTS_RESULT_LOCATION=$2

# check if only a quick subset of the unittests should be run
test_filter='-'
if [ $CVMFS_UNITTESTS_QUICK = 1 ]; then
  echo "running only quick tests (without suffix 'Slow')"
  test_filter='-*Slow'
fi

# run the Python unittests for the GeoAPI
if [ "x$CVMFS_GEOAPI_SOURCES" != "x" ]; then
  # TODO(jblomer): XML output
  (cd $CVMFS_GEOAPI_SOURCES && python test_cvmfs_geo.py)
fi

# run the cache plugin unittests
if [ "x$CVMFS_CACHE_PLUGIN" != "x" ]; then
  CVMFS_CACHE_UNITTESTS="$(dirname $CVMFS_UNITTESTS_BINARY)/cvmfs_test_cache"
  CVMFS_CACHE_LOCATOR=tcp=127.0.0.1:4224
  CVMFS_CACHE_CONFIG=$(mktemp /tmp/cvmfs-unittests-XXXXX)
  echo "CVMFS_CACHE_PLUGIN_LOCATOR=$CVMFS_CACHE_LOCATOR" > $CVMFS_CACHE_CONFIG
  echo "CVMFS_CACHE_PLUGIN_SIZE=1000" >> $CVMFS_CACHE_CONFIG
  echo "CVMFS_CACHE_PLUGIN_TEST=yes" >> $CVMFS_CACHE_CONFIG
  for plugin in $(echo $CVMFS_CACHE_PLUGIN | tr : " "); do
    if [ -x $plugin ]; then
      echo "running unit tests for cache plugin $plugin"
      # All our plugins take a configuration file as a parameter
      plugin_pid="$($plugin $CVMFS_CACHE_CONFIG)"
      echo "cache plugin started as PID $plugin_pid"
      $CVMFS_CACHE_UNITTESTS $CVMFS_CACHE_LOCATOR \
        --gtest_output=xml:${CVMFS_UNITTESTS_RESULT_LOCATION}.$(basename $plugin)
      /bin/kill $plugin_pid
    else
      echo "Warning: plugin $plugin not found, skipping"
    fi
  done
  rm -f $CVMFS_CACHE_CONFIG
fi

# run the shrinkwrap tests
if [ "x$CVMFS_SHRINKWRAP_TEST_BINARY" != "x" ]; then
  echo "running shrinkwrap tests (with XML output $CVMFS_UNITTESTS_RESULT_LOCATION)..."
  $CVMFS_SHRINKWRAP_TEST_BINARY --gtest_shuffle                                     \
    --gtest_output=xml:${CVMFS_UNITTESTS_RESULT_LOCATION}.shrinkwrap \
    --gtest_filter=$test_filter
fi

# run the unit tests
echo "running unit tests (with XML output $CVMFS_UNITTESTS_RESULT_LOCATION)..."
$CVMFS_UNITTESTS_BINARY --gtest_shuffle                                     \
                        --gtest_output=xml:$CVMFS_UNITTESTS_RESULT_LOCATION \
                        --gtest_filter=$test_filter
