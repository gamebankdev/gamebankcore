Quickstart
----------

### Get current gamebankd
Please refer to the building document.

### Configure for your use case
Please refer to example configure files int `config-examples/*.ini`.

### Hard Disk usage

Please make sure that you have enough free disk space available.
Check `shared-file-size =` in your `config.ini` to reflect your needs.
Set it to at least 25% more than current size.

Provided values are expected to grow significantly over time.

### Run gamebankd
E.g., `./gamebankd --webserver-ws-endpoint=0.0.0.0:8090 --webserver-http-endpoint=0.0.0.0:8090 --p2p-endpoint=0.0.0.0:2001 --data-dir=/home/node3/data/ --tags-skip-startup-update`