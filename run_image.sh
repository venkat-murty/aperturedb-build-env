#!/bin/bash

IMAGE_NAME=aperturedb22

docker stop ${IMAGE_NAME} 
docker rm ${IMAGE_NAME}

cat Dockerfile.dependencies Dockerfile.develop Dockerfile.extra > Dockerfile

# Build image
docker build --tag ${IMAGE_NAME} .

docker run \
  --detach \
  --name ${IMAGE_NAME} \
  --publish 2222:22 \
  ${IMAGE_NAME}


