#!/bin/bash

for u in $(ls /tmp/users)
do
  adduser --gecos ""  -q --home /home/$u --ingroup users --shell /bin/bash --disabled-password $u
  cp /tmp/vimrc /home/$u/.vimrc
  echo "$u:$u" | chpasswd
  mkdir -p /home/$u/.ssh
  for k in $(ls /tmp/users/$u/*.pub)
  do
    cat $k >> /home/$u/.ssh/authorized_keys
  done
  chown -R $u:users /home/$i/.ssh
  chown -R $u:users /home/$i/.vimrc
done

rm -rf /tmp/users
