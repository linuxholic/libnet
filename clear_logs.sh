#!/bin/bash

for i in {0..2}
do
    log_file="raft-server-$i.log"
    > "$log_file"
    rm -f ./replicated-$i.log
done
