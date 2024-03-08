# Setup self-hosted GitHub runners

Instructions to set up self-hosted GitHub runners.

- Setup the desired machine.
- Create the runner by following the instructions in `Settings > Actions > Runners > New self-hosted runner`.
- Run the setup script according to your OS from this directory.
- (Optional - Linux) run `svc.sh` to make the runner work with `systemd`: `sudo ./svc.sh install && sudo ./svc.sh start`
