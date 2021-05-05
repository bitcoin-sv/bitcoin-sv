#!/bin/bash
cwd=$(dirname $0)
yum -y install epel-release; yum -y install centos-release-scl git vim rpm-build rpmdevtools
rpmdev-setuptree
spectool -g -R ${cwd}/bitcoin.spec
yum-builddep -y ${cwd}/bitcoin.spec
rpmbuild -bb ${cwd}/bitcoin.spec