#!/bin/bash

cd $(dirname $0)

rm -f *.dll *.so

set -e

docker build -t wineasio .
docker run -v $PWD:/mnt --rm --entrypoint \
    cp wineasio:latest \
        /wineasio/build32/wineasio32.dll \
        /wineasio/build32/wineasio32.dll.so \
        /wineasio/build64/wineasio64.dll \
        /wineasio/build64/wineasio64.dll.so \
        /mnt/
