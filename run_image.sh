#!/bin/bash

IMAGE_NAME=aperturedb

docker stop ${IMAGE_NAME} 
docker rm ${IMAGE_NAME}

cat Dockerfile.base Dockerfile.dependencies Dockerfile.python Dockerfile.extra > Dockerfile

# Build image
docker build --tag ${IMAGE_NAME} .

docker run \
  --detach \
  --name ${IMAGE_NAME} \
  --publish 2222:22 \
  ${IMAGE_NAME}


