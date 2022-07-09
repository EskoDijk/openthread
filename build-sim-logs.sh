#!/bin/bash
./script/cmake-build simulation -DOT_PLATFORM=simulation -DOT_OTNS=ON -DOT_SIMULATION_VIRTUAL_TIME=ON -DOT_SIMULATION_VIRTUAL_TIME_UART=ON \
 -DOT_SIMULATION_MAX_NETWORK_SIZE=999 -DOT_COMMISSIONER=ON -DOT_JOINER=ON -DOT_BORDER_ROUTER=ON -DOT_SERVICE=ON -DOT_COAP=ON \
 -DOT_FULL_LOGS=ON

