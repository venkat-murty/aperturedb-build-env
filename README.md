# aperturedb-build-env

Dockerfile.dependencies should be identical to athena/docker/dependencies/Dockerfile 
Dockerfile.python should have all the command to install python and is derived from athena/docker/testing/Dockerfile
Dockerfile.extra install ssh server, copies pub keys for login etc.

The final docker file is generated concatinating the individual docker files.
create users script creates users in the environment for login.
run image script will create the docker image / run the image with mounting some local files.
