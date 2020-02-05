# %global selinux_variants mls strict targeted

Name:		bitcoin-sv
Version:	1.0.1
Release:	1%{?dist}
Summary:	Peer to Peer Cryptographic Currency

Group:		Applications/System
License:	MIT
URL:		https://bitcoinsv.io
Source0:	https://github.com/bitcoin-sv/bitcoin-sv/archive/v%{version}.tar.gz

Source10:	https://raw.githubusercontent.com/bitcoin-sv/bitcoin-sv/v%{version}/contrib/debian/examples/bitcoin.conf

### SELINUX PART REQUIRES FURTHER WORK
# #selinux
# Source30:	https://raw.githubusercontent.com/bitcoin-sv/bitcoin-sv/v%{version}/contrib/rpm/bitcoin-sv.te
# # Source31 - what about bitcoin-sv-tx and bench_bitcoin-sv ???
# Source31:	https://raw.githubusercontent.com/bitcoin-sv/bitcoin-sv/v%{version}/contrib/rpm/bitcoin-sv.fc
# Source32:	https://raw.githubusercontent.com/bitcoin-sv/bitcoin-sv/v%{version}/contrib/rpm/bitcoin-sv.if

Source100:	https://bitbay.net/helpdesk/bitcoin-cryptocurrencies/bitcoin-sv-bsv/bsv1024.png

BuildRequires:	openssl-devel
BuildRequires:	boost169-devel
BuildRequires:	miniupnpc-devel
BuildRequires:	autoconf automake libtool
BuildRequires:	libevent-devel
BuildRequires:	which
BuildRequires:	centos-release-scl
BuildRequires:	devtoolset-7-gcc
BuildRequires:	devtoolset-7-gcc-c++
BuildRequires:	libdb-cxx-devel
BuildRequires:	libdb-devel

%description
bitcoin-sv is a digital cryptographic currency that uses peer-to-peer technology to
operate with no central authority or banks; managing transactions and the
issuing of bitcoin-svs is carried out collectively by the network.

%package libs
Summary:	bitcoin-sv shared libraries
Group:		System Environment/Libraries

%description libs
This package provides the bitcoin-svconsensus shared libraries. These libraries
may be used by third party software to provide consensus verification
functionality.

Unless you know need this package, you probably do not.

%package devel
Summary:	Development files for bitcoin-sv
Group:		Development/Libraries
Requires:	%{name}-libs = %{version}-%{release}

%description devel
This package contains the header files and static library for the
bitcoin-svconsensus shared library. If you are developing or compiling software
that wants to link against that library, then you need this package installed.

Most people do not need this package installed.

%package server
Summary:	The bitcoin-sv daemon
Group:		System Environment/Daemons
Requires:	bitcoin-sv-utils = %{version}-%{release}
Requires:	selinux-policy policycoreutils-python libdb libdb-cxx
Requires(pre):	shadow-utils
Requires(post):	%{_sbindir}/semodule %{_sbindir}/restorecon %{_sbindir}/fixfiles %{_sbindir}/sestatus
Requires(postun):	%{_sbindir}/semodule %{_sbindir}/restorecon %{_sbindir}/fixfiles %{_sbindir}/sestatus
BuildRequires:	systemd
BuildRequires:	checkpolicy
BuildRequires:	%{_datadir}/selinux/devel/Makefile

%description server
This package provides a stand-alone bitcoin-sv-core daemon. For most users, this
package is only needed if they need a full-node without the graphical client.

Some third party wallet software will want this package to provide the actual
bitcoin-sv-core node they use to connect to the network.

If you use the graphical bitcoin-sv-core client then you almost certainly do not
need this package.

%package utils
Summary:	bitcoin-sv utilities
Group:		Applications/System

%description utils
This package provides several command line utilities for interacting with a
bitcoin-sv-core daemon.

The bitcoin-sv-cli utility allows you to communicate and control a bitcoin-sv daemon
over RPC, the bitcoin-sv-tx utility allows you to create a custom transaction, and
the bench_bitcoin-sv utility can be used to perform some benchmarks.

This package contains utilities needed by the bitcoin-sv-server package.


%prep
%setup -q
cp -p %{SOURCE10} ./bitcoin-sv.conf.example
mkdir db4
#mkdir SELinux - selinux support requires further work
#cp -p %{SOURCE30} %{SOURCE31} %{SOURCE32} SELinux/


%build
. /opt/rh/devtoolset-7/enable
CWD=`pwd`

./autogen.sh
%configure CPPFLAGS="-I/usr/include/boost169" --with-miniupnpc --enable-glibc-back-compat --with-boost-libdir=/usr/lib64/boost169 %{buildargs}
make %{?_smp_mflags}

