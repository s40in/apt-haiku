#!/bin/sh

cmake -DCMAKE_INSTALL_PREFIX=/boot/home/config/non-packaged -DSYSTEMD_LIBRARIES="-lroot -lnetwork" .