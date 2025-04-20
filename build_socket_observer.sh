#!/bin/bash
echo "Building with Socket Observer ENABLED"
cmake -G "Unix Makefiles" -DBUILD_INTERPOSITION=ON -DENABLE_SOCKET_OUTPUT=ON .. && make "$@"