# pushd SELinux
# for selinuxvariant in %{selinux_variants}; do
# 	make NAME=${selinuxvariant} -f %{_datadir}/selinux/devel/Makefile
# 	mv bitcoin-sv.pp bitcoin-sv.pp.${selinuxvariant}
# 	make NAME=${selinuxvariant} -f %{_datadir}/selinux/devel/Makefile clean
# done
# popd


%install
make install DESTDIR=%{buildroot}

mkdir -p -m755 %{buildroot}%{_sbindir}
# rename bitcoin to bitcoin-sv as one can have both of them installed
mv %{buildroot}%{_bindir}/bitcoind %{buildroot}%{_sbindir}/bitcoin-svd
mv %{buildroot}%{_bindir}/bitcoin-cli %{buildroot}%{_bindir}/bitcoin-sv-cli
mv %{buildroot}%{_bindir}/bitcoin-tx %{buildroot}%{_bindir}/bitcoin-sv-tx
mv %{buildroot}%{_bindir}/bench_bitcoin %{buildroot}%{_bindir}/bench_bitcoin-sv
mv %{buildroot}%{_bindir}/bitcoin-miner %{buildroot}%{_bindir}/bitcoin-sv-miner
mv %{buildroot}%{_bindir}/bitcoin-seeder %{buildroot}%{_bindir}/bitcoin-sv-seeder
# remove manual pages as they are for "bitcoin" name
rm -rf %{buildroot}/usr/share/man/man1/bitcoin*

# systemd stuff
mkdir -p %{buildroot}%{_tmpfilesdir}
cat <<EOF > %{buildroot}%{_tmpfilesdir}/bitcoin-sv.conf
d /run/bitcoin-svd 0750 bitcoin-sv bitcoin-sv -
EOF
touch -a -m -t 201504280000 %{buildroot}%{_tmpfilesdir}/bitcoin-sv.conf

mkdir -p %{buildroot}%{_sysconfdir}/sysconfig
cat <<EOF > %{buildroot}%{_sysconfdir}/sysconfig/bitcoin-sv
# Provide options to the bitcoin-sv daemon here, for example
# OPTIONS="-testnet -disable-wallet"

OPTIONS=""

# System service defaults.
# Don't change these unless you know what you're doing.
CONFIG_FILE="%{_sysconfdir}/bitcoin-sv/bitcoin-sv.conf"
DATA_DIR="%{_localstatedir}/lib/bitcoin-sv"
PID_FILE="/run/bitcoin-svd/bitcoin-svd.pid"
EOF
touch -a -m -t 201504280000 %{buildroot}%{_sysconfdir}/sysconfig/bitcoin-sv

mkdir -p %{buildroot}%{_unitdir}
cat <<EOF > %{buildroot}%{_unitdir}/bitcoin-sv.service
[Unit]
Description=bitcoin-sv daemon
After=syslog.target network.target

[Service]
Type=forking
ExecStart=%{_sbindir}/bitcoin-svd -daemon -conf=\${CONFIG_FILE} -datadir=\${DATA_DIR} -pid=\${PID_FILE} \$OPTIONS
EnvironmentFile=%{_sysconfdir}/sysconfig/bitcoin-sv
User=bitcoin-sv
Group=bitcoin-sv

Restart=on-failure
PrivateTmp=true
TimeoutStopSec=120
TimeoutStartSec=60
StartLimitInterval=240
StartLimitBurst=5

[Install]
WantedBy=multi-user.target
EOF
touch -a -m -t 201504280000 %{buildroot}%{_unitdir}/bitcoin-sv.service
#end systemd stuff

mkdir %{buildroot}%{_sysconfdir}/bitcoin-sv
mkdir -p %{buildroot}%{_localstatedir}/lib/bitcoin-sv

# #SELinux
# for selinuxvariant in %{selinux_variants}; do
# 	install -d %{buildroot}%{_datadir}/selinux/${selinuxvariant}
# 	install -p -m 644 SELinux/bitcoin-sv.pp.${selinuxvariant} %{buildroot}%{_datadir}/selinux/${selinuxvariant}/bitcoin-sv.pp
# done

# nuke these, we do extensive testing of binaries in %%check before packaging
rm -f %{buildroot}%{_bindir}/test_*

%check
make check

%post libs -p /sbin/ldconfig

%postun libs -p /sbin/ldconfig

%pre server
getent group bitcoin-sv >/dev/null || groupadd -r bitcoin-sv
getent passwd bitcoin-sv >/dev/null ||
	useradd -r -g bitcoin-sv -d /var/lib/bitcoin-sv -s /sbin/nologin \
	-c "bitcoin-sv wallet server" bitcoin-sv
exit 0

