#!/bin/bash
./script/cmake-build simulation -DOT_PLATFORM=simulation -DOT_TCAT_COAPS=ON -DOT_BLE_TCAT=OFF \
  -DOT_TREL=OFF -DOT_JOINER=ON -DOT_SERVICE=ON -DOT_COAP=ON -DOT_FULL_LOGS=ON \
  $@
