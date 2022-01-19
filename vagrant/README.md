developing v4l2loopback with a VM
=================================

Using `Vagrant` with the `VirtualBox` backend


# SETUP

Use the [`Vagrantfile`](#Vagrantfile) below for a Debian/testing based VM.
Replace the host's `/PATH/TO/v4l2loopback` to the full path to the v4l2loopback sources on your disk.

- The VM needs to have all the goodies (VirtualBox extension pack) for sharing host folders and webcams (if you need that).

      apt install virtualbox-guest-dkms virtualbox-guest-dkms virtualbox-guest-utils

- Make the VM up-to-date `apt update && apt dist-upgrade` or similar
- Drop the unused stuff with `apt autoremove; apt-get clean` or similar
- Make sure that the `vagrant` user will end up in `/vagrant/v4l2loopback` when logging in.
  I did so by adding the following 2 lines at the very end of `~vagrant/.bashrc`:

      cd /vagrant
      test -d v4l2loopback && cd v4l2loopback


- Power the VM off, and create an offline snapshot

- Boot the VM (with the share mounted onto `/vagrant/v4l2loopback`):

      vagrant up

- Create an online snapshot of the running VM
- Leave the VM running

# scripts

You can find a `vbox-restart` script in the `vagrant/` directory of this repository.
Running it (give the UUID of a running VM) will:
- do a hard shutdown of the given VM
- restore the last snapshot of the given VM
- start the running VM
- (optionally) attach the host's webcam to VM

# Workflow

- Open `v4l2loopback.c` in your favourite editor and hack away
- Whenever you feel like testing, do the following in a separate terminal:

~~~
me@host:~/v4l2loopback$ cd vagrant

me@host:~/v4l2loopback/vagrant$ ./vbox-restart -a
me@host:~/v4l2loopback/vagrant$ vagrant ssh
vagrant@/vagrant/v4l2loopback$ make clean
vagrant@/vagrant/v4l2loopback$ make modprobe
vagrant@/vagrant/v4l2loopback$ (do some tests)
~~~

if the machine freezes, or something else really bad happens to it, just
re-run `./vbox-restart` and start anew.

# Resources

## Vagrantfile

~~~vagrant
Vagrant.configure("2") do |config|
  config.vm.box = "debian/contrib-testing64"
  config.vm.synced_folder "/PATH/TO/v4l2loopback", "/vagrant/v4l2loopback"
end
~~~