%post server
%systemd_post bitcoin-sv.service
# # SELinux
# if [ `%{_sbindir}/sestatus |grep -c "disabled"` -eq 0 ]; then
# for selinuxvariant in %{selinux_variants}; do
# 	%{_sbindir}/semodule -s ${selinuxvariant} -i %{_datadir}/selinux/${selinuxvariant}/bitcoin-sv.pp &> /dev/null || :
# done
# %{_sbindir}/semanage port -a -t bitcoin-sv_port_t -p tcp 8332
# %{_sbindir}/semanage port -a -t bitcoin-sv_port_t -p tcp 8333
# %{_sbindir}/semanage port -a -t bitcoin-sv_port_t -p tcp 18332
# %{_sbindir}/semanage port -a -t bitcoin-sv_port_t -p tcp 18333
# %{_sbindir}/fixfiles -R bitcoin-sv-server restore &> /dev/null || :
# %{_sbindir}/restorecon -R %{_localstatedir}/lib/bitcoin-sv || :
# fi

%posttrans server
%{_bindir}/systemd-tmpfiles --create

%preun server
%systemd_preun bitcoin-sv.service

%postun server
%systemd_postun bitcoin-sv.service
# # SELinux
# if [ $1 -eq 0 ]; then
# 	if [ `%{_sbindir}/sestatus |grep -c "disabled"` -eq 0 ]; then
# 	%{_sbindir}/semanage port -d -p tcp 8332
# 	%{_sbindir}/semanage port -d -p tcp 8333
# 	%{_sbindir}/semanage port -d -p tcp 18332
# 	%{_sbindir}/semanage port -d -p tcp 18333
# 	for selinuxvariant in %{selinux_variants}; do
# 		%{_sbindir}/semodule -s ${selinuxvariant} -r bitcoin-sv &> /dev/null || :
# 	done
# 	%{_sbindir}/fixfiles -R bitcoin-sv-server restore &> /dev/null || :
# 	[ -d %{_localstatedir}/lib/bitcoin-sv ] && \
# 		%{_sbindir}/restorecon -R %{_localstatedir}/lib/bitcoin-sv &> /dev/null || :
# 	fi
# fi

%clean
rm -rf %{buildroot}

%files libs
%defattr(-,root,root,-)
%license COPYING
%doc COPYING doc/README.md doc/shared-libraries.md
%{_libdir}/lib*.so.*

%files devel
%defattr(-,root,root,-)
%license COPYING
%doc COPYING doc/README.md doc/developer-notes.md doc/shared-libraries.md
%attr(0644,root,root) %{_includedir}/*.h
%{_libdir}/*.so
%{_libdir}/*.a
%{_libdir}/*.la
%attr(0644,root,root) %{_libdir}/pkgconfig/*.pc

%files server
%defattr(-,root,root,-)
%license COPYING
%doc COPYING bitcoin-sv.conf.example doc/README.md
%attr(0755,root,root) %{_sbindir}/bitcoin-svd
%attr(0755,root,root) %{_bindir}/bitcoin-sv-miner
%attr(0755,root,root) %{_bindir}/bitcoin-sv-seeder
%attr(0644,root,root) %{_tmpfilesdir}/bitcoin-sv.conf
%attr(0644,root,root) %{_unitdir}/bitcoin-sv.service
%dir %attr(0750,bitcoin-sv,bitcoin-sv) %{_sysconfdir}/bitcoin-sv
%dir %attr(0750,bitcoin-sv,bitcoin-sv) %{_localstatedir}/lib/bitcoin-sv
%config(noreplace) %attr(0600,root,root) %{_sysconfdir}/sysconfig/bitcoin-sv
# %attr(0644,root,root) %{_datadir}/selinux/*/*.pp
# %attr(0644,root,root) %{_mandir}/man1/bitcoin-sv-cli.1.gz
# %attr(0644,root,root) %{_mandir}/man1/bitcoin-sv-tx.1.gz
# %attr(0644,root,root) %{_mandir}/man1/bitcoin-svd.1.gz

%files utils
%defattr(-,root,root,-)
%license COPYING
%doc COPYING bitcoin-sv.conf.example doc/README.md
%attr(0755,root,root) %{_bindir}/bitcoin-sv-cli
%attr(0755,root,root) %{_bindir}/bitcoin-sv-tx
%attr(0755,root,root) %{_bindir}/bench_bitcoin-sv



%changelog
* Fri Feb 26 2016 Alice Wonder <buildmaster@librelamp.com> - 0.12.0-2
- Rename Qt package from bitcoin-sv to bitcoin-sv-core
- Make building of the Qt package optional
- When building the Qt package, default to Qt5 but allow building
-  against Qt4
- Only run SELinux stuff in post scripts if it is not set to disabled

* Wed Feb 24 2016 Alice Wonder <buildmaster@librelamp.com> - 0.12.0-1
- Initial spec file for 0.12.0 release

# This spec file is written from scratch but a lot of the packaging decisions are directly
# based upon the 0.11.2 package spec file from https://www.ringingliberty.com/bitcoin-sv/
