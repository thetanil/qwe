# A root-owned, world-writable file: our content write succeeds (matches already,
# so it is not even attempted), but chmod on a file we do not own is EPERM.
sudo sh -c 'printf hi > owned-by-root.txt'
sudo chmod 666 owned-by-root.txt
