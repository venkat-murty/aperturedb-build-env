#!/bin/bash

for u in $(ls /tmp/users)
do
  adduser --home /home/$u --gecos "$u" -ingroup  users --shell /bin/bash -q --disabled-password $u
  touch /tmp/users/$u/vimrc
  touch /tmp/users/$u/bashrc
  cp /tmp/vimrc /home/$u/.vimrc
  cp /tmp/bashrc /home/$u/.bashrc
  cat /tmp/users/$u/vimrc >> /home/$u/.vimrc
  cat /tmp/users/$u/bashrc >> /home/$u/.bashrc
  adduser $u wheel
  echo "$u:$u" | chpasswd
  mkdir -p /home/$u/.ssh
  for k in $(ls /tmp/users/$u/*.pub)
  do
    cat $k >> /home/$u/.ssh/authorized_keys
  done
  chown -R $u:users /home/$u/.ssh
  chown -R $u:users /home/$u/.vimrc
  chown -R $u:users /home/$u/.bashrc
done

rm -rf /tmp/users
rm -f /tmp/vimrc
rm -f /tmp/bashrc
