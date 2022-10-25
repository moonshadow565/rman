#!/bin/bash
echo 'Set of CLI tools for rito manifest and bundle files'
echo ''
for f in $1/rbun-*; do
    echo '```sh'
    $f --help
    echo '```'
    echo ''
done
for f in $1/rman-*; do
    echo '```sh'
    $f --help
    echo '```'
    echo ''
done

